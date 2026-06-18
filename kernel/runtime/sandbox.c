#include <xaios/arena.h>
#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/sandbox.h>
#include <xaios/security.h>

#define MAX_SANDBOXES 4U
#define SANDBOX_BUILD_ARENA_BASE 25U
#define SANDBOX_MAX_ARTIFACT_BYTES UINT64_C(262144)

typedef struct xaios_sandbox {
  xaios_sandbox_state_t state;
  xaios_sandbox_manifest_t manifest;
  uint32_t build_arena_id;
  uint64_t build_arena_base;
  uint64_t build_generation;
  uint64_t rollback_generation;
  const char *last_good_revision;
  const char *current_revision;
} xaios_sandbox_t;

static xaios_sandbox_t g_sandboxes[MAX_SANDBOXES];
static uint8_t g_workspace_owner[MAX_SANDBOXES + 1U];
static uint64_t g_transition_count;
static uint64_t g_active_count;
static uint64_t g_vm_exec_count;

static int str_nonempty(const char *value) {
  return value != 0 && value[0] != '\0';
}

static int str_prefix(const char *value, const char *prefix) {
  if (!str_nonempty(value) || !str_nonempty(prefix)) {
    return 0;
  }
  while (*prefix != '\0') {
    if (*value != *prefix) {
      return 0;
    }
    ++value;
    ++prefix;
  }
  return 1;
}

static xaios_status_t validate_manifest(
    const xaios_sandbox_manifest_t *manifest) {
  if (manifest == 0 || manifest->sandbox_id >= MAX_SANDBOXES ||
      manifest->cell_id >= MAX_SANDBOXES || manifest->workspace_id == 0 ||
      manifest->workspace_id > MAX_SANDBOXES ||
      !str_prefix(manifest->repo_path, "/repo/") ||
      !str_prefix(manifest->worktree_path, "/workspace/") ||
      !str_prefix(manifest->build_dir, "/workspace/") ||
      !str_prefix(manifest->artifact_dir, "/artifacts/") ||
      !str_nonempty(manifest->source_revision) ||
      manifest->max_artifact_bytes == 0 ||
      manifest->max_artifact_bytes > SANDBOX_MAX_ARTIFACT_BYTES ||
      manifest->allow_network != 0) {
    return XAIOS_ERR_INVALID;
  }
  if (security_validate_sandbox_path(manifest->repo_path) != XAIOS_OK ||
      security_validate_sandbox_path(manifest->worktree_path) != XAIOS_OK ||
      security_validate_sandbox_path(manifest->build_dir) != XAIOS_OK ||
      security_validate_sandbox_path(manifest->artifact_dir) != XAIOS_OK) {
    return XAIOS_ERR_INVALID;
  }
  return XAIOS_OK;
}

static void copy_manifest(xaios_sandbox_manifest_t *dst,
                          const xaios_sandbox_manifest_t *src) {
  dst->sandbox_id = src->sandbox_id;
  dst->cell_id = src->cell_id;
  dst->workspace_id = src->workspace_id;
  dst->repo_path = src->repo_path;
  dst->worktree_path = src->worktree_path;
  dst->build_dir = src->build_dir;
  dst->artifact_dir = src->artifact_dir;
  dst->source_revision = src->source_revision;
  dst->max_artifact_bytes = src->max_artifact_bytes;
  dst->allow_network = src->allow_network;
}

static void fill_manifest(xaios_sandbox_manifest_t *manifest,
                          uint32_t sandbox_id, uint32_t workspace_id,
                          const char *repo_path, uint32_t allow_network) {
  manifest->sandbox_id = sandbox_id;
  manifest->cell_id = 0;
  manifest->workspace_id = workspace_id;
  manifest->repo_path = repo_path;
  manifest->worktree_path = "/workspace/1/app";
  manifest->build_dir = "/workspace/1/build";
  manifest->artifact_dir = "/artifacts/1";
  manifest->source_revision = "rev-good";
  manifest->max_artifact_bytes = 64 * 1024;
  manifest->allow_network = allow_network;
}

void sandbox_runtime_init(void) {
  for (uint32_t i = 0; i < MAX_SANDBOXES; ++i) {
    g_sandboxes[i].state = XAIOS_SANDBOX_EMPTY;
    g_sandboxes[i].build_arena_id = 0;
    g_sandboxes[i].build_arena_base = 0;
    g_sandboxes[i].build_generation = 0;
    g_sandboxes[i].rollback_generation = 0;
    g_sandboxes[i].last_good_revision = 0;
    g_sandboxes[i].current_revision = 0;
  }
  for (uint32_t i = 0; i <= MAX_SANDBOXES; ++i) {
    g_workspace_owner[i] = 0;
  }
  g_transition_count = 0;
  g_active_count = 0;
  g_vm_exec_count = 0;
  klog("sandbox: runtime initialized\n");
}

xaios_status_t sandbox_create(const xaios_sandbox_manifest_t *manifest) {
  if (validate_manifest(manifest) != XAIOS_OK) {
    return XAIOS_ERR_INVALID;
  }

  xaios_sandbox_t *sandbox = &g_sandboxes[manifest->sandbox_id];
  if (sandbox->state != XAIOS_SANDBOX_EMPTY ||
      g_workspace_owner[manifest->workspace_id] != 0) {
    return XAIOS_ERR_BUSY;
  }

  uint32_t arena_id = SANDBOX_BUILD_ARENA_BASE + manifest->sandbox_id;
  const xaios_arena_t *arena = 0;
  if (arena_create(arena_id, XAIOS_ARENA_BUILD_OUTPUT, manifest->sandbox_id,
                   "sandbox-build", manifest->max_artifact_bytes, 0,
                   &arena) != XAIOS_OK) {
    return XAIOS_ERR_NO_MEMORY;
  }

  copy_manifest(&sandbox->manifest, manifest);
  sandbox->state = XAIOS_SANDBOX_CREATED;
  sandbox->build_arena_id = arena_id;
  sandbox->build_arena_base = arena->base;
  sandbox->build_generation = 0;
  sandbox->rollback_generation = 0;
  sandbox->last_good_revision = manifest->source_revision;
  sandbox->current_revision = manifest->source_revision;
  g_workspace_owner[manifest->workspace_id] =
      (uint8_t)(manifest->sandbox_id + 1U);
  ++g_transition_count;
  ++g_active_count;
  klog("sandbox: %u created cell=%u workspace=%u repo=%s worktree=%s build=%s artifacts=%s arena=0x%lx limit=%lu\n",
       manifest->sandbox_id, manifest->cell_id, manifest->workspace_id,
       manifest->repo_path, manifest->worktree_path, manifest->build_dir,
       manifest->artifact_dir, sandbox->build_arena_base,
       manifest->max_artifact_bytes);
  return XAIOS_OK;
}

xaios_status_t sandbox_start_build(uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES) {
    return XAIOS_ERR_INVALID;
  }
  return sandbox_start_build_as(g_sandboxes[sandbox_id].manifest.cell_id,
                                sandbox_id);
}

xaios_status_t sandbox_start_build_as(uint32_t actor_cell_id,
                                     uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != XAIOS_SANDBOX_CREATED) {
    return XAIOS_ERR_INVALID;
  }
  if (security_authorize_sandbox(sandbox_id,
                                 g_sandboxes[sandbox_id].manifest.cell_id,
                                 actor_cell_id, "build") != XAIOS_OK) {
    return XAIOS_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = XAIOS_SANDBOX_BUILDING;
  ++g_sandboxes[sandbox_id].build_generation;
  ++g_transition_count;
  klog("sandbox: %u build-start generation=%lu source=%s\n", sandbox_id,
       g_sandboxes[sandbox_id].build_generation,
       g_sandboxes[sandbox_id].current_revision);
  return XAIOS_OK;
}

xaios_status_t sandbox_finish_build(uint32_t sandbox_id,
                                   const char *artifact_revision) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != XAIOS_SANDBOX_BUILDING ||
      !str_nonempty(artifact_revision)) {
    return XAIOS_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = XAIOS_SANDBOX_BUILT;
  g_sandboxes[sandbox_id].current_revision = artifact_revision;
  ++g_transition_count;
  klog("sandbox: %u build-finished generation=%lu artifact=%s arena_id=%u\n",
       sandbox_id, g_sandboxes[sandbox_id].build_generation,
       artifact_revision, g_sandboxes[sandbox_id].build_arena_id);
  return XAIOS_OK;
}

xaios_status_t sandbox_rollback(uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES) {
    return XAIOS_ERR_INVALID;
  }
  return sandbox_rollback_as(g_sandboxes[sandbox_id].manifest.cell_id,
                             sandbox_id);
}

xaios_status_t sandbox_rollback_as(uint32_t actor_cell_id,
                                  uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != XAIOS_SANDBOX_BUILT) {
    return XAIOS_ERR_INVALID;
  }
  if (security_authorize_sandbox(sandbox_id,
                                 g_sandboxes[sandbox_id].manifest.cell_id,
                                 actor_cell_id, "rollback") != XAIOS_OK ||
      security_authorize_rollback("sandbox", actor_cell_id ==
                                                 g_sandboxes[sandbox_id]
                                                     .manifest.cell_id) !=
          XAIOS_OK) {
    return XAIOS_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = XAIOS_SANDBOX_ROLLED_BACK;
  g_sandboxes[sandbox_id].current_revision =
      g_sandboxes[sandbox_id].last_good_revision;
  ++g_sandboxes[sandbox_id].rollback_generation;
  ++g_transition_count;
  klog("sandbox: %u rolled-back rollback_generation=%lu current=%s\n",
       sandbox_id, g_sandboxes[sandbox_id].rollback_generation,
       g_sandboxes[sandbox_id].current_revision);
  return XAIOS_OK;
}

uint64_t sandbox_transition_count(void) {
  return g_transition_count;
}

uint64_t sandbox_active_count(void) {
  return g_active_count;
}

uint64_t sandbox_vm_exec_count(void) {
  return g_vm_exec_count;
}

xaios_status_t sandbox_execute_build(uint32_t sandbox_id,
                                    const xaios_sandbox_vm_insn_t *program,
                                    uint32_t insn_count,
                                    uint64_t *output_bytes) {
  if (sandbox_id >= MAX_SANDBOXES || program == 0 || insn_count == 0 ||
      insn_count > XAIOS_SANDBOX_VM_MAX_INSTRUCTIONS || output_bytes == 0) {
    return XAIOS_ERR_INVALID;
  }
  xaios_sandbox_t *sb = &g_sandboxes[sandbox_id];
  if (sb->state != XAIOS_SANDBOX_BUILDING) {
    return XAIOS_ERR_INVALID;
  }

  int64_t regs[XAIOS_SANDBOX_VM_REGISTERS];
  for (uint32_t i = 0; i < XAIOS_SANDBOX_VM_REGISTERS; ++i) {
    regs[i] = 0;
  }

  uint8_t *arena_out = 0;
  uint64_t arena_cap = 0;
  if (sb->build_arena_base != 0) {
    arena_out = (uint8_t *)(uintptr_t)sb->build_arena_base;
    arena_cap = sb->manifest.max_artifact_bytes;
  }
  uint64_t out_pos = 0;
  uint32_t pc = 0;
  uint32_t cycles = 0;
  const uint32_t max_cycles = insn_count * 64U;

  while (pc < insn_count && cycles < max_cycles) {
    const xaios_sandbox_vm_insn_t *insn = &program[pc];
    uint8_t dst = insn->dst % XAIOS_SANDBOX_VM_REGISTERS;
    uint8_t s1 = insn->src1 % XAIOS_SANDBOX_VM_REGISTERS;
    uint8_t s2 = insn->src2 % XAIOS_SANDBOX_VM_REGISTERS;
    ++cycles;

    switch (insn->op) {
      case XAIOS_SANDBOX_VM_OP_NOP:
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_LOAD_IMM:
        regs[dst] = (int64_t)(int32_t)insn->imm;
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_ADD:
        regs[dst] = regs[s1] + regs[s2];
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_MUL:
        regs[dst] = regs[s1] * regs[s2];
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_EMIT:
        if (arena_out != 0 && out_pos < arena_cap) {
          arena_out[out_pos++] = (uint8_t)(regs[dst] & 0xFF);
        }
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_CMP:
        regs[dst] = (regs[s1] == regs[s2]) ? 0 : 1;
        ++pc;
        break;
      case XAIOS_SANDBOX_VM_OP_JNZ:
        if (regs[dst] != 0) {
          pc = insn->imm;
        } else {
          ++pc;
        }
        break;
      case XAIOS_SANDBOX_VM_OP_HALT:
        pc = insn_count;
        break;
      case XAIOS_SANDBOX_VM_OP_EMIT_STR: {
        uint32_t remaining = insn->imm;
        uint32_t ri = 0;
        while (ri < remaining && arena_out != 0 && out_pos < arena_cap) {
          /* emit low bytes of registers in sequence */
          uint8_t reg_idx = (uint8_t)((insn->src1 + ri) %
                              XAIOS_SANDBOX_VM_REGISTERS);
          arena_out[out_pos++] = (uint8_t)(regs[reg_idx] & 0xFF);
          ++ri;
        }
        ++pc;
        break;
      }
      default:
        ++g_vm_exec_count;
        *output_bytes = out_pos;
        return XAIOS_ERR_INVALID;
    }
  }

  ++g_vm_exec_count;
  *output_bytes = out_pos;
  klog("sandbox: %u vm-build executed cycles=%u output=%lu\n",
       sandbox_id, cycles, out_pos);
  return XAIOS_OK;
}

void sandbox_self_test(void) {
  sandbox_runtime_init();

  xaios_sandbox_manifest_t manifest;
  fill_manifest(&manifest, 0, 1, "/repo/app", 0);

  kassert(sandbox_create(&manifest) == XAIOS_OK);
  kassert(sandbox_create(&manifest) == XAIOS_ERR_BUSY);
  kassert(sandbox_start_build_as(1, 0) == XAIOS_ERR_INVALID);
  kassert(sandbox_start_build(0) == XAIOS_OK);
  kassert(sandbox_finish_build(0, "rev-candidate") == XAIOS_OK);
  kassert(sandbox_rollback_as(1, 0) == XAIOS_ERR_INVALID);
  kassert(sandbox_rollback(0) == XAIOS_OK);

  xaios_sandbox_manifest_t invalid;
  fill_manifest(&invalid, 1, 2, "repo/app", 0);
  kassert(sandbox_create(&invalid) == XAIOS_ERR_INVALID);

  fill_manifest(&invalid, 1, 2, "/repo/app", 1);
  kassert(sandbox_create(&invalid) == XAIOS_ERR_INVALID);

  fill_manifest(&invalid, 1, 2, "/repo/app", 0);
  invalid.worktree_path = "/workspace/1/../escape";
  kassert(sandbox_create(&invalid) == XAIOS_ERR_INVALID);

  klog("sandbox: lifecycle self-test passed active=%lu transitions=%lu\n",
       sandbox_active_count(), sandbox_transition_count());

  /* bytecode VM test: emit "OK" (0x4F, 0x4B) */
  {
    xaios_sandbox_manifest_t vm_manifest;
    fill_manifest(&vm_manifest, 1, 2, "/repo/vm-test", 0);
    kassert(sandbox_create(&vm_manifest) == XAIOS_OK);
    kassert(sandbox_start_build(1) == XAIOS_OK);

    xaios_sandbox_vm_insn_t prog[5];
    prog[0].op = XAIOS_SANDBOX_VM_OP_LOAD_IMM;
    prog[0].dst = 0; prog[0].src1 = 0; prog[0].src2 = 0;
    prog[0].imm = 0x4F;
    prog[1].op = XAIOS_SANDBOX_VM_OP_EMIT;
    prog[1].dst = 0; prog[1].src1 = 0; prog[1].src2 = 0;
    prog[1].imm = 0;
    prog[2].op = XAIOS_SANDBOX_VM_OP_LOAD_IMM;
    prog[2].dst = 0; prog[2].src1 = 0; prog[2].src2 = 0;
    prog[2].imm = 0x4B;
    prog[3].op = XAIOS_SANDBOX_VM_OP_EMIT;
    prog[3].dst = 0; prog[3].src1 = 0; prog[3].src2 = 0;
    prog[3].imm = 0;
    prog[4].op = XAIOS_SANDBOX_VM_OP_HALT;
    prog[4].dst = 0; prog[4].src1 = 0; prog[4].src2 = 0;
    prog[4].imm = 0;

    uint64_t vm_out = 0;
    kassert(sandbox_execute_build(1, prog, 5, &vm_out) == XAIOS_OK);
    kassert(vm_out == 2);
    uint8_t *arena = (uint8_t *)(uintptr_t)g_sandboxes[1].build_arena_base;
    kassert(arena[0] == 0x4F && arena[1] == 0x4B);
    kassert(sandbox_finish_build(1, "vm-build-ok") == XAIOS_OK);
    kassert(sandbox_vm_exec_count() == 1);
  }

  klog("sandbox: VM build self-test passed execs=%lu\n",
       sandbox_vm_exec_count());
}
