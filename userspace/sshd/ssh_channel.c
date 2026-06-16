#include "ssh_channel.h"

static ssh_channel_t g_channels[SSH_CHANNEL_MAX];
static uint32_t g_next_local_id = 1;

void ssh_channel_init(void) {
  for (uint32_t i = 0; i < SSH_CHANNEL_MAX; ++i) {
    g_channels[i].active = 0;
    g_channels[i].local_id = 0;
    g_channels[i].remote_id = 0;
    g_channels[i].window_size = 0;
  }
  g_next_local_id = 1;
}

static ssh_channel_t *alloc_channel(void) {
  for (uint32_t i = 0; i < SSH_CHANNEL_MAX; ++i) {
    if (!g_channels[i].active) {
      g_channels[i].active = 1;
      g_channels[i].local_id = g_next_local_id++;
      return &g_channels[i];
    }
  }
  return (ssh_channel_t *)0;
}

int ssh_channel_handle_packet(int sockfd, const ssh_packet_t *pkt) {
  if (pkt->len == 0) return -1;
  uint8_t msg_type = pkt->data[0];

  if (msg_type == SSH_MSG_CHANNEL_OPEN) {
    ssh_channel_t *ch = alloc_channel();
    if (!ch) return -1;
    /* Parse: string channel_type, uint32 sender_channel, uint32 initial_window, uint32 max_packet */
    if (pkt->len < 17) return -1;
    uint32_t type_len = ssh_read_string_len(pkt->data + 1);
    uint32_t off = 5 + type_len;
    ch->remote_id = ssh_read_u32_be(pkt->data + off);
    ch->window_size = ssh_read_u32_be(pkt->data + off + 4);
    /* Send CHANNEL_OPEN_CONFIRMATION */
    uint8_t reply[32];
    reply[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    ssh_write_u32_be(reply + 1, ch->remote_id);
    ssh_write_u32_be(reply + 5, ch->local_id);
    ssh_write_u32_be(reply + 9, 65536); /* initial window */
    ssh_write_u32_be(reply + 13, 32768); /* max packet */
    return ssh_packet_write(sockfd, reply, 17);
  }

  if (msg_type == SSH_MSG_CHANNEL_DATA) {
    /* Echo data back as a simple response */
    if (pkt->len < 9) return -1;
    uint32_t remote_id = ssh_read_u32_be(pkt->data + 1);
    uint32_t data_len = ssh_read_string_len(pkt->data + 5);
    if (9 + data_len > pkt->len) return -1;
    /* Build CHANNEL_DATA reply */
    uint8_t reply[SSH_MAX_PACKET_SIZE];
    reply[0] = SSH_MSG_CHANNEL_DATA;
    ssh_write_u32_be(reply + 1, remote_id);
    ssh_write_u32_be(reply + 5, data_len);
    for (uint32_t i = 0; i < data_len; ++i) {
      reply[9 + i] = pkt->data[9 + i];
    }
    return ssh_packet_write(sockfd, reply, 9 + data_len);
  }

  if (msg_type == SSH_MSG_CHANNEL_EOF || msg_type == SSH_MSG_CHANNEL_CLOSE) {
    /* Find and close channel */
    if (pkt->len >= 5) {
      uint32_t remote_id = ssh_read_u32_be(pkt->data + 1);
      for (uint32_t i = 0; i < SSH_CHANNEL_MAX; ++i) {
        if (g_channels[i].active && g_channels[i].remote_id == remote_id) {
          /* Send close back */
          uint8_t close_msg[5];
          close_msg[0] = SSH_MSG_CHANNEL_CLOSE;
          ssh_write_u32_be(close_msg + 1, g_channels[i].remote_id);
          ssh_packet_write(sockfd, close_msg, 5);
          g_channels[i].active = 0;
          break;
        }
      }
    }
    return 0;
  }

  return 0; /* ignore unknown channel messages */
}
