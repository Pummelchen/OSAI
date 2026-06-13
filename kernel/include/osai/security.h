#ifndef OSAI_SECURITY_H
#define OSAI_SECURITY_H

#include <osai/status.h>
#include <osai/types.h>

void security_policy_init(void);
osai_status_t security_validate_update_signature(const char *signature);
osai_status_t security_reject_credential_material(const char *text);
void security_record_denied_operation(void);
uint64_t security_denied_operation_count(void);
uint64_t security_signature_accept_count(void);
uint64_t security_signature_reject_count(void);
uint64_t security_credential_reject_count(void);
void security_self_test(void);

#endif
