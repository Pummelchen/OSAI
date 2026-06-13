#ifndef OSAI_STATUS_H
#define OSAI_STATUS_H

typedef enum osai_status {
  OSAI_OK = 0,
  OSAI_ERR_INVALID = -1,
  OSAI_ERR_NO_MEMORY = -2,
  OSAI_ERR_NOT_FOUND = -3,
  OSAI_ERR_IO = -4,
  OSAI_ERR_BUSY = -5,
} osai_status_t;

#endif
