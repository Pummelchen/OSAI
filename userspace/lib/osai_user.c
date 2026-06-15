#include <osai_user.h>

u64 osai_syscall3(u64 number, u64 arg0, u64 arg1, u64 arg2) {
  register u64 x0 __asm__("x0") = arg0;
  register u64 x1 __asm__("x1") = arg1;
  register u64 x2 __asm__("x2") = arg2;
  register u64 x8 __asm__("x8") = number;
  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x1), "r"(x2), "r"(x8)
                   : "memory");
  return x0;
}

u64 osai_strlen(const char *text) {
  u64 len = 0;
  if (text == 0) {
    return 0;
  }
  while (text[len] != '\0') {
    ++len;
  }
  return len;
}

void osai_memzero(void *buffer, u64 size) {
  char *bytes = (char *)buffer;
  for (u64 i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

void *memset(void *buffer, int value, u64 size) {
  unsigned char *bytes = (unsigned char *)buffer;
  for (u64 i = 0; i < size; ++i) {
    bytes[i] = (unsigned char)value;
  }
  return buffer;
}

void *memcpy(void *dst, const void *src, u64 size) {
  unsigned char *out = (unsigned char *)dst;
  const unsigned char *in = (const unsigned char *)src;
  for (u64 i = 0; i < size; ++i) {
    out[i] = in[i];
  }
  return dst;
}

void osai_log(const char *text) {
  (void)osai_syscall3(OSAI_SYSCALL_LOG, (u64)text, osai_strlen(text), 0);
}

void osai_exit(int code) {
  (void)osai_syscall3(OSAI_SYSCALL_EXIT, (u64)(u32)code, 0, 0);
  for (;;) {
    __asm__ volatile("wfe");
  }
}

u64 osai_clock_nanos(void) {
  return osai_syscall3(OSAI_SYSCALL_CLOCK_NANOS, 0, 0, 0);
}

int osai_osctl(const char *command) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_OSCTL, (u64)command,
                         osai_strlen(command), 0);
  return rc == ~0ULL ? -1 : 0;
}

int osai_fs_mkdir(const char *path) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_MKDIR, (u64)path, osai_strlen(path), 0);
  return rc == ~0ULL ? -1 : 0;
}

int osai_fs_delete(const char *path) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_DELETE, (u64)path, osai_strlen(path), 0);
  return rc == ~0ULL ? -1 : 0;
}

int osai_fs_rename(const char *old_path, const char *new_path) {
  osai_rename_request_t request;
  request.old_path = (u64)old_path;
  request.old_path_len = osai_strlen(old_path);
  request.new_path = (u64)new_path;
  request.new_path_len = osai_strlen(new_path);
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_RENAME, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : 0;
}

int osai_fs_list(const char *path, char *buffer, u64 buffer_size,
                 u64 *out_size) {
  osai_list_request_t request;
  request.buffer = (u64)buffer;
  request.buffer_size = buffer_size;
  request.out_size = (u64)out_size;
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_LIST, (u64)path, osai_strlen(path),
                         (u64)&request);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_fs_open(const char *path, u32 flags) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_OPEN, (u64)path, osai_strlen(path),
                         (u64)flags);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_fs_read(int fd, void *buffer, u64 size) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_READ, (u64)(u32)fd, (u64)buffer, size);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_fs_write(int fd, const void *buffer, u64 size) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_WRITE, (u64)(u32)fd, (u64)buffer, size);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_fs_close(int fd) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_CLOSE, (u64)(u32)fd, 0, 0);
  return rc == ~0ULL ? -1 : 0;
}

int osai_fs_stat(const char *path, osai_mfs_stat_user_t *stat) {
  u64 rc = osai_syscall3(OSAI_SYSCALL_FS_STAT, (u64)path, osai_strlen(path),
                         (u64)stat);
  return rc == ~0ULL ? -1 : 0;
}

int osai_net_udp_echo(const void *payload, u64 payload_size,
                      u64 *echoed_bytes) {
  osai_net_request_t request;
  request.payload = (u64)payload;
  request.payload_size = payload_size;
  request.out_value = (u64)echoed_bytes;
  u64 rc = osai_syscall3(OSAI_SYSCALL_NET_UDP_ECHO, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_net_tcp_connect(u64 *round_trips) {
  osai_net_request_t request;
  request.payload = 0;
  request.payload_size = 0;
  request.out_value = (u64)round_trips;
  u64 rc = osai_syscall3(OSAI_SYSCALL_NET_TCP_CONNECT, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_smp_run(u64 worker_count, u64 iterations, u64 *ran_workers,
                 u64 *checksum) {
  osai_smp_request_t request;
  request.worker_count = worker_count;
  request.iterations = iterations;
  request.out_workers = (u64)ran_workers;
  request.out_checksum = (u64)checksum;
  u64 rc = osai_syscall3(OSAI_SYSCALL_SMP_RUN, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_cpu_ai_decode(const void *input, u64 input_size, char *output,
                       u64 output_size, u64 *out_size) {
  osai_cpu_ai_decode_request_t request;
  request.input = (u64)input;
  request.input_size = input_size;
  request.output = (u64)output;
  request.output_size = output_size;
  request.out_size = (u64)out_size;
  u64 rc = osai_syscall3(OSAI_SYSCALL_CPU_AI_DECODE, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_remote_login(const char *user, const char *command, char *output,
                      u64 output_size, u64 *out_size) {
  osai_remote_login_request_t request;
  request.user = (u64)user;
  request.user_size = osai_strlen(user);
  request.command = (u64)command;
  request.command_size = osai_strlen(command);
  request.output = (u64)output;
  request.output_size = output_size;
  request.out_size = (u64)out_size;
  u64 rc = osai_syscall3(OSAI_SYSCALL_REMOTE_LOGIN, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_net_external_session(u64 protocol, u64 port, const void *payload,
                              u64 payload_size, char *output,
                              u64 output_size, u64 *out_size) {
  osai_net_external_session_request_t request;
  request.protocol = protocol;
  request.port = port;
  request.payload = (u64)payload;
  request.payload_size = payload_size;
  request.output = (u64)output;
  request.output_size = output_size;
  request.out_size = (u64)out_size;
  u64 rc = osai_syscall3(OSAI_SYSCALL_NET_EXTERNAL_SESSION, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_thread_group_run(u64 thread_count, u64 iterations, u64 *ran_threads,
                          u64 *checksum) {
  osai_thread_group_request_t request;
  request.thread_count = thread_count;
  request.iterations = iterations;
  request.out_threads = (u64)ran_threads;
  request.out_checksum = (u64)checksum;
  u64 rc = osai_syscall3(OSAI_SYSCALL_THREAD_GROUP_RUN, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_ml_run(u64 model_kind, const void *input, u64 input_size,
                char *output, u64 output_size, u64 *out_size) {
  osai_ml_run_request_t request;
  request.model_kind = model_kind;
  request.input = (u64)input;
  request.input_size = input_size;
  request.output = (u64)output;
  request.output_size = output_size;
  request.out_size = (u64)out_size;
  u64 rc = osai_syscall3(OSAI_SYSCALL_ML_RUN, (u64)&request,
                         sizeof(request), 0);
  return rc == ~0ULL ? -1 : (int)rc;
}

int osai_write_file(const char *path, const char *content) {
  int fd = osai_fs_open(path, OSAI_MFS_OPEN_WRITE | OSAI_MFS_OPEN_CREATE |
                                  OSAI_MFS_OPEN_TRUNCATE);
  if (fd < 0) {
    return -1;
  }
  u64 content_len = osai_strlen(content);
  int written = 0;
  if (content_len != 0) {
    written = osai_fs_write(fd, content, content_len);
  }
  if (osai_fs_close(fd) != 0 || written < 0) {
    return -1;
  }
  return written;
}

int osai_read_file(const char *path, char *buffer, u64 buffer_size) {
  int fd = osai_fs_open(path, OSAI_MFS_OPEN_READ);
  if (fd < 0) {
    return -1;
  }
  int bytes = osai_fs_read(fd, buffer, buffer_size);
  if (osai_fs_close(fd) != 0 || bytes < 0) {
    return -1;
  }
  if ((u64)bytes < buffer_size) {
    buffer[bytes] = '\0';
  }
  return bytes;
}

void osai_append_cstr(char *buffer, u64 capacity, u64 *offset,
                      const char *text) {
  if (buffer == 0 || offset == 0 || text == 0 || capacity == 0) {
    return;
  }
  for (u64 i = 0; text[i] != '\0' && *offset + 1 < capacity; ++i) {
    buffer[*offset] = text[i];
    ++(*offset);
  }
  buffer[*offset] = '\0';
}

void osai_append_u64(char *buffer, u64 capacity, u64 *offset, u64 value) {
  char digits[20];
  u64 count = 0;
  if (value == 0) {
    osai_append_cstr(buffer, capacity, offset, "0");
    return;
  }
  while (value != 0 && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10ULL));
    value /= 10ULL;
  }
  while (count > 0) {
    char one[2];
    --count;
    one[0] = digits[count];
    one[1] = '\0';
    osai_append_cstr(buffer, capacity, offset, one);
  }
}

void osai_log_u64(const char *prefix, u64 value, const char *suffix) {
  char line[160];
  u64 offset = 0;
  osai_memzero(line, sizeof(line));
  osai_append_cstr(line, sizeof(line), &offset, prefix);
  osai_append_u64(line, sizeof(line), &offset, value);
  osai_append_cstr(line, sizeof(line), &offset, suffix);
  osai_log(line);
}
