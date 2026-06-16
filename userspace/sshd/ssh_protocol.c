#include "ssh_protocol.h"
#include <osai_user.h>

uint32_t ssh_read_u32_be(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void ssh_write_u32_be(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

uint32_t ssh_read_string_len(const uint8_t *p) {
  return ssh_read_u32_be(p);
}

static int send_all(int sockfd, const void *data, uint64_t len) {
  uint64_t sent = 0;
  while (sent < len) {
    u64 n = 0;
    int r = osai_net_send((u64)(uint64_t)sockfd,
                          (const uint8_t *)data + sent, len - sent, &n);
    if (r != 0 || n == 0) return -1;
    sent += n;
  }
  return 0;
}

static int recv_all(int sockfd, void *data, uint64_t len) {
  uint64_t got = 0;
  while (got < len) {
    u64 n = 0;
    int r = osai_net_recv((u64)(uint64_t)sockfd,
                          (uint8_t *)data + got, len - got, &n);
    if (r != 0 || n == 0) return -1;
    got += n;
  }
  return 0;
}

int ssh_send_version(int sockfd) {
  const char *version = SSH_VERSION_SERVER "\r\n";
  uint64_t len = 0;
  while (version[len]) ++len;
  return send_all(sockfd, version, len);
}

int ssh_recv_version(int sockfd, uint8_t *buf, uint32_t buf_size,
                     uint32_t *out_len) {
  /* Read until \n */
  uint32_t pos = 0;
  while (pos < buf_size) {
    u64 n = 0;
    int r = osai_net_recv((u64)(uint64_t)sockfd, buf + pos, 1, &n);
    if (r != 0 || n == 0) return -1;
    if (buf[pos] == '\n') {
      *out_len = pos + 1;
      return 0;
    }
    ++pos;
  }
  return -1;
}

int ssh_packet_read(int sockfd, ssh_packet_t *pkt) {
  /* Read 4-byte packet length */
  uint8_t len_buf[4];
  if (recv_all(sockfd, len_buf, 4) != 0) return -1;
  uint32_t pkt_len = ssh_read_u32_be(len_buf);
  if (pkt_len > SSH_MAX_PACKET_SIZE || pkt_len < 2) return -1;
  if (recv_all(sockfd, pkt->data, pkt_len) != 0) return -1;
  /* pkt->data[0] = padding_length, data starts at [1+padding_length] */
  uint32_t padding = pkt->data[0];
  pkt->len = pkt_len - padding - 1;
  /* Shift payload to start */
  for (uint32_t i = 0; i < pkt->len; ++i) {
    pkt->data[i] = pkt->data[1 + padding + i];
  }
  return 0;
}

int ssh_packet_write(int sockfd, const uint8_t *data, uint32_t len) {
  uint32_t block_size = 8;
  uint32_t padding = block_size - ((len + 5) % block_size);
  if (padding < 4) padding += block_size;
  uint32_t pkt_len = len + padding + 1;
  uint8_t header[5];
  ssh_write_u32_be(header, pkt_len);
  header[4] = (uint8_t)padding;
  if (send_all(sockfd, header, 5) != 0) return -1;
  if (send_all(sockfd, data, len) != 0) return -1;
  /* Send zero padding */
  uint8_t pad[32];
  for (uint32_t i = 0; i < padding && i < 32; ++i) pad[i] = 0;
  if (padding > 0 && send_all(sockfd, pad, padding) != 0) return -1;
  return 0;
}
