#ifndef XAIOS_PAGED_KV_CACHE_H
#define XAIOS_PAGED_KV_CACHE_H

#include <xaios/status.h>
#include <xaios/types.h>

/*
 * Paged KV Cache for LLM Inference
 *
 * Implements non-contiguous KV cache allocation similar to vLLM's paged attention.
 * Benefits:
 * - No memory fragmentation
 * - Dynamic allocation per sequence
 * - Support long contexts (128K+ tokens)
 * - 2-5× memory efficiency over linear KV cache
 */

#define XAIOS_KV_BLOCK_SIZE 16U        /* Tokens per block */
#define XAIOS_KV_MAX_BLOCKS_PER_SEQ 8192U  /* Max 128K tokens (16 × 8192) */
#define XAIOS_KV_MAX_SEQUENCES 64U     /* Concurrent sequences per cell */

/* KV cache block */
typedef struct xaios_kv_block {
  uint32_t block_id;
  void *key_cache;      /* Key cache for this block (head_dim × block_size) */
  void *value_cache;    /* Value cache for this block (head_dim × block_size) */
  uint32_t ref_count;   /* Reference count for sharing */
  uint32_t token_count; /* Number of tokens stored (≤ block_size) */
} xaios_kv_block_t;

/* Sequence state */
typedef struct xaios_kv_sequence {
  uint32_t seq_id;
  uint32_t num_blocks;
  uint32_t block_indices[XAIOS_KV_MAX_BLOCKS_PER_SEQ]; /* Logical → physical mapping */
  uint32_t total_tokens;
  uint32_t head_dim;
  uint32_t num_heads;
  uint64_t kv_bytes_allocated;
} xaios_kv_sequence_t;

/* Paged KV cache manager */
typedef struct xaios_paged_kv_cache {
  uint32_t cache_id;
  uint32_t num_blocks_total;
  uint32_t num_blocks_free;
  uint32_t block_size;
  uint32_t head_dim;
  uint32_t num_heads;
  uint64_t bytes_per_block;
  
  xaios_kv_block_t *blocks;
  uint32_t free_block_count;
  uint32_t free_block_indices[XAIOS_KV_MAX_BLOCKS_PER_SEQ];
  
  xaios_kv_sequence_t sequences[XAIOS_KV_MAX_SEQUENCES];
  uint32_t num_sequences;
  
  /* Statistics */
  uint64_t allocation_count;
  uint64_t eviction_count;
  uint64_t hit_count;
  uint64_t miss_count;
} xaios_paged_kv_cache_t;

/* Initialize paged KV cache */
xaios_status_t paged_kv_cache_init(xaios_paged_kv_cache_t *cache,
                                   uint32_t cache_id,
                                   uint32_t num_blocks,
                                   uint32_t head_dim,
                                   uint32_t num_heads,
                                   uint32_t block_size);

/* Destroy paged KV cache and free all blocks */
void paged_kv_cache_destroy(xaios_paged_kv_cache_t *cache);

/* Create a new sequence */
xaios_status_t paged_kv_cache_create_sequence(xaios_paged_kv_cache_t *cache,
                                              uint32_t *seq_id_out);

/* Destroy a sequence and free its blocks */
xaios_status_t paged_kv_cache_destroy_sequence(xaios_paged_kv_cache_t *cache,
                                               uint32_t seq_id);

/* Append tokens to sequence (allocates blocks as needed) */
xaios_status_t paged_kv_cache_append_tokens(xaios_paged_kv_cache_t *cache,
                                            uint32_t seq_id,
                                            const void *keys,
                                            const void *values,
                                            uint32_t num_tokens);

/* Get block table for attention computation */
xaios_status_t paged_kv_cache_get_block_table(
    const xaios_paged_kv_cache_t *cache,
    uint32_t seq_id,
    const uint32_t **block_indices_out,
    uint32_t *num_blocks_out);

/* Get total tokens in sequence */
uint32_t paged_kv_cache_get_token_count(const xaios_paged_kv_cache_t *cache,
                                        uint32_t seq_id);

/* Get cache statistics */
void paged_kv_cache_get_stats(const xaios_paged_kv_cache_t *cache,
                              uint64_t *allocation_count,
                              uint64_t *eviction_count,
                              uint64_t *hit_count,
                              uint64_t *miss_count,
                              uint32_t *num_blocks_free);

#endif
