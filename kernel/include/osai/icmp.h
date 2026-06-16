#ifndef OSAI_ICMP_H
#define OSAI_ICMP_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_ICMP_ECHO_REPLY 0U
#define OSAI_ICMP_ECHO_REQUEST 8U

osai_status_t icmp_build_echo_reply(uint8_t *frame, uint64_t *frame_len,
                                     const uint8_t src_mac[6],
                                     const uint8_t dst_mac[6],
                                     uint32_t src_ip, uint32_t dst_ip,
                                     const uint8_t *request_frame,
                                     uint64_t request_len);
osai_status_t icmp_process_echo_request(const uint8_t *frame,
                                         uint64_t frame_len,
                                         uint16_t *identifier,
                                         uint16_t *sequence);
void icmp_self_test(void);

#endif
