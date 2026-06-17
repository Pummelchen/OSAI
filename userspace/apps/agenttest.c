#include <osai_user.h>

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
  osai_log("/bin/agenttest: agent protocol dispatch test starting\n");

  osai_agent_request_t req;
  osai_agent_response_t resp;
  char output[256];
  u64 out_size = 0;

  /* Test 1: PING */
  osai_memzero(&req, sizeof(req));
  osai_memzero(&resp, sizeof(resp));
  osai_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = OSAI_AGENT_CMD_PING;
  req.cell_id = 0;
  req.payload_size = 0;

  if (osai_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    osai_log("/bin/agenttest: PING dispatch failed\n");
    return 1;
  }
  if (resp.magic != AGENT_RESPONSE_MAGIC) {
    osai_log("/bin/agenttest: PING bad response magic\n");
    return 1;
  }
  if (resp.status != OSAI_AGENT_STATUS_OK) {
    osai_log("/bin/agenttest: PING status not OK\n");
    return 1;
  }
  if (!text_starts_with(output, "pong:")) {
    osai_log("/bin/agenttest: PING output missing pong prefix\n");
    return 1;
  }
  osai_log("/bin/agenttest: PING output=");
  osai_log(output);
  osai_log("\n");

  /* Test 2: INDEX_QUERY */
  osai_memzero(&req, sizeof(req));
  osai_memzero(&resp, sizeof(resp));
  osai_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = OSAI_AGENT_CMD_INDEX_QUERY;
  req.cell_id = 0;
  req.payload_size = 0;

  if (osai_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    osai_log("/bin/agenttest: INDEX_QUERY dispatch failed\n");
    return 1;
  }
  if (resp.status != OSAI_AGENT_STATUS_OK) {
    osai_log("/bin/agenttest: INDEX_QUERY status not OK\n");
    return 1;
  }
  if (!text_starts_with(output, "index:")) {
    osai_log("/bin/agenttest: INDEX_QUERY output missing index prefix\n");
    return 1;
  }
  osai_log("/bin/agenttest: INDEX_QUERY output=");
  osai_log(output);
  osai_log("\n");

  /* Test 3: GIT_STATUS */
  osai_memzero(&req, sizeof(req));
  osai_memzero(&resp, sizeof(resp));
  osai_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = OSAI_AGENT_CMD_GIT_STATUS;
  req.cell_id = 0;
  req.payload_size = 0;

  if (osai_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    osai_log("/bin/agenttest: GIT_STATUS dispatch failed\n");
    return 1;
  }
  if (resp.status != OSAI_AGENT_STATUS_OK) {
    osai_log("/bin/agenttest: GIT_STATUS status not OK\n");
    return 1;
  }
  osai_log("/bin/agenttest: GIT_STATUS output=");
  osai_log(output);
  osai_log("\n");

  /* Test 4: BUILD query */
  osai_memzero(&req, sizeof(req));
  osai_memzero(&resp, sizeof(resp));
  osai_memzero(output, sizeof(output));
  req.magic = AGENT_REQUEST_MAGIC;
  req.version = 1;
  req.command = OSAI_AGENT_CMD_BUILD;
  req.cell_id = 0;
  req.payload_size = 0;

  if (osai_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                          &out_size) < 0) {
    osai_log("/bin/agenttest: BUILD dispatch failed\n");
    return 1;
  }
  if (resp.status != OSAI_AGENT_STATUS_OK) {
    osai_log("/bin/agenttest: BUILD status not OK\n");
    return 1;
  }
  osai_log("/bin/agenttest: BUILD output=");
  osai_log(output);
  osai_log("\n");

  /* Test 5: bad magic should fail gracefully */
  osai_memzero(&req, sizeof(req));
  osai_memzero(&resp, sizeof(resp));
  osai_memzero(output, sizeof(output));
  req.magic = 0xDEADBEEF;
  req.version = 1;
  req.command = OSAI_AGENT_CMD_PING;

  int rc = osai_agent_dispatch(&req, &resp, 0, 0, output, sizeof(output),
                               &out_size);
  if (rc == 0 && resp.status == OSAI_AGENT_STATUS_OK) {
    osai_log("/bin/agenttest: bad magic should have been rejected\n");
    return 1;
  }
  osai_log("/bin/agenttest: bad magic correctly rejected\n");

  osai_log("/bin/agenttest: agent protocol dispatch passed\n");
  osai_log("/bin/agenttest: complete\n");
  return 0;
}
