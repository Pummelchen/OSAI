#ifndef OSAI_SECURITY_H
#define OSAI_SECURITY_H

#include <osai/status.h>
#include <osai/types.h>

void security_policy_init(void);
osai_status_t security_authorize_capability(const char *operation,
                                            uint64_t granted,
                                            uint64_t required);
osai_status_t security_authorize_fs_read(const char *path);
osai_status_t security_authorize_fs_write(const char *path);
osai_status_t security_authorize_git_workspace(uint32_t workspace_id,
                                               uint32_t owner_cell_id,
                                               uint32_t actor_cell_id,
                                               const char *operation);
osai_status_t security_authorize_sandbox(uint32_t sandbox_id,
                                         uint32_t owner_cell_id,
                                         uint32_t actor_cell_id,
                                         const char *operation);
osai_status_t security_authorize_rollback(const char *target,
                                          uint32_t authorized);
osai_status_t security_authorize_admin(const char *operation,
                                       uint64_t granted);
osai_status_t security_authorize_update_signature(const char *signature,
                                                  uint64_t granted);
osai_status_t security_validate_update_signature(const char *signature);
osai_status_t security_validate_sandbox_path(const char *path);
osai_status_t security_reject_credential_material(const char *text);
osai_status_t security_reject_credential_material_buffer(const char *text,
                                                         uint64_t length);
osai_status_t security_validate_benchmark_record(const char *record);
void security_record_denied_operation(void);
uint64_t security_denied_operation_count(void);
uint64_t security_capability_denial_count(void);
uint64_t security_fs_denial_count(void);
uint64_t security_workspace_denial_count(void);
uint64_t security_sandbox_denial_count(void);
uint64_t security_rollback_denial_count(void);
uint64_t security_update_policy_reject_count(void);
uint64_t security_signature_accept_count(void);
uint64_t security_signature_reject_count(void);
uint64_t security_credential_reject_count(void);
uint64_t security_admin_denial_count(void);
uint64_t security_update_authorization_count(void);
uint64_t security_update_replay_reject_count(void);
uint64_t security_key_accept_count(void);
uint64_t security_key_reject_count(void);
uint64_t security_sandbox_escape_reject_count(void);
void security_self_test(void);

#endif
