# Adopt hybrid page table structure for user address space isolation

_Source: coding plans from commit period 43c873f → 8083940 — records intent at planning time; the implementation may lag or differ._

**Status:** accepted

## Context
OSAI needs to support concurrent processes with isolated memory (Task 4), but the kernel currently resides in the lower half of the virtual address space. A full kernel relocation to the upper half is risky and complex. A mechanism is needed to provide per-process isolation without moving the kernel.

## Decision drivers
- Avoidance of kernel relocation complexity
- Memory efficiency via shared kernel mappings
- Process isolation security

## Considered options
- **Full kernel relocation to upper half** _(rejected)_ — pros: Clean separation of user/kernel spaces, standard layout; cons: Requires massive refactoring of kernel links and addresses, high risk of regression
- **Shared L0/L1 with per-process L2/L3 sub-trees** — pros: Preserves existing kernel VA layout, shares kernel mappings efficiently, provides isolated user ranges (0x41000000..0x50000000); cons: Context switch requires swapping L2 pointers and TLB invalidation (TLBI VMALLE1IS)

## Decision
Implement a hybrid MMU scheme where L0 and L1 page tables are shared across all contexts, while each process gets private L2/L3 sub-trees for its user virtual address range. Context switches will swap L2 sub-tree pointers and invalidate the TLB.

## Consequences
Kernel remains in lower half without relocation. Context switching incurs a TLB invalidation cost. User PTEs must be marked non-Global (nG) to ensure correct TLB management during switches.