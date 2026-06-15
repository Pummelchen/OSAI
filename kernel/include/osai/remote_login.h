#ifndef OSAI_REMOTE_LOGIN_H
#define OSAI_REMOTE_LOGIN_H

#include <osai/status.h>
#include <osai/types.h>

osai_status_t remote_login_execute(const char *user, const char *command,
                                   char *output, uint64_t output_capacity,
                                   uint64_t *output_bytes);
uint64_t remote_login_session_count(void);
uint64_t remote_login_command_count(void);
uint64_t remote_login_denial_count(void);
void remote_login_self_test(void);

#endif
