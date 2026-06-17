#ifndef OSAI_SANDBOX_H
#define OSAI_SANDBOX_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_SANDBOX_VM_MAX_INSTRUCTIONS 4096U
#define OSAI_SANDBOX_VM_REGISTERS 8U

#define OSAI_SANDBOX_VM_OP_NOP 0U
#define OSAI_SANDBOX_VM_OP_LOAD_IMM 1U
#define OSAI_SANDBOX_VM_OP_ADD 2U
#define OSAI_SANDBOX_VM_OP_MUL 3U
#define OSAI_SANDBOX_VM_OP_EMIT 4U
#define OSAI_SANDBOX_VM_OP_CMP 5U
#define OSAI_SANDBOX_VM_OP_JNZ 6U
#define OSAI_SANDBOX_VM_OP_HALT 7U
#define OSAI_SANDBOX_VM_OP_EMIT_STR 8U

typedef struct osai_sandbox_vm_insn {
  uint8_t op;
  uint8_t dst;
  uint8_t src1;
  uint8_t src2;
  uint32_t imm;
} osai_sandbox_vm_insn_t;

typedef enum osai_sandbox_state {
  OSAI_SANDBOX_EMPTY = 0,
  OSAI_SANDBOX_CREATED = 1,
  OSAI_SANDBOX_BUILDING = 2,
  OSAI_SANDBOX_BUILT = 3,
  OSAI_SANDBOX_ROLLED_BACK = 4,
  OSAI_SANDBOX_FAILED = 5,
} osai_sandbox_state_t;

typedef struct osai_sandbox_manifest {
  uint32_t sandbox_id;
  uint32_t cell_id;
  uint32_t workspace_id;
  const char *repo_path;
  const char *worktree_path;
  const char *build_dir;
  const char *artifact_dir;
  const char *source_revision;
  uint64_t max_artifact_bytes;
  uint32_t allow_network;
} osai_sandbox_manifest_t;

void sandbox_runtime_init(void);
osai_status_t sandbox_create(const osai_sandbox_manifest_t *manifest);
osai_status_t sandbox_start_build(uint32_t sandbox_id);
osai_status_t sandbox_start_build_as(uint32_t actor_cell_id,
                                     uint32_t sandbox_id);
osai_status_t sandbox_finish_build(uint32_t sandbox_id,
                                   const char *artifact_revision);
osai_status_t sandbox_rollback(uint32_t sandbox_id);
osai_status_t sandbox_rollback_as(uint32_t actor_cell_id, uint32_t sandbox_id);
uint64_t sandbox_transition_count(void);
uint64_t sandbox_active_count(void);
uint64_t sandbox_vm_exec_count(void);
osai_status_t sandbox_execute_build(uint32_t sandbox_id,
                                    const osai_sandbox_vm_insn_t *program,
                                    uint32_t insn_count,
                                    uint64_t *output_bytes);
void sandbox_self_test(void);

#endif
