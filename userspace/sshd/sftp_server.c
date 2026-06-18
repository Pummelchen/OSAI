#include "ssh_protocol.h"
#include <xaios_user.h>
#include <xaios/mutable_fs.h>

/* ---- SFTP Protocol Constants ---- */
#define SFTP_VERSION 3
#define SFTP_MAX_PACKET_SIZE 32768
#define SFTP_MAX_HANDLES 64

/* SFTP Message Types */
#define SSH_FXP_INIT        1
#define SSH_FXP_VERSION     2
#define SSH_FXP_OPEN        3
#define SSH_FXP_CLOSE       4
#define SSH_FXP_READ        5
#define SSH_FXP_WRITE       6
#define SSH_FXP_LSTAT       7
#define SSH_FXP_FSTAT       8
#define SSH_FXP_SETSTAT     9
#define SSH_FXP_FSETSTAT   10
#define SSH_FXP_OPENDIR    11
#define SSH_FXP_READDIR    12
#define SSH_FXP_REMOVE     13
#define SSH_FXP_MKDIR      14
#define SSH_FXP_RMDIR      15
#define SSH_FXP_REALPATH   16
#define SSH_FXP_STAT       17
#define SSH_FXP_RENAME     18
#define SSH_FXP_READLINK   19
#define SSH_FXP_SYMLINK    20

/* SFTP Response Types */
#define SSH_FXP_STATUS      101
#define SSH_FXP_HANDLE      102
#define SSH_FXP_DATA        103
#define SSH_FXP_NAME        104
#define SSH_FXP_ATTRS       105

/* SFTP Status Codes */
#define SSH_FX_OK                0
#define SSH_FX_EOF               1
#define SSH_FX_NO_SUCH_FILE      2
#define SSH_FX_PERMISSION_DENIED 3
#define SSH_FX_FAILURE           4
#define SSH_FX_BAD_MESSAGE       5
#define SSH_FX_NO_CONNECTION     6
#define SSH_FX_CONNECTION_LOST   7
#define SSH_FX_OP_UNSUPPORTED    8

/* SFTP Open Flags */
#define SSH_FXF_READ    0x00000001
#define SSH_FXF_WRITE   0x00000002
#define SSH_FXF_APPEND  0x00000004
#define SSH_FXF_CREAT   0x00000008
#define SSH_FXF_TRUNC   0x00000010
#define SSH_FXF_EXCL    0x00000020

/* SFTP File Handle */
typedef struct {
  uint32_t handle_id;
  char path[XAIOS_MFS_PATH_MAX];
  uint64_t offset;
  int is_open;
  int is_dir;
} sftp_file_handle_t;

/* Global State */
static sftp_file_handle_t g_sftp_handles[SFTP_MAX_HANDLES];
static uint32_t g_next_handle_id = 1;

/* ---- Utility Functions ---- */
static void mem_copy(void *d, const void *s, uint64_t n) {
  uint8_t *o = (uint8_t *)d; const uint8_t *i = (const uint8_t *)s;
  for (uint64_t j = 0; j < n; ++j) o[j] = i[j];
}

static void mem_zero(void *p, uint64_t n) {
  uint8_t *b = (uint8_t *)p;
  for (uint64_t i = 0; i < n; ++i) b[i] = 0;
}

static uint32_t str_len(const char *s) {
  uint32_t n = 0; while (s[n]) ++n; return n;
}

/* Validate path - prevent directory traversal */
static int validate_path(const char *path) {
  if (path == 0 || path[0] != '/') return -1;
  
  /* Check for ".." components */
  const char *p = path;
  while (*p) {
    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
      return -1; /* Directory traversal attempt */
    }
    p++;
  }
  
  return 0;
}

/* Allocate new handle */
static sftp_file_handle_t *alloc_handle(void) {
  for (uint32_t i = 0; i < SFTP_MAX_HANDLES; ++i) {
    if (!g_sftp_handles[i].is_open) {
      g_sftp_handles[i].is_open = 1;
      g_sftp_handles[i].handle_id = g_next_handle_id++;
      g_sftp_handles[i].offset = 0;
      g_sftp_handles[i].is_dir = 0;
      return &g_sftp_handles[i];
    }
  }
  return 0;
}

/* Find handle by ID */
static sftp_file_handle_t *find_handle(uint32_t handle_id) {
  for (uint32_t i = 0; i < SFTP_MAX_HANDLES; ++i) {
    if (g_sftp_handles[i].is_open && g_sftp_handles[i].handle_id == handle_id) {
      return &g_sftp_handles[i];
    }
  }
  return 0;
}

/* ---- Packet Parsing ---- */
static uint32_t read_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void write_u64(uint8_t *p, uint64_t v) {
  write_u32(p, (uint32_t)(v >> 32));
  write_u32(p + 4, (uint32_t)v);
}

static const char *read_string(const uint8_t *p, uint32_t *out_len) {
  *out_len = read_u32(p);
  return (const char *)(p + 4);
}

/* ---- Response Builders ---- */
static int send_status(int sockfd, uint32_t request_id, uint32_t status_code,
                       const char *message) {
  uint8_t buf[256];
  uint32_t pos = 0;
  
  buf[pos++] = SSH_FXP_STATUS;
  write_u32(buf + pos, request_id); pos += 4;
  write_u32(buf + pos, status_code); pos += 4;
  
  /* Error message */
  uint32_t msg_len = message ? str_len(message) : 0;
  write_u32(buf + pos, msg_len); pos += 4;
  if (msg_len > 0) {
    mem_copy(buf + pos, message, msg_len);
    pos += msg_len;
  }
  
  /* Language tag (empty) */
  write_u32(buf + pos, 0); pos += 4;
  
  return ssh_packet_write(sockfd, buf, pos);
}

static int send_handle(int sockfd, uint32_t request_id, uint32_t handle_id) {
  uint8_t buf[64];
  uint32_t pos = 0;
  
  buf[pos++] = SSH_FXP_HANDLE;
  write_u32(buf + pos, request_id); pos += 4;
  
  /* Handle as string */
  write_u32(buf + pos, 4); pos += 4;
  write_u32(buf + pos, handle_id); pos += 4;
  
  return ssh_packet_write(sockfd, buf, pos);
}

static int send_data(int sockfd, uint32_t request_id, const uint8_t *data,
                     uint32_t data_len) {
  uint8_t buf[SFTP_MAX_PACKET_SIZE];
  uint32_t pos = 0;
  
  buf[pos++] = SSH_FXP_DATA;
  write_u32(buf + pos, request_id); pos += 4;
  write_u32(buf + pos, data_len); pos += 4;
  mem_copy(buf + pos, data, data_len); pos += data_len;
  
  return ssh_packet_write(sockfd, buf, pos);
}

/* ---- SFTP Request Handlers ---- */
static int handle_open(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid OPEN");
  
  uint32_t request_id = read_u32(data);
  uint32_t path_len;
  const char *path = read_string(data + 4, &path_len);
  
  if (path_len >= XAIOS_MFS_PATH_MAX) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "Path too long");
  }
  
  /* Validate path */
  char local_path[XAIOS_MFS_PATH_MAX];
  mem_copy(local_path, path, path_len);
  local_path[path_len] = '\0';
  
  if (validate_path(local_path) != 0) {
    return send_status(sockfd, request_id, SSH_FX_PERMISSION_DENIED, "Invalid path");
  }
  
  /* Parse flags */
  uint32_t flags = read_u32(data + 8 + path_len);
  
  /* Allocate handle */
  sftp_file_handle_t *handle = alloc_handle();
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "No handles available");
  }
  
  mem_copy(handle->path, local_path, path_len + 1);
  
  /* For now, accept all opens (production: check file existence, permissions) */
  return send_handle(sockfd, request_id, handle->handle_id);
}

static int handle_close(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid CLOSE");
  
  uint32_t request_id = read_u32(data);
  uint32_t handle_len;
  const char *handle_data = read_string(data + 4, &handle_len);
  
  if (handle_len != 4) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle");
  }
  
  uint32_t handle_id = read_u32((const uint8_t *)handle_data);
  sftp_file_handle_t *handle = find_handle(handle_id);
  
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle ID");
  }
  
  /* Close handle */
  handle->is_open = 0;
  mem_zero(handle, sizeof(sftp_file_handle_t));
  
  return send_status(sockfd, request_id, SSH_FX_OK, "Success");
}

static int handle_read(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 20) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid READ");
  
  uint32_t request_id = read_u32(data);
  uint32_t handle_len;
  const char *handle_data = read_string(data + 4, &handle_len);
  
  if (handle_len != 4) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle");
  }
  
  uint32_t handle_id = read_u32((const uint8_t *)handle_data);
  uint64_t offset = read_u32(data + 8 + handle_len) | 
                    ((uint64_t)read_u32(data + 12 + handle_len) << 32);
  uint32_t read_len = read_u32(data + 16 + handle_len);
  
  sftp_file_handle_t *handle = find_handle(handle_id);
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle ID");
  }
  
  /* Read file data (production: use xaios_mfs_read) */
  uint8_t file_data[SFTP_MAX_PACKET_SIZE];
  uint32_t actual_len = 0;
  
  /* Placeholder: in production, read from actual filesystem */
  /* For now, return EOF */
  return send_status(sockfd, request_id, SSH_FX_EOF, "End of file");
}

static int handle_write(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 20) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid WRITE");
  
  uint32_t request_id = read_u32(data);
  uint32_t handle_len;
  const char *handle_data = read_string(data + 4, &handle_len);
  
  if (handle_len != 4) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle");
  }
  
  uint32_t handle_id = read_u32((const uint8_t *)handle_data);
  uint64_t offset = read_u32(data + 8 + handle_len) | 
                    ((uint64_t)read_u32(data + 12 + handle_len) << 32);
  uint32_t write_len = read_u32(data + 16 + handle_len);
  
  sftp_file_handle_t *handle = find_handle(handle_id);
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle ID");
  }
  
  /* Write file data (production: use xaios_mfs_write) */
  /* Placeholder: acknowledge write */
  return send_status(sockfd, request_id, SSH_FX_OK, "Success");
}

static int handle_opendir(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid OPENDIR");
  
  uint32_t request_id = read_u32(data);
  uint32_t path_len;
  const char *path = read_string(data + 4, &path_len);
  
  if (path_len >= XAIOS_MFS_PATH_MAX) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "Path too long");
  }
  
  char local_path[XAIOS_MFS_PATH_MAX];
  mem_copy(local_path, path, path_len);
  local_path[path_len] = '\0';
  
  if (validate_path(local_path) != 0) {
    return send_status(sockfd, request_id, SSH_FX_PERMISSION_DENIED, "Invalid path");
  }
  
  /* Allocate handle */
  sftp_file_handle_t *handle = alloc_handle();
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "No handles available");
  }
  
  mem_copy(handle->path, local_path, path_len + 1);
  handle->is_dir = 1;
  
  return send_handle(sockfd, request_id, handle->handle_id);
}

static int handle_readdir(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid READDIR");
  
  uint32_t request_id = read_u32(data);
  uint32_t handle_len;
  const char *handle_data = read_string(data + 4, &handle_len);
  
  if (handle_len != 4) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle");
  }
  
  uint32_t handle_id = read_u32((const uint8_t *)handle_data);
  sftp_file_handle_t *handle = find_handle(handle_id);
  
  if (handle == 0) {
    return send_status(sockfd, request_id, SSH_FX_BAD_MESSAGE, "Invalid handle ID");
  }
  
  /* For now, return EOF (production: list directory contents) */
  return send_status(sockfd, request_id, SSH_FX_EOF, "End of directory");
}

static int handle_mkdir(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid MKDIR");
  
  uint32_t request_id = read_u32(data);
  uint32_t path_len;
  const char *path = read_string(data + 4, &path_len);
  
  if (path_len >= XAIOS_MFS_PATH_MAX) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "Path too long");
  }
  
  char local_path[XAIOS_MFS_PATH_MAX];
  mem_copy(local_path, path, path_len);
  local_path[path_len] = '\0';
  
  if (validate_path(local_path) != 0) {
    return send_status(sockfd, request_id, SSH_FX_PERMISSION_DENIED, "Invalid path");
  }
  
  /* Create directory (production: use xaios_mfs_mkdir) */
  /* Placeholder: acknowledge */
  return send_status(sockfd, request_id, SSH_FX_OK, "Success");
}

static int handle_remove(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid REMOVE");
  
  uint32_t request_id = read_u32(data);
  uint32_t path_len;
  const char *path = read_string(data + 4, &path_len);
  
  char local_path[XAIOS_MFS_PATH_MAX];
  mem_copy(local_path, path, path_len);
  local_path[path_len] = '\0';
  
  if (validate_path(local_path) != 0) {
    return send_status(sockfd, request_id, SSH_FX_PERMISSION_DENIED, "Invalid path");
  }
  
  /* Remove file (production: use xaios_mfs_delete) */
  /* Placeholder: acknowledge */
  return send_status(sockfd, request_id, SSH_FX_OK, "Success");
}

static int handle_rename(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 12) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid RENAME");
  
  uint32_t request_id = read_u32(data);
  uint32_t old_len;
  const char *old_path = read_string(data + 4, &old_len);
  
  uint32_t new_len;
  const char *new_path = read_string(data + 8 + old_len, &new_len);
  
  char old_local[XAIOS_MFS_PATH_MAX];
  char new_local[XAIOS_MFS_PATH_MAX];
  
  if (old_len >= XAIOS_MFS_PATH_MAX || new_len >= XAIOS_MFS_PATH_MAX) {
    return send_status(sockfd, request_id, SSH_FX_FAILURE, "Path too long");
  }
  
  mem_copy(old_local, old_path, old_len);
  old_local[old_len] = '\0';
  
  mem_copy(new_local, new_path, new_len);
  new_local[new_len] = '\0';
  
  if (validate_path(old_local) != 0 || validate_path(new_local) != 0) {
    return send_status(sockfd, request_id, SSH_FX_PERMISSION_DENIED, "Invalid path");
  }
  
  /* Rename file (production: use xaios_mfs_rename) */
  /* Placeholder: acknowledge */
  return send_status(sockfd, request_id, SSH_FX_OK, "Success");
}

static int handle_stat(int sockfd, const uint8_t *data, uint32_t len) {
  if (len < 8) return send_status(sockfd, 0, SSH_FX_BAD_MESSAGE, "Invalid STAT");
  
  uint32_t request_id = read_u32(data);
  uint32_t path_len;
  const char *path = read_string(data + 4, &path_len);
  
  char local_path[XAIOS_MFS_PATH_MAX];
  mem_copy(local_path, path, path_len);
  local_path[path_len] = '\0';
  
  if (validate_path(local_path) != 0) {
    return send_status(sockfd, request_id, SSH_FX_NO_SUCH_FILE, "File not found");
  }
  
  /* Get file stat (production: use xaios_mfs_stat) */
  /* Placeholder: return dummy attrs */
  uint8_t buf[64];
  uint32_t pos = 0;
  
  buf[pos++] = SSH_FXP_ATTRS;
  write_u32(buf + pos, request_id); pos += 4;
  
  /* Attributes: size (8 bytes), uid (4), gid (4), permissions (4), atime (4), mtime (4) */
  write_u32(buf + pos, 0x00000001); pos += 4; /* flags: SSH_FILEXFER_ATTR_SIZE */
  write_u64(buf + pos, 0); pos += 8;           /* size */
  
  return ssh_packet_write(sockfd, buf, pos);
}

/* ---- Main SFTP Message Handler ---- */
int sftp_handle_message(int sockfd, const uint8_t *data, uint32_t len) {
  if (len == 0) return -1;
  
  uint8_t msg_type = data[0];
  
  switch (msg_type) {
    case SSH_FXP_INIT:
      /* Client sends version, we reply with our version */
      {
        uint8_t buf[16];
        buf[0] = SSH_FXP_VERSION;
        write_u32(buf + 1, SFTP_VERSION);
        return ssh_packet_write(sockfd, buf, 5);
      }
    
    case SSH_FXP_OPEN:
      return handle_open(sockfd, data + 1, len - 1);
    
    case SSH_FXP_CLOSE:
      return handle_close(sockfd, data + 1, len - 1);
    
    case SSH_FXP_READ:
      return handle_read(sockfd, data + 1, len - 1);
    
    case SSH_FXP_WRITE:
      return handle_write(sockfd, data + 1, len - 1);
    
    case SSH_FXP_OPENDIR:
      return handle_opendir(sockfd, data + 1, len - 1);
    
    case SSH_FXP_READDIR:
      return handle_readdir(sockfd, data + 1, len - 1);
    
    case SSH_FXP_REMOVE:
      return handle_remove(sockfd, data + 1, len - 1);
    
    case SSH_FXP_MKDIR:
      return handle_mkdir(sockfd, data + 1, len - 1);
    
    case SSH_FXP_RENAME:
      return handle_rename(sockfd, data + 1, len - 1);
    
    case SSH_FXP_STAT:
    case SSH_FXP_LSTAT:
    case SSH_FXP_FSTAT:
      return handle_stat(sockfd, data + 1, len - 1);
    
    default:
      /* Unsupported operation */
      return send_status(sockfd, 0, SSH_FX_OP_UNSUPPORTED, "Operation not supported");
  }
}

/* ---- SFTP Session Start ---- */
int sftp_session_start(int sockfd, uint32_t channel_id) {
  (void)channel_id;
  
  /* Wait for SSH_FXP_INIT from client */
  ssh_packet_t pkt;
  if (ssh_packet_read(sockfd, &pkt) != 0) {
    return -1;
  }
  
  /* Handle SFTP messages in loop */
  for (;;) {
    if (sftp_handle_message(sockfd, pkt.data, pkt.len) != 0) {
      break;
    }
    
    if (ssh_packet_read(sockfd, &pkt) != 0) {
      break;
    }
  }
  
  return 0;
}
