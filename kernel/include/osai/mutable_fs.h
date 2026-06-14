#ifndef OSAI_MUTABLE_FS_H
#define OSAI_MUTABLE_FS_H

#include <osai/status.h>
#include <osai/types.h>

void mutable_fs_self_test(void);

uint64_t mutable_fs_mount_count(void);
uint64_t mutable_fs_format_count(void);
uint64_t mutable_fs_boot_load_count(void);
uint64_t mutable_fs_file_count(void);
uint64_t mutable_fs_directory_count(void);
uint64_t mutable_fs_write_count(void);
uint64_t mutable_fs_read_count(void);
uint64_t mutable_fs_delete_count(void);
uint64_t mutable_fs_commit_count(void);
uint64_t mutable_fs_rollback_count(void);
uint64_t mutable_fs_reject_count(void);
uint64_t mutable_fs_checksum_error_count(void);
uint64_t mutable_fs_allocation_count(void);
uint64_t mutable_fs_free_count(void);
uint64_t mutable_fs_replay_count(void);
uint64_t mutable_fs_journal_write_count(void);
uint64_t mutable_fs_multi_sector_file_count(void);
uint64_t mutable_fs_state_record_count(void);

osai_status_t mutable_fs_record_service_state(const char *name,
                                              const char *state);
osai_status_t mutable_fs_record_workspace_state(uint32_t workspace_id,
                                                const char *revision);
osai_status_t mutable_fs_record_update_state(const char *policy);
osai_status_t mutable_fs_record_update_transaction(uint32_t generation,
                                                   const char *state,
                                                   const char *target,
                                                   const char *rollback_label);
osai_status_t mutable_fs_record_admin_status(const char *service,
                                             const char *state,
                                             uint32_t starts,
                                             uint32_t restarts,
                                             uint32_t logs);
osai_status_t mutable_fs_commit(const char *label);
osai_status_t mutable_fs_rollback(void);

#endif
