#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/paged_kv_cache.h>
#include <xaios/vmm.h>

/*
 * Paged KV Cache Implementation
 *
 * Non-contiguous memory allocation for LLM attention cache.
 * Allocates fixed-size blocks on-demand, eliminating fragmentation.
 */

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1U) & ~(align - 1U);
}

xaios_status_t paged_kv_cache_init(xaios_paged_kv_cache_t *cache,
                                   uint32_t cache_id,
                                   uint32_t num_blocks,
                                   uint32_t head_dim,
                                   uint32_t num_heads,
                                   uint32_t block_size) {
  kassert(cache != 0 && num_blocks > 0 && head_dim > 0);

  cache->cache_id = cache_id;
  cache->num_blocks_total = num_blocks;
  cache->num_blocks_free = num_blocks;
  cache->block_size = block_size;
  cache->head_dim = head_dim;
  cache->num_heads = num_heads;
  
  /* Calculate bytes per block: 2 × (head_dim × block_size × sizeof(float)) */
  cache->bytes_per_block = 2U * (uint64_t)head_dim * block_size * sizeof(float);
  cache->bytes_per_block = align_up(cache->bytes_per_block, 4096U); /* Page-align */
  
  /* Allocate block array */
  cache->blocks = (xaios_kv_block_t *)__builtin_alloca(
      num_blocks * sizeof(xaios_kv_block_t));
  
  /* Allocate physical memory for all blocks */
  for (uint32_t i = 0; i < num_blocks; ++i) {
    cache->blocks[i].block_id = i;
    cache->blocks[i].ref_count = 0;
    cache->blocks[i].token_count = 0;
    
    /* Allocate key and value cache (page-aligned) */
    uint64_t half_bytes = cache->bytes_per_block / 2U;
    cache->blocks[i].key_cache = (void *)(uintptr_t)(0xFFFF000000000000ULL + 
        (uint64_t)i * cache->bytes_per_block);
    cache->blocks[i].value_cache = (void *)(uintptr_t)(0xFFFF000000000000ULL + 
        (uint64_t)i * cache->bytes_per_block + half_bytes);
    
    bytes_zero(cache->blocks[i].key_cache, half_bytes);
    bytes_zero(cache->blocks[i].value_cache, half_bytes);
  }
  
  /* Initialize free list */
  cache->free_block_count = num_blocks;
  for (uint32_t i = 0; i < num_blocks; ++i) {
    cache->free_block_indices[i] = i;
  }
  
  /* Initialize sequences */
  cache->num_sequences = 0;
  for (uint32_t i = 0; i < XAIOS_KV_MAX_SEQUENCES; ++i) {
    cache->sequences[i].seq_id = i;
    cache->sequences[i].num_blocks = 0;
    cache->sequences[i].total_tokens = 0;
    cache->sequences[i].head_dim = head_dim;
    cache->sequences[i].num_heads = num_heads;
    cache->sequences[i].kv_bytes_allocated = 0;
  }
  
  /* Initialize statistics */
  cache->allocation_count = 0;
  cache->eviction_count = 0;
  cache->hit_count = 0;
  cache->miss_count = 0;
  
  klog("paged-kv-cache: initialized cache_id=%u blocks=%u head_dim=%u num_heads=%u block_size=%u bytes_per_block=%lu\n",
       cache_id, num_blocks, head_dim, num_heads, block_size,
       cache->bytes_per_block);
  
  return XAIOS_OK;
}

void paged_kv_cache_destroy(xaios_paged_kv_cache_t *cache) {
  kassert(cache != 0);
  
  /* Free all sequences */
  for (uint32_t i = 0; i < cache->num_sequences; ++i) {
    cache->sequences[i].num_blocks = 0;
    cache->sequences[i].total_tokens = 0;
  }
  cache->num_sequences = 0;
  
  /* Reset free list */
  cache->free_block_count = cache->num_blocks_total;
  for (uint32_t i = 0; i < cache->num_blocks_total; ++i) {
    cache->free_block_indices[i] = i;
    cache->blocks[i].ref_count = 0;
    cache->blocks[i].token_count = 0;
  }
  
  klog("paged-kv-cache: destroyed cache_id=%u\n", cache->cache_id);
}

static int find_sequence(const xaios_paged_kv_cache_t *cache, uint32_t seq_id) {
  for (uint32_t i = 0; i < XAIOS_KV_MAX_SEQUENCES; ++i) {
    if (cache->sequences[i].seq_id == seq_id &&
        cache->sequences[i].num_blocks > 0) {
      return (int)i;
    }
  }
  return -1;
}

static uint32_t allocate_block(xaios_paged_kv_cache_t *cache) {
  if (cache->free_block_count == 0) {
    return UINT32_MAX; /* Out of memory */
  }
  
  uint32_t block_idx = cache->free_block_indices[cache->free_block_count - 1];
  cache->free_block_count--;
  cache->blocks[block_idx].ref_count = 1;
  cache->allocation_count++;
  
  return block_idx;
}

static void free_block(xaios_paged_kv_cache_t *cache, uint32_t block_idx) {
  kassert(block_idx < cache->num_blocks_total);
  
  cache->blocks[block_idx].ref_count = 0;
  cache->blocks[block_idx].token_count = 0;
  cache->free_block_indices[cache->free_block_count] = block_idx;
  cache->free_block_count++;
}

xaios_status_t paged_kv_cache_create_sequence(xaios_paged_kv_cache_t *cache,
                                              uint32_t *seq_id_out) {
  kassert(cache != 0 && seq_id_out != 0);
  
  /* Find empty sequence slot */
  int slot = -1;
  for (uint32_t i = 0; i < XAIOS_KV_MAX_SEQUENCES; ++i) {
    if (cache->sequences[i].num_blocks == 0) {
      slot = (int)i;
      break;
    }
  }
  
  if (slot < 0) {
    return XAIOS_ERR_NO_MEMORY; /* Max sequences reached */
  }
  
  cache->sequences[slot].seq_id = slot;
  cache->sequences[slot].num_blocks = 0;
  cache->sequences[slot].total_tokens = 0;
  cache->sequences[slot].kv_bytes_allocated = 0;
  cache->num_sequences++;
  
  *seq_id_out = slot;
  
  klog("paged-kv-cache: created sequence seq_id=%u\n", slot);
  return XAIOS_OK;
}

xaios_status_t paged_kv_cache_destroy_sequence(xaios_paged_kv_cache_t *cache,
                                               uint32_t seq_id) {
  kassert(cache != 0);
  
  int slot = find_sequence(cache, seq_id);
  if (slot < 0) {
    return XAIOS_ERR_INVALID;
  }
  
  /* Free all blocks */
  xaios_kv_sequence_t *seq = &cache->sequences[slot];
  for (uint32_t i = 0; i < seq->num_blocks; ++i) {
    uint32_t block_idx = seq->block_indices[i];
    cache->blocks[block_idx].ref_count--;
    if (cache->blocks[block_idx].ref_count == 0) {
      free_block(cache, block_idx);
    }
  }
  
  seq->num_blocks = 0;
  seq->total_tokens = 0;
  seq->kv_bytes_allocated = 0;
  cache->num_sequences--;
  
  cache->eviction_count++;
  
  klog("paged-kv-cache: destroyed sequence seq_id=%u blocks_freed=%u\n",
       seq_id, seq->num_blocks);
  
  return XAIOS_OK;
}

xaios_status_t paged_kv_cache_append_tokens(xaios_paged_kv_cache_t *cache,
                                            uint32_t seq_id,
                                            const void *keys,
                                            const void *values,
                                            uint32_t num_tokens) {
  kassert(cache != 0 && keys != 0 && values != 0);
  
  int slot = find_sequence(cache, seq_id);
  if (slot < 0) {
    return XAIOS_ERR_INVALID;
  }
  
  xaios_kv_sequence_t *seq = &cache->sequences[slot];
  const uint8_t *key_bytes = (const uint8_t *)keys;
  const uint8_t *value_bytes = (const uint8_t *)values;
  
  uint32_t tokens_remaining = num_tokens;
  uint64_t key_offset = 0;
  uint64_t value_offset = 0;
  
  while (tokens_remaining > 0) {
    /* Determine current block and position within block */
    uint32_t current_block_idx = seq->num_blocks > 0 
        ? seq->block_indices[seq->num_blocks - 1] 
        : UINT32_MAX;
    uint32_t tokens_in_current_block = current_block_idx != UINT32_MAX
        ? cache->blocks[current_block_idx].token_count
        : cache->block_size; /* Force new block */
    
    /* Allocate new block if current is full */
    if (tokens_in_current_block >= cache->block_size) {
      current_block_idx = allocate_block(cache);
      if (current_block_idx == UINT32_MAX) {
        return XAIOS_ERR_NO_MEMORY;
      }
      
      if (seq->num_blocks >= XAIOS_KV_MAX_BLOCKS_PER_SEQ) {
        free_block(cache, current_block_idx);
        return XAIOS_ERR_NO_MEMORY; /* Max blocks per sequence */
      }
      
      seq->block_indices[seq->num_blocks] = current_block_idx;
      seq->num_blocks++;
      cache->blocks[current_block_idx].token_count = 0;
    }
    
    /* Copy tokens into current block */
    uint32_t space_in_block = cache->block_size - cache->blocks[current_block_idx].token_count;
    uint32_t tokens_to_copy = tokens_remaining < space_in_block 
        ? tokens_remaining 
        : space_in_block;
    
    uint64_t bytes_per_token = (uint64_t)cache->head_dim * sizeof(float);
    uint64_t copy_bytes = tokens_to_copy * bytes_per_token;
    
    /* Copy key cache */
    uint8_t *key_dest = (uint8_t *)cache->blocks[current_block_idx].key_cache +
        (cache->blocks[current_block_idx].token_count * bytes_per_token);
    for (uint64_t i = 0; i < copy_bytes; ++i) {
      key_dest[i] = key_bytes[key_offset + i];
    }
    
    /* Copy value cache */
    uint8_t *value_dest = (uint8_t *)cache->blocks[current_block_idx].value_cache +
        (cache->blocks[current_block_idx].token_count * bytes_per_token);
    for (uint64_t i = 0; i < copy_bytes; ++i) {
      value_dest[i] = value_bytes[value_offset + i];
    }
    
    cache->blocks[current_block_idx].token_count += tokens_to_copy;
    seq->total_tokens += tokens_to_copy;
    
    key_offset += copy_bytes;
    value_offset += copy_bytes;
    tokens_remaining -= tokens_to_copy;
  }
  
  seq->kv_bytes_allocated = seq->num_blocks * cache->bytes_per_block;
  
  return XAIOS_OK;
}

xaios_status_t paged_kv_cache_get_block_table(
    const xaios_paged_kv_cache_t *cache,
    uint32_t seq_id,
    const uint32_t **block_indices_out,
    uint32_t *num_blocks_out) {
  kassert(cache != 0 && block_indices_out != 0 && num_blocks_out != 0);
  
  int slot = find_sequence(cache, seq_id);
  if (slot < 0) {
    return XAIOS_ERR_INVALID;
  }
  
  const xaios_kv_sequence_t *seq = &cache->sequences[slot];
  *block_indices_out = seq->block_indices;
  *num_blocks_out = seq->num_blocks;
  
  return XAIOS_OK;
}

uint32_t paged_kv_cache_get_token_count(const xaios_paged_kv_cache_t *cache,
                                        uint32_t seq_id) {
  kassert(cache != 0);
  
  int slot = find_sequence(cache, seq_id);
  if (slot < 0) {
    return 0;
  }
  
  return cache->sequences[slot].total_tokens;
}

void paged_kv_cache_get_stats(const xaios_paged_kv_cache_t *cache,
                              uint64_t *allocation_count,
                              uint64_t *eviction_count,
                              uint64_t *hit_count,
                              uint64_t *miss_count,
                              uint32_t *num_blocks_free) {
  kassert(cache != 0);
  
  if (allocation_count != 0) *allocation_count = cache->allocation_count;
  if (eviction_count != 0) *eviction_count = cache->eviction_count;
  if (hit_count != 0) *hit_count = cache->hit_count;
  if (miss_count != 0) *miss_count = cache->miss_count;
  if (num_blocks_free != 0) *num_blocks_free = cache->free_block_count;
}
