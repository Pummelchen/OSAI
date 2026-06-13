#ifndef OSAI_CPU_AI_RUNTIME_H
#define OSAI_CPU_AI_RUNTIME_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_CPU_AI_RUNTIME_MAX_CELLS 4U

void cpu_ai_runtime_init(void);
osai_status_t cpu_ai_runtime_bind_model(uint32_t cell_id, uint32_t model_arena_id);
osai_status_t cpu_ai_runtime_bind_model_with_kv(uint32_t cell_id,
                                                uint32_t model_arena_id,
                                                uint64_t kv_base,
                                                uint64_t kv_bytes);
osai_status_t cpu_ai_runtime_unbind_model(uint32_t cell_id);
osai_status_t cpu_ai_runtime_decode_piece(uint32_t cell_id, const uint8_t *piece,
                                         uint64_t piece_bytes, char *output,
                                         uint64_t output_capacity,
                                         uint64_t *output_bytes);
uint64_t cpu_ai_runtime_decode_count(uint32_t cell_id);
uint64_t cpu_ai_runtime_model_load_count(void);
uint64_t cpu_ai_runtime_model_load_failure_count(void);
uint64_t cpu_ai_runtime_tokenizer_call_count(void);
uint64_t cpu_ai_runtime_runtime_call_count(void);
uint64_t cpu_ai_runtime_kv_write_count(void);
uint64_t cpu_ai_runtime_shared_weight_bind_count(void);
uint64_t cpu_ai_runtime_gpu_reject_count(void);
void cpu_ai_runtime_self_test(void);

#endif
