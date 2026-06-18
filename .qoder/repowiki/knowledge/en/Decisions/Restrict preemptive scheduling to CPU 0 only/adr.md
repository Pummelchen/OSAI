# Restrict preemptive scheduling to CPU 0 only

_Source: coding plans from commit period 43c873f → 8083940 — records intent at planning time; the implementation may lag or differ._

**Status:** accepted

## Context
OSAI is introducing a preemptive scheduler to support concurrent workloads. The hardware target includes multiple cores, some of which are designated as 'leased AI cores'. A decision is needed on how to distribute scheduling responsibility.

## Decision drivers
- Simplicity of initial SMP implementation
- Preservation of AI core execution guarantees
- Avoidance of cross-core migration complexity

## Considered options
- **Full SMP preemptive scheduling on all cores** _(rejected)_ — pros: Maximizes throughput, balances load automatically; cons: Complex lock contention, requires migration logic, risks interrupting AI workloads
- **Single-core preemption on CPU 0** — pros: Simple round-robin implementation, zero migration overhead, AI cores (1-3) remain uninterrupted; cons: CPU 0 becomes a bottleneck for system services and general tasks

## Decision
Configure the AArch64 generic timer and GICv3 to drive a round-robin preemptive scheduler exclusively on CPU 0. CPUs 1-3 are reserved for leased AI workloads and will not participate in preemption or task migration.

## Consequences
System services and user processes run concurrently only on CPU 0. AI cores operate in a non-preemptive mode, ensuring deterministic execution for AI tasks but requiring explicit yielding or completion to release the core.