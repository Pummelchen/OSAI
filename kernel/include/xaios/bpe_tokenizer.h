#ifndef XAIOS_BPE_TOKENIZER_H
#define XAIOS_BPE_TOKENIZER_H

#include <xaios/types.h>

/* BPE merge rule */
typedef struct xaios_bpe_rule {
  uint32_t token_a;      /* First token ID */
  uint32_t token_b;      /* Second token ID */
  uint32_t merged_token; /* Result token ID */
  int32_t priority;      /* Lower = higher priority */
} xaios_bpe_rule_t;

/* BPE tokenizer state */
typedef struct xaios_bpe_tokenizer {
  const char **vocab;           /* Vocabulary array (token strings) */
  uint32_t vocab_size;          /* Number of tokens in vocabulary */
  const xaios_bpe_rule_t *rules; /* Merge rules (sorted by priority) */
  uint32_t rule_count;          /* Number of merge rules */
  uint8_t *working_buffer;      /* Working buffer for tokenization */
  uint64_t working_size;        /* Size of working buffer */
} xaios_bpe_tokenizer_t;

#endif
