#ifndef XAIOS_GIC_H
#define XAIOS_GIC_H

#include <xaios/types.h>

typedef struct xaios_gic_info {
  uint64_t distributor_base;
  uint32_t typer;
  uint32_t iidr;
  uint32_t interrupt_lines;
  uint32_t cpu_count_hint;
} xaios_gic_info_t;

void gic_init_qemu_virt(void);
void gic_enable_full(void);
void gic_disable_full(void);
void gic_secondary_init(uint32_t cpu_id);
const xaios_gic_info_t *gic_info(void);
void gic_self_test(void);

#endif
