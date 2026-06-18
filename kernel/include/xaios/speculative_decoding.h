#ifndef XAIOS_SPECULATIVE_DECODING_H
#define XAIOS_SPECULATIVE_DECODING_H

#include <xaios/status.h>
#include <xaios/types.h>

/*
 * Speculative Decoding for 2-3× LLM Speedup
 *
 * Uses a small "draft" model to generate candidate tokens,
 * then verifies them with the large "target" model.
 *
 * Flow:
 * 1. Draft model (fast) generates K candidate tokens
 * 2. Target model (accurate) verifies all K tokens in parallel
 * 3. Accept matching tokens, regenerate from first mismatch
 *
 * Expected speedup: 2-3× for typical workloads
 */

#define XAIOS_SPEC_MAX_DRAFT_TOKENS 8U    /* Max tokens to speculate */
#define XAIOS_SPEC_MIN_ACCEPT_RATE 50U    /* Minimum 50% acceptance */

/* Speculative decoding state */
typedef struct xaios_speculative_state {
  uint32_t draft_cell_id;      /* Cell running draft model */
  uint32_t target_cell_id;     /* Cell running target model */
  
  /* Candidate tokens from draft model */
  uint32_t draft_tokens[XAIOS_SPEC_MAX_DRAFT_TOKENS];
  uint32_t num_draft_tokens;
  
  /* Verified tokens from target model */
  uint32_t verified_tokens[XAIOS_SPEC_MAX_DRAFT_TOKENS];
  uint32_t num_verified_tokens;
  uint32_t num_accepted_tokens;
  
  /* Statistics */
  uint64_t total_speculations;
  uint64_t total_accepted;
  uint64_t total_rejected;
  uint64_t avg_acceptance_rate;  /* Scaled by 100 (e.g., 75 = 75%) */
  uint64_t speedup_factor;       /* Scaled by 100 (e.g., 250 = 2.5×) */
} xaios_speculative_state_t;

/* Initialize speculative decoding */
xaios_status_t speculative_decoding_init(xaios_speculative_state_t *state,
                                         uint32_t draft_cell_id,
                                         uint32_t target_cell_id);

/* Generate candidate tokens with draft model */
xaios_status_t speculative_generate_draft(
    xaios_speculative_state_t *state,
    const uint8_t *context,
    uint32_t context_length,
    uint32_t num_draft_tokens);

/* Verify draft tokens with target model */
xaios_status_t speculative_verify_tokens(
    xaios_speculative_state_t *state,
    const uint8_t *context,
    uint32_t context_length);

/* Execute full speculative decoding step */
xaios_status_t speculative_decode_step(
    xaios_speculative_state_t *state,
    const uint8_t *context,
    uint32_t context_length,
    uint32_t *output_tokens,
    uint32_t *num_output_tokens);

/* Get speculative decoding statistics */
void speculative_decoding_get_stats(
    const xaios_speculative_state_t *state,
    uint64_t *acceptance_rate,
    uint64_t *speedup_factor,
    uint64_t *total_speculations,
    uint64_t *total_accepted);

/* Reset speculative state */
void speculative_decoding_reset(xaios_speculative_state_t *state);

#endif
