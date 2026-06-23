#include "ssh_protocol.h"
#include "ssh_crypto.h"
#include "ssh_connection.h"
#include <xaios_user.h>

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
    int r = xaios_net_send((u64)(uint64_t)sockfd,
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
    int r = xaios_net_recv((u64)(uint64_t)sockfd,
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
    int r = xaios_net_recv((u64)(uint64_t)sockfd, buf + pos, 1, &n);
    if (r != 0 || n == 0) return -1;
    
    /* FIX-003: Reject overly long version strings (buffer overflow protection) */
    if (pos > 255) {
      return -1;  /* Version string too long */
    }
    
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
  
  /* FIX-003: Comprehensive packet size validation */
  if (pkt_len > SSH_MAX_PACKET_SIZE) {
    return -1;  /* Packet too large */
  }
  if (pkt_len < 2) {
    return -1;  /* Packet too small */
  }
  
  if (recv_all(sockfd, pkt->data, pkt_len) != 0) return -1;
  
  /* FIX-003: Validate padding length */
  uint32_t padding = pkt->data[0];
  if (padding >= pkt_len - 1) {
    return -1;  /* Invalid padding */
  }
  
  pkt->len = pkt_len - padding - 1;
  
  /* FIX-003: Validate payload length */
  if (pkt->len > SSH_MAX_PACKET_SIZE - 5) {
    return -1;  /* Payload too large */
  }
  
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
  if (padding < 4) return -1;
  uint32_t pkt_len = len + padding + 1;
  uint8_t header[5];
  ssh_write_u32_be(header, pkt_len);
  header[4] = (uint8_t)padding;
  if (send_all(sockfd, header, 5) != 0) return -1;
  if (send_all(sockfd, data, len) != 0) return -1;
  uint8_t pad[256];
  crypto_random_bytes(pad, padding);
  if (send_all(sockfd, pad, padding) != 0) return -1;
  return 0;
}

int ssh_packet_write_encrypted(int sockfd, const uint8_t *data, uint32_t len) {
  ssh_connection_t *conn = ssh_conn_find((uint64_t)(uint64_t)sockfd);
  if (!conn || !conn->crypto.enabled) return -1;

  uint32_t block_size = 16; /* AES block size */
  uint32_t mac_len = 32;    /* HMAC-SHA-256 */
  uint32_t padding = block_size - ((len + 5) % block_size);
  if (padding < 4) padding += block_size;
  uint32_t pkt_len = len + padding + 1;

  /* Build: packet_length(4) + padding_length(1) + payload + padding + MAC */
  uint8_t *pkt = conn->encrypt_packet;
  ssh_write_u32_be(pkt, pkt_len);
  pkt[4] = (uint8_t)padding;
  for (uint32_t i = 0; i < len; ++i) pkt[5 + i] = data[i];
  crypto_random_bytes(pkt + 5 + len, padding);

  /* Encrypt payload (not length field) with AES-CTR */
  conn->crypto.encrypt_seq++;
  aes128_ctr(&conn->crypto.encrypt_ctx, conn->crypto.encrypt_iv,
              pkt + 4, pkt + 4, pkt_len);

  /* Compute MAC over seq + encrypted packet */
  uint64_t seq = conn->crypto.encrypt_seq - 1;
  uint8_t *mac_input = conn->mac_input;
  for (uint32_t i = 0; i < 8; ++i) {
    mac_input[i] = (uint8_t)(seq >> (56 - i * 8));
  }
  for (uint32_t i = 0; i < 4 + pkt_len; ++i) {
    mac_input[8 + i] = pkt[i];
  }
  hmac_sha256(conn->crypto.encrypt_mac_key, 32, mac_input, 8 + 4 + pkt_len,
              pkt + 4 + pkt_len);

  /* Send: length(4) + encrypted_pkt(pkt_len) + MAC(32) */
  if (send_all(sockfd, pkt, 4 + pkt_len + mac_len) != 0) return -1;
  return 0;
}

int ssh_packet_read_encrypted(int sockfd, ssh_packet_t *out_pkt) {
  ssh_connection_t *conn = ssh_conn_find((uint64_t)(uint64_t)sockfd);
  if (!conn || !conn->crypto.enabled) return -1;

  /* Read encrypted length (first 4 bytes are sent in the clear) */
  uint8_t len_buf[4];
  if (recv_all(sockfd, len_buf, 4) != 0) return -1;
  uint32_t pkt_len = ssh_read_u32_be(len_buf);
  if (pkt_len > SSH_MAX_PACKET_SIZE) return -1;
  if (pkt_len < 2) return -1;

  uint32_t mac_len = 32;

  /* Read the rest: encrypted block + MAC */
  uint8_t *full = conn->decrypt_full_packet;
  for (uint32_t i = 0; i < 4; ++i) full[i] = len_buf[i];
  if (recv_all(sockfd, full + 4, pkt_len + mac_len) != 0) return -1;

  /* Decrypt the packet body */
  aes128_ctr(&conn->crypto.decrypt_ctx, conn->crypto.decrypt_iv,
              full + 4, full + 4, pkt_len);

  /* Verify MAC */
  uint64_t seq = conn->crypto.decrypt_seq;
  uint8_t *mac_input = conn->decrypt_mac_input;
  for (uint32_t i = 0; i < 8; ++i) {
    mac_input[i] = (uint8_t)(seq >> (56 - i * 8));
  }
  for (uint32_t i = 0; i < 4 + pkt_len; ++i) {
    mac_input[8 + i] = full[i];
  }
  uint8_t computed_mac[32];
  hmac_sha256(conn->crypto.decrypt_mac_key, 32, mac_input, 8 + 4 + pkt_len,
              computed_mac);
  uint8_t *rcvd_mac = full + 4 + pkt_len;
  uint32_t mac_ok = 1;
  for (uint32_t i = 0; i < 32 && mac_ok; ++i) {
    if (computed_mac[i] != rcvd_mac[i]) mac_ok = 0;
  }
  if (!mac_ok) return -1;

  conn->crypto.decrypt_seq++;

  /* Extract payload */
  uint8_t padding = full[4];
  uint32_t payload_len = pkt_len - padding - 1;
  if (payload_len > SSH_MAX_PACKET_SIZE - 5) return -1;

  out_pkt->len = payload_len;
  for (uint32_t i = 0; i < payload_len; ++i) {
    out_pkt->data[i] = full[5 + i];
  }
  return 0;
}
