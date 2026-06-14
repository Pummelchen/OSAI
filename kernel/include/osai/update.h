#ifndef OSAI_UPDATE_H
#define OSAI_UPDATE_H

#include <osai/status.h>
#include <osai/types.h>

void update_runtime_init(void);
osai_status_t update_begin(uint32_t generation, const char *target,
                           const char *signature);
osai_status_t update_stage(void);
osai_status_t update_commit(void);
osai_status_t update_fail(void);
osai_status_t update_recover_boot(void);
osai_status_t update_rollback(void);

uint64_t update_transaction_count(void);
uint64_t update_stage_count(void);
uint64_t update_commit_count(void);
uint64_t update_failure_count(void);
uint64_t update_recovery_count(void);
uint64_t update_rollback_count(void);
uint64_t update_boot_fallback_count(void);
uint64_t update_record_persist_count(void);
uint64_t update_rollback_point_count(void);
uint64_t update_reject_count(void);

void update_self_test(void);

#endif
