# OSAI Userspace API Reference

This document describes the system call interface available to OSAI userspace programs. All calls are made via `svc #0` with the syscall number in `x8` and up to three arguments in `x0`–`x2`.

## Invocation

```c
#include <osai_user.h>

u64 osai_syscall3(u64 number, u64 arg0, u64 arg1, u64 arg2);
```

All wrapper functions below are built on this primitive.

## Core Services

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_LOG` | 1 | `osai_log(text)` | Write a string to the kernel log (UART). |
| `OSAI_SYSCALL_EXIT` | 2 | `osai_exit(code)` | Terminate the current process. |
| `OSAI_SYSCALL_OSCTL` | 3 | `osai_osctl(command)` | Send a control-plane command (JSON telemetry query). |
| `OSAI_SYSCALL_CLOCK_NANOS` | 20 | `osai_clock_nanos()` | Return monotonic wall-clock nanoseconds since boot. |

## Filesystem

All filesystem operations target the mutable filesystem (`mutable_fs`). Paths are absolute, rooted at `/`.

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_FS_OPEN` | 11 | `osai_fs_open(path, flags)` | Open a file. Flags: `OSAI_MFS_OPEN_READ` (1), `OSAI_MFS_OPEN_WRITE` (2), `OSAI_MFS_OPEN_CREATE` (4), `OSAI_MFS_OPEN_TRUNCATE` (8). Returns fd >= 0 on success. |
| `OSAI_SYSCALL_FS_READ` | 12 | `osai_fs_read(fd, buf, size)` | Read up to `size` bytes from `fd` into `buf`. Returns bytes read. |
| `OSAI_SYSCALL_FS_WRITE` | 13 | `osai_fs_write(fd, buf, size)` | Write `size` bytes from `buf` to `fd`. Returns bytes written. |
| `OSAI_SYSCALL_FS_CLOSE` | 14 | `osai_fs_close(fd)` | Close an open file descriptor. |
| `OSAI_SYSCALL_FS_STAT` | 15 | `osai_fs_stat(path, stat)` | Populate `osai_mfs_stat_user_t` with file metadata. |
| `OSAI_SYSCALL_FS_MKDIR` | 16 | `osai_fs_mkdir(path)` | Create a directory. |
| `OSAI_SYSCALL_FS_DELETE` | 17 | `osai_fs_delete(path)` | Delete a file or empty directory. |
| `OSAI_SYSCALL_FS_RENAME` | 18 | `osai_fs_rename(old, new)` | Rename a file or directory. |
| `OSAI_SYSCALL_FS_LIST` | 19 | `osai_fs_list(path, buf, cap, out)` | List directory entries into `buf`. |

### Convenience wrappers

```c
int osai_write_file(const char *path, const char *content);
int osai_read_file(const char *path, char *buffer, u64 buffer_size);
```

### Stat structure

```c
typedef struct osai_mfs_stat_user {
    u32 type;           // 1 = file, 2 = directory
    u32 block_count;    // blocks allocated
    u64 size;           // file size in bytes
    u64 generation;     // modification generation counter
    u64 content_hash;   // content hash
} osai_mfs_stat_user_t;
```

## Networking

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_NET_UDP_ECHO` | 21 | `osai_net_udp_echo(payload, size, echoed)` | Echo a UDP payload (self-test). |
| `OSAI_SYSCALL_NET_TCP_CONNECT` | 22 | `osai_net_tcp_connect(trips)` | TCP handshake self-test. |
| `OSAI_SYSCALL_NET_EXTERNAL_SESSION` | 26 | `osai_net_external_session(proto, port, ...)` | Open external host session (UDP=17, TCP=6). |
| `OSAI_SYSCALL_NET_LISTEN` | 29 | `osai_net_listen(port, sockfd)` | Listen on a TCP port. Returns socket fd. |
| `OSAI_SYSCALL_NET_ACCEPT` | 30 | `osai_net_accept(sockfd, newfd)` | Accept an incoming TCP connection. |
| `OSAI_SYSCALL_NET_RECV` | 31 | `osai_net_recv(sockfd, buf, size, bytes)` | Receive data from a socket. |
| `OSAI_SYSCALL_NET_SEND` | 32 | `osai_net_send(sockfd, buf, size, bytes)` | Send data on a socket. |
| `OSAI_SYSCALL_NET_CLOSE` | 33 | `osai_net_close(sockfd)` | Close a socket. |

## SMP and Threads

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_SMP_RUN` | 23 | `osai_smp_run(workers, iters, ran, cksum)` | Dispatch work to secondary CPU cores. |
| `OSAI_SYSCALL_THREAD_GROUP_RUN` | 27 | `osai_thread_group_run(threads, iters, ran, cksum)` | Run a thread group on CPU 0. |

## AI / ML Runtime

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_CPU_AI_DECODE` | 24 | `osai_cpu_ai_decode(input, in_size, out, out_size, out_len)` | Run CPU-only AI inference decode. |
| `OSAI_SYSCALL_ML_RUN` | 28 | `osai_ml_run(model_kind, input, in_size, out, out_size, out_len)` | Run ML model. Kinds: `DECODE`=1, `XOR`=2, `SUM`=3, `PARITY`=4. |

## Remote Login / Shell

| Syscall | Number | Wrapper | Description |
|---------|-------:|---------|-------------|
| `OSAI_SYSCALL_REMOTE_LOGIN` | 25 | `osai_remote_login(user, cmd, out, cap, out_size)` | Execute a shell command as a user. Returns command output. |

### Supported shell commands

`pwd`, `ls` (with `-l`/`-a`), `cd`, `mkdir`, `touch`, `cat`, `cp`, `mv`, `rm`, `rmdir`, `stat`, `write`, `echo`, `grep`, `find`, `head`, `tail`, `sed`, `tar`, `cpio`, `status`, `sysinfo`, `help`, `exit`.

Pipe (`|`) and output redirection (`>`) are supported for chaining commands.

## Capabilities

Each process is launched with a capability bitmask. Syscalls are rejected if the required capability is not held.

| Capability | Bit | Grants access to |
|------------|----:|------------------|
| `OSAI_CAP_LOG` | 1 | `log` syscall |
| `OSAI_CAP_EXIT` | 2 | `exit` syscall |
| `OSAI_CAP_OSCTL` | 4 | `osctl` syscall |
| `OSAI_CAP_SERVICE_ROLLBACK` | 8 | `service_rollback` |
| `OSAI_CAP_UPDATE` | 16 | `service_update` |
| `OSAI_CAP_FS_READ` | 32 | `fs_open`, `fs_read`, `fs_close`, `fs_stat`, `fs_list`, `read_service_descriptor` |
| `OSAI_CAP_SERVICE_CONTROL` | 64 | `service_start`, `service_stop`, `service_restart`, `service_status` |
| `OSAI_CAP_ADMIN` | 128 | Administrative operations |
| `OSAI_CAP_FS_WRITE` | 256 | `fs_write`, `fs_mkdir`, `fs_delete`, `fs_rename` |
| `OSAI_CAP_TIME` | 512 | `clock_nanos` |
| `OSAI_CAP_NET` | 1024 | Network self-test syscalls |
| `OSAI_CAP_SMP` | 2048 | `smp_run` |
| `OSAI_CAP_CPU_AI` | 4096 | `cpu_ai_decode` |
| `OSAI_CAP_REMOTE_LOGIN` | 8192 | `remote_login` |
| `OSAI_CAP_THREADS` | 16384 | `thread_group_run` |
| `OSAI_CAP_ML` | 32768 | `ml_run` |
| `OSAI_CAP_NET_SOCKET` | 65536 | Socket API (`listen`, `accept`, `recv`, `send`, `close`) |

## Data Types

```c
typedef unsigned long long u64;
typedef unsigned int u32;
typedef int s32;
```

Request structures passed by pointer via syscall arguments:

- `osai_rename_request_t` — old/new path pairs for rename
- `osai_list_request_t` — buffer/size for directory listing
- `osai_net_request_t` — network payload buffer
- `osai_smp_request_t` — SMP worker parameters
- `osai_cpu_ai_decode_request_t` — AI decode input/output buffers
- `osai_remote_login_request_t` — user/command/output buffers
- `osai_net_external_session_request_t` — external session parameters
- `osai_thread_group_request_t` — thread group parameters
- `osai_ml_run_request_t` — ML model kind and I/O buffers
- `osai_socket_request_t` — socket fd, port, buffer, byte counts
