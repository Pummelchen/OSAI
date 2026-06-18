#include <xaios_user.h>

#define AGENT_REQUEST_MAGIC 0x41475251U
#define AGENT_RESPONSE_MAGIC 0x41475253U

static int text_starts_with(const char *text, const char *prefix) {
  if (text == 0 || prefix == 0) {
    return 0;
  }
  for (u64 i = 0; prefix[i] != '\0'; ++i) {
    if (text[i] != prefix[i]) {
      return 0;
    }
  }
  return 1;
}

int main(void) {
  xaios_log("/bin/agenttest: agent protocol dispatch test starting\n");

  xaios_agent_request_t req;
  xaios_agent_response_t resp;
  char output[256];
  u64 out_size = 0;

  /* Test 1: PING */
  xaios_memzero(&req, sizeof(req));
  xaios_memzero(&resp, sizeof(resp));
  xaios_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = XAIOS_AGENT_CMD_PING;
  req.cell_id = 0;
  req.payload_size = 0;

  if (xaios_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    xaios_log("/bin/agenttest: PING dispatch failed\n");
    return 1;
  }
  if (resp.magic != AGENT_RESPONSE_MAGIC) {
    xaios_log("/bin/agenttest: PING bad response magic\n");
    return 1;
  }
  if (resp.status != XAIOS_AGENT_STATUS_OK) {
    xaios_log("/bin/agenttest: PING status not OK\n");
    return 1;
  }
  if (!text_starts_with(output, "pong:")) {
    xaios_log("/bin/agenttest: PING output missing pong prefix\n");
    return 1;
  }
  xaios_log("/bin/agenttest: PING output=");
  xaios_log(output);
  xaios_log("\n");

  /* Test 2: INDEX_QUERY */
  xaios_memzero(&req, sizeof(req));
  xaios_memzero(&resp, sizeof(resp));
  xaios_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = XAIOS_AGENT_CMD_INDEX_QUERY;
  req.cell_id = 0;
  req.payload_size = 0;

  if (xaios_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    xaios_log("/bin/agenttest: INDEX_QUERY dispatch failed\n");
    return 1;
  }
  if (resp.status != XAIOS_AGENT_STATUS_OK) {
    xaios_log("/bin/agenttest: INDEX_QUERY status not OK\n");
    return 1;
  }
  if (!text_starts_with(output, "index:")) {
    xaios_log("/bin/agenttest: INDEX_QUERY output missing index prefix\n");
    return 1;
  }
  xaios_log("/bin/agenttest: INDEX_QUERY output=");
  xaios_log(output);
  xaios_log("\n");

  /* Test 3: GIT_STATUS */
  xaios_memzero(&req, sizeof(req));
  xaios_memzero(&resp, sizeof(resp));
  xaios_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = XAIOS_AGENT_CMD_GIT_STATUS;
  req.cell_id = 0;
  req.payload_size = 0;

  if (xaios_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    xaios_log("/bin/agenttest: GIT_STATUS dispatch failed\n");
    return 1;
  }
  if (resp.status != XAIOS_AGENT_STATUS_OK) {
    xaios_log("/bin/agenttest: GIT_STATUS status not OK\n");
    return 1;
  }
  xaios_log("/bin/agenttest: GIT_STATUS output=");
  xaios_log(output);
  xaios_log("\n");

  /* Test 4: BUILD query */
  xaios_memzero(&req, sizeof(req));
  xaios_memzero(&resp, sizeof(resp));
  xaios_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = XAIOS_AGENT_CMD_BUILD;
  req.cell_id = 0;
  req.payload_size = 0;

  if (xaios_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    xaios_log("/bin/agenttest: BUILD dispatch failed\n");
    return 1;
  }
  if (resp.status != XAIOS_AGENT_STATUS_OK) {
    xaios_log("/bin/agenttest: BUILD status not OK\n");
    return 1;
  }
  xaios_log("/bin/agenttest: BUILD output=");
  xaios_log(output);
  xaios_log("\n");

  /* Test 5: bad magic should fail gracefully */
  xaios_memzero(&req, sizeof(req));
  xaios_memzero(&resp, sizeof(resp));
  xaios_memzero(output, sizeof(output));
  req.magic = 0xDEADBEEF;
  req.version = 1;
  req.command = XAIOS_AGENT_CMD_PING;

  int rc = xaios_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                               &out_size);
  if (rc == 0 && resp.status == XAIOS_AGENT_STATUS_OK) {
    xaios_log("/bin/agenttest: bad magic should have been rejected\n");
    return 1;
  }
  xaios_log("/bin/agenttest: bad magic correctly rejected\n");

  xaios_log("/bin/agenttest: agent protocol dispatch passed\n");
  xaios_log("/bin/agenttest: complete\n");
  return 0;
}
