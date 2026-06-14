#include <osai/arena.h>
#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/sandbox.h>
#include <osai/security.h>

#define MAX_SANDBOXES 4U
#define SANDBOX_BUILD_ARENA_BASE 25U
#define SANDBOX_MAX_ARTIFACT_BYTES UINT64_C(262144)

typedef struct osai_sandbox {
  osai_sandbox_state_t state;
  osai_sandbox_manifest_t manifest;
  uint32_t build_arena_id;
  uint64_t build_arena_base;
  uint64_t build_generation;
  uint64_t rollback_generation;
  const char *last_good_revision;
  const char *current_revision;
} osai_sandbox_t;

static osai_sandbox_t g_sandboxes[MAX_SANDBOXES];
static uint8_t g_workspace_owner[MAX_SANDBOXES + 1U];
static uint64_t g_transition_count;
static uint64_t g_active_count;

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

static osai_status_t validate_manifest(
    const osai_sandbox_manifest_t *manifest) {
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
    return OSAI_ERR_INVALID;
  }
  if (security_validate_sandbox_path(manifest->repo_path) != OSAI_OK ||
      security_validate_sandbox_path(manifest->worktree_path) != OSAI_OK ||
      security_validate_sandbox_path(manifest->build_dir) != OSAI_OK ||
      security_validate_sandbox_path(manifest->artifact_dir) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  return OSAI_OK;
}

static void copy_manifest(osai_sandbox_manifest_t *dst,
                          const osai_sandbox_manifest_t *src) {
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

static void fill_manifest(osai_sandbox_manifest_t *manifest,
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
    g_sandboxes[i].state = OSAI_SANDBOX_EMPTY;
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
  klog("sandbox: runtime initialized\n");
}

osai_status_t sandbox_create(const osai_sandbox_manifest_t *manifest) {
  if (validate_manifest(manifest) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  osai_sandbox_t *sandbox = &g_sandboxes[manifest->sandbox_id];
  if (sandbox->state != OSAI_SANDBOX_EMPTY ||
      g_workspace_owner[manifest->workspace_id] != 0) {
    return OSAI_ERR_BUSY;
  }

  uint32_t arena_id = SANDBOX_BUILD_ARENA_BASE + manifest->sandbox_id;
  const osai_arena_t *arena = 0;
  if (arena_create(arena_id, OSAI_ARENA_BUILD_OUTPUT, manifest->sandbox_id,
                   "sandbox-build", manifest->max_artifact_bytes, 0,
                   &arena) != OSAI_OK) {
    return OSAI_ERR_NO_MEMORY;
  }

  copy_manifest(&sandbox->manifest, manifest);
  sandbox->state = OSAI_SANDBOX_CREATED;
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
  return OSAI_OK;
}

osai_status_t sandbox_start_build(uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES) {
    return OSAI_ERR_INVALID;
  }
  return sandbox_start_build_as(g_sandboxes[sandbox_id].manifest.cell_id,
                                sandbox_id);
}

osai_status_t sandbox_start_build_as(uint32_t actor_cell_id,
                                     uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != OSAI_SANDBOX_CREATED) {
    return OSAI_ERR_INVALID;
  }
  if (security_authorize_sandbox(sandbox_id,
                                 g_sandboxes[sandbox_id].manifest.cell_id,
                                 actor_cell_id, "build") != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = OSAI_SANDBOX_BUILDING;
  ++g_sandboxes[sandbox_id].build_generation;
  ++g_transition_count;
  klog("sandbox: %u build-start generation=%lu source=%s\n", sandbox_id,
       g_sandboxes[sandbox_id].build_generation,
       g_sandboxes[sandbox_id].current_revision);
  return OSAI_OK;
}

osai_status_t sandbox_finish_build(uint32_t sandbox_id,
                                   const char *artifact_revision) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != OSAI_SANDBOX_BUILDING ||
      !str_nonempty(artifact_revision)) {
    return OSAI_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = OSAI_SANDBOX_BUILT;
  g_sandboxes[sandbox_id].current_revision = artifact_revision;
  ++g_transition_count;
  klog("sandbox: %u build-finished generation=%lu artifact=%s arena_id=%u\n",
       sandbox_id, g_sandboxes[sandbox_id].build_generation,
       artifact_revision, g_sandboxes[sandbox_id].build_arena_id);
  return OSAI_OK;
}

osai_status_t sandbox_rollback(uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES) {
    return OSAI_ERR_INVALID;
  }
  return sandbox_rollback_as(g_sandboxes[sandbox_id].manifest.cell_id,
                             sandbox_id);
}

osai_status_t sandbox_rollback_as(uint32_t actor_cell_id,
                                  uint32_t sandbox_id) {
  if (sandbox_id >= MAX_SANDBOXES ||
      g_sandboxes[sandbox_id].state != OSAI_SANDBOX_BUILT) {
    return OSAI_ERR_INVALID;
  }
  if (security_authorize_sandbox(sandbox_id,
                                 g_sandboxes[sandbox_id].manifest.cell_id,
                                 actor_cell_id, "rollback") != OSAI_OK ||
      security_authorize_rollback("sandbox", actor_cell_id ==
                                                 g_sandboxes[sandbox_id]
                                                     .manifest.cell_id) !=
          OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  g_sandboxes[sandbox_id].state = OSAI_SANDBOX_ROLLED_BACK;
  g_sandboxes[sandbox_id].current_revision =
      g_sandboxes[sandbox_id].last_good_revision;
  ++g_sandboxes[sandbox_id].rollback_generation;
  ++g_transition_count;
  klog("sandbox: %u rolled-back rollback_generation=%lu current=%s\n",
       sandbox_id, g_sandboxes[sandbox_id].rollback_generation,
       g_sandboxes[sandbox_id].current_revision);
  return OSAI_OK;
}

uint64_t sandbox_transition_count(void) {
  return g_transition_count;
}

uint64_t sandbox_active_count(void) {
  return g_active_count;
}

void sandbox_self_test(void) {
  sandbox_runtime_init();

  osai_sandbox_manifest_t manifest;
  fill_manifest(&manifest, 0, 1, "/repo/app", 0);

  kassert(sandbox_create(&manifest) == OSAI_OK);
  kassert(sandbox_create(&manifest) == OSAI_ERR_BUSY);
  kassert(sandbox_start_build_as(1, 0) == OSAI_ERR_INVALID);
  kassert(sandbox_start_build(0) == OSAI_OK);
  kassert(sandbox_finish_build(0, "rev-candidate") == OSAI_OK);
  kassert(sandbox_rollback_as(1, 0) == OSAI_ERR_INVALID);
  kassert(sandbox_rollback(0) == OSAI_OK);

  osai_sandbox_manifest_t invalid;
  fill_manifest(&invalid, 1, 2, "repo/app", 0);
  kassert(sandbox_create(&invalid) == OSAI_ERR_INVALID);

  fill_manifest(&invalid, 1, 2, "/repo/app", 1);
  kassert(sandbox_create(&invalid) == OSAI_ERR_INVALID);

  fill_manifest(&invalid, 1, 2, "/repo/app", 0);
  invalid.worktree_path = "/workspace/1/../escape";
  kassert(sandbox_create(&invalid) == OSAI_ERR_INVALID);

  klog("sandbox: lifecycle self-test passed active=%lu transitions=%lu\n",
       sandbox_active_count(), sandbox_transition_count());
}
