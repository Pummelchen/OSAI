#include "sshd.h"
#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include "ssh_channel.h"
#include "ssh_host_key.h"
#include <osai_user.h>

static void mem_copy(void *d, const void *s, uint64_t n) {
  uint8_t *o = (uint8_t *)d; const uint8_t *i = (const uint8_t *)s;
  for (uint64_t j = 0; j < n; ++j) o[j] = i[j];
}

static uint32_t str_len(const char *s) {
  uint32_t n = 0; while (s[n]) ++n; return n;
}

/* Build KEXINIT packet with minimal algorithm lists */
static uint32_t build_kexinit(uint8_t *buf) {
  uint32_t pos = 0;
  buf[pos++] = SSH_MSG_KEXINIT;
  /* 16 bytes cookie (zeros for now) */
  for (uint32_t i = 0; i < 16; ++i) buf[pos++] = 0;
  /* kex_algorithms: "curve25519-sha256" */
  const char *kex = "curve25519-sha256";
  uint32_t kex_len = str_len(kex);
  ssh_write_u32_be(buf + pos, kex_len); pos += 4;
  mem_copy(buf + pos, kex, kex_len); pos += kex_len;
  /* server_host_key_algorithms: "ssh-ed25519" */
  const char *hkey = "ssh-ed25519";
  uint32_t hkey_len = str_len(hkey);
  ssh_write_u32_be(buf + pos, hkey_len); pos += 4;
  mem_copy(buf + pos, hkey, hkey_len); pos += hkey_len;
  /* encryption_algorithms_client_to_server: "aes128-ctr" */
  const char *enc = "aes128-ctr";
  uint32_t enc_len = str_len(enc);
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  /* encryption_algorithms_server_to_client */
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  /* mac_algorithms_client_to_server: "hmac-sha2-256" */
  const char *mac = "hmac-sha2-256";
  uint32_t mac_len = str_len(mac);
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  /* mac_algorithms_server_to_client */
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  /* compression: "none" x2 */
  const char *comp = "none";
  uint32_t comp_len = str_len(comp);
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  /* languages: empty x2 */
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  /* first_kex_packet_follows: 0, reserved: 0 */
  buf[pos++] = 0;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  return pos;
}

/* Handle one SSH connection */
static int handle_connection(int sockfd) {
  /* Send server version */
  if (ssh_send_version(sockfd) != 0) return -1;

  /* Receive client version */
  uint8_t version_buf[256];
  uint32_t version_len = 0;
  if (ssh_recv_version(sockfd, version_buf, sizeof(version_buf),
                       &version_len) != 0) return -1;

  /* Send KEXINIT */
  uint8_t kexinit_buf[512];
  uint32_t kexinit_len = build_kexinit(kexinit_buf);
  if (ssh_packet_write(sockfd, kexinit_buf, kexinit_len) != 0) return -1;

  /* Receive client KEXINIT */
  ssh_packet_t pkt;
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_KEXINIT) return -1;

  /* Handle DH key exchange */
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_KEXDH_INIT) return -1;

  /* Parse client ephemeral public key (string at offset 1) */
  if (pkt.len < 5) return -1;
  uint32_t client_pub_len = ssh_read_string_len(pkt.data + 1);
  if (client_pub_len != 32 || pkt.len < 5 + 32) return -1;
  uint8_t client_pub[32];
  mem_copy(client_pub, pkt.data + 5, 32);

  /* Generate ephemeral server key pair */
  uint8_t server_priv[32] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
  };
  uint8_t server_pub[32];
  curve25519_base(server_pub, server_priv);

  /* Compute shared secret */
  uint8_t shared_secret[32];
  curve25519_scalar_mult(shared_secret, server_priv, client_pub);

  /* Build KEXDH_REPLY */
  uint8_t reply[512];
  uint32_t rpos = 0;
  reply[rpos++] = SSH_MSG_KEXDH_REPLY;
  /* host_key: string (fake ed25519 blob) */
  uint8_t host_pub[32];
  ssh_host_key_get_public(host_pub);
  uint32_t host_key_blob_len = 4 + 11 + 4 + 32; /* "ssh-ed25519" + key */
  ssh_write_u32_be(reply + rpos, host_key_blob_len); rpos += 4;
  ssh_write_u32_be(reply + rpos, 11); rpos += 4;
  mem_copy(reply + rpos, "ssh-ed25519", 11); rpos += 11;
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, host_pub, 32); rpos += 32;
  /* f (server public key) as string */
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, server_pub, 32); rpos += 32;
  /* signature (fake - just SHA-256 of shared secret for testing) */
  uint8_t sig[32];
  sha256_hash(shared_secret, 32, sig);
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, sig, 32); rpos += 32;

  if (ssh_packet_write(sockfd, reply, rpos) != 0) return -1;

  /* Send NEWKEYS */
  uint8_t newkeys = SSH_MSG_NEWKEYS;
  if (ssh_packet_write(sockfd, &newkeys, 1) != 0) return -1;

  /* Receive NEWKEYS */
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_NEWKEYS) return -1;

  /* Message loop: handle service requests, auth, channels */
  ssh_channel_init();
  for (uint32_t msg_count = 0; msg_count < 100; ++msg_count) {
    if (ssh_packet_read(sockfd, &pkt) != 0) break;
    if (pkt.len == 0) continue;
    uint8_t msg = pkt.data[0];

    if (msg == SSH_MSG_SERVICE_REQUEST) {
      /* Accept any service request */
      /* Actually send SERVICE_ACCEPT first */
      uint8_t sa[32];
      sa[0] = 6; /* SERVICE_ACCEPT = 6 */
      const char *svc = "ssh-userauth";
      uint32_t svc_len = str_len(svc);
      ssh_write_u32_be(sa + 1, svc_len);
      mem_copy(sa + 5, svc, svc_len);
      ssh_packet_write(sockfd, sa, 5 + svc_len);
    } else if (msg == SSH_MSG_USERAUTH_REQUEST) {
      /* Simple password check */
      uint8_t auth_reply[1] = {SSH_MSG_USERAUTH_SUCCESS};
      ssh_packet_write(sockfd, auth_reply, 1);
    } else if (msg >= SSH_MSG_CHANNEL_OPEN &&
               msg <= SSH_MSG_CHANNEL_CLOSE) {
      if (ssh_channel_handle_packet(sockfd, &pkt) != 0) break;
    } else if (msg == SSH_MSG_GLOBAL_REQUEST) {
      /* Ignore global requests */
    }
  }
  return 0;
}

int sshd_run(void) {
  /* Crypto self-test */
  ssh_crypto_self_test();

  /* Listen on SSH port */
  u64 listen_fd = 0;
  if (osai_net_listen(SSHD_PORT, &listen_fd) != 0) {
    return -1;
  }

  /* Accept loop */
  for (;;) {
    u64 conn_fd = 0;
    if (osai_net_accept(listen_fd, &conn_fd) != 0) {
      continue;
    }
    handle_connection((int)conn_fd);
    osai_net_close(conn_fd);
  }
  return 0; /* unreachable */
}

/* Entry point - called from _start via osai_main pattern */
void sshd_main(void) {
  sshd_run();
  osai_exit(0);
}
