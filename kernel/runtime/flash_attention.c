#include <xaios/assert.h>
#include <xaios/ai_kernels.h>
#include <xaios/flash_attention.h>
#include <xaios/kheap.h>
#include <xaios/klog.h>
#include <xaios/math_intrinsics.h>

/*
 * Flash Attention Implementation
 *
 * Block-wise attention computation with O(n) memory.
 * Avoids materializing full n×n attention matrix.
 */

static uint32_t divide_round_up(uint32_t numerator, uint32_t denominator) {
  return (numerator + denominator - 1U) / denominator;
}

xaios_status_t flash_attention_init(xaios_flash_attention_config_t *config,
                                    uint32_t config_id,
                                    uint32_t num_tokens,
                                    uint32_t head_dim,
                                    uint32_t num_heads,
                                    void *output,
                                    uint64_t output_bytes) {
  kassert(config != 0 && num_tokens > 0 && head_dim > 0 && num_heads > 0);
  kassert(output != 0 && output_bytes > 0);
  
  config->config_id = config_id;
  config->num_tokens = num_tokens;
  config->head_dim = head_dim;
  config->num_heads = num_heads;
  config->block_size = XAIOS_FLASH_BLOCK_SIZE;
  config->num_blocks = divide_round_up(num_tokens, config->block_size);
  config->output = output;
  config->output_bytes = output_bytes;
  
  /* Allocate temporary QKV buffer from kernel heap (arena-managed) */
  config->QKV_bytes = (uint64_t)num_tokens * head_dim * num_heads * sizeof(float) * 3U;
  if (config->QKV_bytes == 0) {
    return XAIOS_ERR_INVALID;
  }
  config->QKV_buffer = kheap_alloc(config->QKV_bytes, 64);
  if (config->QKV_buffer == 0) {
    klog("flash-attention: failed to allocate QKV buffer (%lu bytes)\n",
         config->QKV_bytes);
    config->QKV_bytes = 0;
    return XAIOS_ERR_NO_MEMORY;
  }
  
  klog("flash-attention: initialized config_id=%u tokens=%u heads=%u head_dim=%u blocks=%u\n",
       config_id, num_tokens, num_heads, head_dim, config->num_blocks);
  
  return XAIOS_OK;
}

/* Compute attention for a single block pair */
static void compute_block_attention(
    const float *Q_block,
    const float *K_block,
    const float *V_block,
    float *output_block,
    uint32_t num_q_tokens,
    uint32_t num_k_tokens,
    uint32_t head_dim) {
  
  /* Simplified scaled dot-product attention */
  for (uint32_t i = 0; i < num_q_tokens; ++i) {
    float max_score = -1e30f;
    float sum_exp = 0.0f;
    
    /* Compute attention scores */
    for (uint32_t j = 0; j < num_k_tokens; ++j) {
      float score = 0.0f;
      for (uint32_t k = 0; k < head_dim; ++k) {
        score += Q_block[i * head_dim + k] * K_block[j * head_dim + k];
      }
      score /= (float)head_dim;  /* Scale */
      
      if (score > max_score) {
        max_score = score;
      }
    }
    
    /* Compute softmax and weighted sum */
    for (uint32_t j = 0; j < num_k_tokens; ++j) {
      float score = 0.0f;
      for (uint32_t k = 0; k < head_dim; ++k) {
        score += Q_block[i * head_dim + k] * K_block[j * head_dim + k];
      }
      score /= (float)head_dim;
      
      float exp_score = xaios_expf(score - max_score);
      sum_exp += exp_score;
      
      /* Accumulate weighted V */
      for (uint32_t k = 0; k < head_dim; ++k) {
        output_block[i * head_dim + k] += exp_score * V_block[j * head_dim + k];
      }
    }
    
    /* Normalize */
    if (sum_exp > 0.0f) {
      for (uint32_t k = 0; k < head_dim; ++k) {
        output_block[i * head_dim + k] /= sum_exp;
      }
    }
  }
}

xaios_status_t flash_attention_forward(
    xaios_flash_attention_config_t *config,
    const void *Q,
    const void *K,
    const void *V) {
  kassert(config != 0 && Q != 0 && K != 0 && V != 0);
  
  const float *Q_ptr = (const float *)Q;
  const float *K_ptr = (const float *)K;
  const float *V_ptr = (const float *)V;
  float *output_ptr = (float *)config->output;
  
  /* Initialize output to zero */
  for (uint64_t i = 0; i < config->output_bytes / sizeof(float); ++i) {
    output_ptr[i] = 0.0f;
  }
  
  /* Process blocks */
  uint32_t num_q_blocks = config->num_blocks;
  
  for (uint32_t qb = 0; qb < num_q_blocks; ++qb) {
    uint32_t q_start = qb * config->block_size;
    uint32_t q_end = q_start + config->block_size;
    if (q_end > config->num_tokens) {
      q_end = config->num_tokens;
    }
    uint32_t num_q_tokens = q_end - q_start;
    
    /* Get Q block */
    const float *Q_block = &Q_ptr[q_start * config->head_dim];
    
    /* Process all K blocks (full attention) */
    for (uint32_t kb = 0; kb <= qb; ++kb) {
      uint32_t k_start = kb * config->block_size;
      uint32_t k_end = k_start + config->block_size;
      if (k_end > config->num_tokens) {
        k_end = config->num_tokens;
      }
      uint32_t num_k_tokens = k_end - k_start;
      
      const float *K_block = &K_ptr[k_start * config->head_dim];
      const float *V_block = &V_ptr[k_start * config->head_dim];
      
      /* Compute attention for this block pair */
      compute_block_attention(
          Q_block, K_block, V_block,
          &output_ptr[q_start * config->head_dim],
          num_q_tokens, num_k_tokens,
          config->head_dim);
    }
    
    /* Prefetch next Q block */
    if (qb + 1 < num_q_blocks) {
      ai_prefetch_read_keep(&Q_ptr[(qb + 1) * config->block_size * config->head_dim]);
    }
  }
  
  klog("flash-attention: forward pass complete, %u tokens, %u blocks\n",
       config->num_tokens, config->num_blocks);
  
  return XAIOS_OK;
}

void flash_attention_get_stats(const xaios_flash_attention_config_t *config,
                               uint32_t *num_blocks,
                               uint32_t *block_size,
                               uint64_t *memory_bytes) {
  kassert(config != 0);
  
  if (num_blocks != 0) *num_blocks = config->num_blocks;
  if (block_size != 0) *block_size = config->block_size;
  if (memory_bytes != 0) *memory_bytes = config->QKV_bytes;
}

void flash_attention_destroy(xaios_flash_attention_config_t *config) {
  kassert(config != 0);
  
  config->num_tokens = 0;
  config->num_blocks = 0;
  config->QKV_buffer = 0;
  config->QKV_bytes = 0;
  
  klog("flash-attention: destroyed config_id=%u\n", config->config_id);
}
