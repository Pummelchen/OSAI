#ifndef XAIOS_AGENT_PROTOCOL_H
#define XAIOS_AGENT_PROTOCOL_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_AGENT_CMD_INFERENCE 1U
#define XAIOS_AGENT_CMD_INDEX_QUERY 2U
#define XAIOS_AGENT_CMD_GIT_STATUS 3U
#define XAIOS_AGENT_CMD_GIT_DIFF 4U
#define XAIOS_AGENT_CMD_BUILD 5U
#define XAIOS_AGENT_CMD_PING 6U

#define XAIOS_AGENT_STATUS_OK 0U
#define XAIOS_AGENT_STATUS_INVALID 1U
#define XAIOS_AGENT_STATUS_DENIED 2U
#define XAIOS_AGENT_STATUS_NOT_FOUND 3U
#define XAIOS_AGENT_STATUS_INTERNAL_ERROR 4U

#define XAIOS_AGENT_HEADER_SIZE 128U
#define XAIOS_AGENT_MAX_PAYLOAD 4096U

typedef struct xaios_agent_request {
  uint32_t magic;          /* 0x41475251 = "AGRQ" */
  uint32_t version;        /* protocol version (1) */
  uint32_t command;        /* XAIOS_AGENT_CMD_* */
  uint32_t cell_id;        /* target AI cell */
  uint64_t payload_size;
  uint8_t reserved[104];
} xaios_agent_request_t;

typedef struct xaios_agent_response {
  uint32_t magic;          /* 0x41475253 = "AGRS" */
  uint32_t version;
  uint32_t status;         /* XAIOS_AGENT_STATUS_* */
  uint32_t command;        /* echoed back */
  uint64_t payload_size;
  uint8_t reserved[104];
} xaios_agent_response_t;

void agent_protocol_init(void);
xaios_status_t agent_protocol_dispatch(const xaios_agent_request_t *request,
                                      xaios_agent_response_t *response,
                                      const uint8_t *payload,
                                      uint64_t payload_size,
                                      char *output, uint64_t output_capacity,
                                      uint64_t *output_bytes);
uint64_t agent_protocol_request_count(void);
uint64_t agent_protocol_error_count(void);
void agent_protocol_self_test(void);

#endif
