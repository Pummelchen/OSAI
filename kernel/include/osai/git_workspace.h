#ifndef OSAI_GIT_WORKSPACE_H
#define OSAI_GIT_WORKSPACE_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_GIT_WORKSPACE_BRANCH_MAX 24U
#define OSAI_GIT_WORKSPACE_REVISION_MAX 32U
#define OSAI_GIT_WORKSPACE_PATCH_ID_MAX 32U

typedef enum osai_git_workspace_state {
  OSAI_GIT_WORKSPACE_EMPTY = 0,
  OSAI_GIT_WORKSPACE_READY = 1,
  OSAI_GIT_WORKSPACE_SYNCING = 2,
  OSAI_GIT_WORKSPACE_APPLYING_PATCH = 3,
  OSAI_GIT_WORKSPACE_REVERTING_PATCH = 4,
  OSAI_GIT_WORKSPACE_FAILED = 5,
} osai_git_workspace_state_t;

typedef struct osai_git_workspace_manifest {
  uint32_t workspace_id;
  uint32_t owner_cell_id;
  const char *repo_path;
  const char *branch;
  const char *starting_revision;
  uint64_t patch_buffer_bytes;
} osai_git_workspace_manifest_t;

void git_workspace_runtime_init(void);
osai_status_t git_workspace_create(const osai_git_workspace_manifest_t *manifest);
osai_status_t git_workspace_sync_start(uint32_t workspace_id,
                                      const char *revision);
osai_status_t git_workspace_sync_finish(uint32_t workspace_id);
osai_status_t git_workspace_patch_apply_start(uint32_t workspace_id,
                                             const char *patch_id,
                                             uint64_t patch_bytes);
osai_status_t git_workspace_patch_apply_finish(uint32_t workspace_id);
osai_status_t git_workspace_patch_revert_start(uint32_t workspace_id);
osai_status_t git_workspace_patch_revert_finish(uint32_t workspace_id);
uint64_t git_workspace_active_count(void);
uint64_t git_workspace_sync_count(void);
uint64_t git_workspace_apply_count(void);
uint64_t git_workspace_revert_count(void);
uint64_t git_workspace_conflict_count(void);
void git_workspace_self_test(void);

#endif

