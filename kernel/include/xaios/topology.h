#ifndef XAIOS_TOPOLOGY_H
#define XAIOS_TOPOLOGY_H

#include <xaios/smp.h>
#include <xaios/types.h>

/*
 * CPU Topology and Hierarchical Scheduling Domains
 *
 * Supports up to 131,072 CPUs organized in a 4-level hierarchy:
 *   Level 0: Core domain (SMT threads sharing L1, up to 16 members)
 *   Level 1: Socket domain (cores sharing L2/L3, up to 16 core domains)
 *   Level 2: NUMA domain (sockets sharing memory, up to 16 socket domains)
 *   Level 3: System domain (all CPUs, single instance)
 *
 * For QEMU virt: assumes single NUMA node, single socket, linear IDs, no SMT.
 * Real hardware would parse ACPI MADT/SLIT or Device Tree for actual topology.
 */

#define XAIOS_SCHED_DOMAIN_MAX_MEMBERS 16U
#define XAIOS_SCHED_DOMAIN_MAX_LEVELS  4U

/* Scheduling domain hierarchy */
typedef struct xaios_sched_domain {
  uint32_t domain_id;
  uint32_t level;            /* 0=core, 1=socket, 2=numa, 3=system */
  uint32_t parent_domain;    /* parent domain ID (UINT32_MAX if root) */
  uint32_t member_count;
  uint32_t members[XAIOS_SCHED_DOMAIN_MAX_MEMBERS];
  uint32_t load_estimate;    /* cached: sum of member runqueue sizes */
} xaios_sched_domain_t;

/* Per-CPU topology information */
typedef struct xaios_cpu_topology {
  uint32_t cpu_id;
  uint32_t numa_node;
  uint32_t socket_id;
  uint32_t core_id;
  uint32_t thread_id;        /* 0 = primary, >0 = SMT sibling */
  uint32_t sched_domain_id;  /* leaf domain (level 0) */
} xaios_cpu_topology_t;

/* Domain level accessors */
uint32_t topology_get_core_domain(uint32_t cpu_id);
uint32_t topology_get_socket_domain(uint32_t cpu_id);
uint32_t topology_get_numa_domain(uint32_t cpu_id);
const xaios_sched_domain_t *topology_get_domain(uint32_t domain_id);
const xaios_cpu_topology_t *topology_get_cpu(uint32_t cpu_id);
uint32_t topology_get_numa_node_for_cpu(uint32_t cpu_id);

/* Initialization */
void topology_init(void);
void topology_self_test(void);

#endif
