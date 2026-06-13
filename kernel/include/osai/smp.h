#ifndef OSAI_SMP_H
#define OSAI_SMP_H

#include <osai/types.h>

#define OSAI_MAX_CPUS 4U

typedef enum osai_cpu_role {
  OSAI_CPU_ROLE_OFFLINE = 0,
  OSAI_CPU_ROLE_HOUSEKEEPING = 1,
  OSAI_CPU_ROLE_RESERVED_IDLE = 2,
} osai_cpu_role_t;

typedef struct osai_cpu_state {
  uint32_t cpu_id;
  uint32_t online;
  uint64_t mpidr;
  osai_cpu_role_t role;
} osai_cpu_state_t;

void smp_init_qemu_virt(void);
const osai_cpu_state_t *smp_cpu_state(uint32_t cpu_id);
uint32_t smp_online_count(void);
void smp_self_test(void);
void smp_secondary_main(uint64_t cpu_id);

#endif
