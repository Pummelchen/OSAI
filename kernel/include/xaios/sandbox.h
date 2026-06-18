#ifndef XAIOS_SANDBOX_H
#define XAIOS_SANDBOX_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_SANDBOX_VM_MAX_INSTRUCTIONS 4096U
#define XAIOS_SANDBOX_VM_REGISTERS 8U

#define XAIOS_SANDBOX_VM_OP_NOP 0U
#define XAIOS_SANDBOX_VM_OP_LOAD_IMM 1U
#define XAIOS_SANDBOX_VM_OP_ADD 2U
#define XAIOS_SANDBOX_VM_OP_MUL 3U
#define XAIOS_SANDBOX_VM_OP_EMIT 4U
#define XAIOS_SANDBOX_VM_OP_CMP 5U
#define XAIOS_SANDBOX_VM_OP_JNZ 6U
#define XAIOS_SANDBOX_VM_OP_HALT 7U
#define XAIOS_SANDBOX_VM_OP_EMIT_STR 8U

typedef struct xaios_sandbox_vm_insn {
  uint8_t op;
  uint8_t dst;
  uint8_t src1;
  uint8_t src2;
  uint32_t imm;
} xaios_sandbox_vm_insn_t;

typedef enum xaios_sandbox_state {
  XAIOS_SANDBOX_EMPTY = 0,
  XAIOS_SANDBOX_CREATED = 1,
  XAIOS_SANDBOX_BUILDING = 2,
  XAIOS_SANDBOX_BUILT = 3,
  XAIOS_SANDBOX_ROLLED_BACK = 4,
  XAIOS_SANDBOX_FAILED = 5,
} xaios_sandbox_state_t;

typedef struct xaios_sandbox_manifest {
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
} xaios_sandbox_manifest_t;

void sandbox_runtime_init(void);
xaios_status_t sandbox_create(const xaios_sandbox_manifest_t *manifest);
xaios_status_t sandbox_start_build(uint32_t sandbox_id);
xaios_status_t sandbox_start_build_as(uint32_t actor_cell_id,
                                     uint32_t sandbox_id);
xaios_status_t sandbox_finish_build(uint32_t sandbox_id,
                                   const char *artifact_revision);
xaios_status_t sandbox_rollback(uint32_t sandbox_id);
xaios_status_t sandbox_rollback_as(uint32_t actor_cell_id, uint32_t sandbox_id);
uint64_t sandbox_transition_count(void);
uint64_t sandbox_active_count(void);
uint64_t sandbox_vm_exec_count(void);
xaios_status_t sandbox_execute_build(uint32_t sandbox_id,
                                    const xaios_sandbox_vm_insn_t *program,
                                    uint32_t insn_count,
                                    uint64_t *output_bytes);
void sandbox_self_test(void);

#endif
