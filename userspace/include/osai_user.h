#ifndef OSAI_USERSPACE_OSAI_USER_H
#define OSAI_USERSPACE_OSAI_USER_H

typedef unsigned long long u64;
typedef unsigned int u32;
typedef int s32;

#define OSAI_SYSCALL_LOG 1ULL
#define OSAI_SYSCALL_EXIT 2ULL
#define OSAI_SYSCALL_OSCTL 3ULL
#define OSAI_SYSCALL_FS_OPEN 11ULL
#define OSAI_SYSCALL_FS_READ 12ULL
#define OSAI_SYSCALL_FS_WRITE 13ULL
#define OSAI_SYSCALL_FS_CLOSE 14ULL
#define OSAI_SYSCALL_FS_STAT 15ULL
#define OSAI_SYSCALL_FS_MKDIR 16ULL
#define OSAI_SYSCALL_FS_DELETE 17ULL
#define OSAI_SYSCALL_FS_RENAME 18ULL
#define OSAI_SYSCALL_FS_LIST 19ULL
#define OSAI_SYSCALL_CLOCK_NANOS 20ULL

#define OSAI_MFS_OPEN_READ 1U
#define OSAI_MFS_OPEN_WRITE 2U
#define OSAI_MFS_OPEN_CREATE 4U
#define OSAI_MFS_OPEN_TRUNCATE 8U

typedef struct osai_mfs_stat_user {
  u32 type;
  u32 block_count;
  u64 size;
  u64 generation;
  u64 content_hash;
} osai_mfs_stat_user_t;

typedef struct osai_rename_request {
  u64 old_path;
  u64 old_path_len;
  u64 new_path;
  u64 new_path_len;
} osai_rename_request_t;

typedef struct osai_list_request {
  u64 buffer;
  u64 buffer_size;
  u64 out_size;
} osai_list_request_t;

u64 osai_syscall3(u64 number, u64 arg0, u64 arg1, u64 arg2);
u64 osai_strlen(const char *text);
void osai_log(const char *text);
void osai_log_u64(const char *prefix, u64 value, const char *suffix);
void osai_exit(int code);
u64 osai_clock_nanos(void);
int osai_osctl(const char *command);
int osai_fs_mkdir(const char *path);
int osai_fs_delete(const char *path);
int osai_fs_rename(const char *old_path, const char *new_path);
int osai_fs_list(const char *path, char *buffer, u64 buffer_size, u64 *out_size);
int osai_fs_open(const char *path, u32 flags);
int osai_fs_read(int fd, void *buffer, u64 size);
int osai_fs_write(int fd, const void *buffer, u64 size);
int osai_fs_close(int fd);
int osai_fs_stat(const char *path, osai_mfs_stat_user_t *stat);
int osai_write_file(const char *path, const char *content);
int osai_read_file(const char *path, char *buffer, u64 buffer_size);
void osai_memzero(void *buffer, u64 size);
void osai_append_u64(char *buffer, u64 capacity, u64 *offset, u64 value);
void osai_append_cstr(char *buffer, u64 capacity, u64 *offset, const char *text);

#endif
