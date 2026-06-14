#ifndef OSAI_MUTABLE_FS_H
#define OSAI_MUTABLE_FS_H

#include <osai/status.h>
#include <osai/types.h>

void mutable_fs_self_test(void);

uint64_t mutable_fs_mount_count(void);
uint64_t mutable_fs_format_count(void);
uint64_t mutable_fs_boot_load_count(void);
uint64_t mutable_fs_file_count(void);
uint64_t mutable_fs_write_count(void);
uint64_t mutable_fs_read_count(void);
uint64_t mutable_fs_delete_count(void);
uint64_t mutable_fs_commit_count(void);
uint64_t mutable_fs_rollback_count(void);
uint64_t mutable_fs_reject_count(void);
uint64_t mutable_fs_checksum_error_count(void);

#endif
