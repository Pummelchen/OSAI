#ifndef SSH_CHANNEL_H
#define SSH_CHANNEL_H

#include <osai/types.h>
#include "ssh_protocol.h"

#define SSH_CHANNEL_MAX 4U

typedef struct ssh_channel {
  uint32_t active;
  uint32_t local_id;
  uint32_t remote_id;
  uint32_t window_size;
} ssh_channel_t;

void ssh_channel_init(void);
int ssh_channel_handle_packet(int sockfd, const ssh_packet_t *pkt);

#endif
