# Roadmap

OSAI is currently a design-stage project. The roadmap below defines a practical path from architecture concept to bootable prototype to measurable CPU-AI appliance OS.

## Phase 0: Architecture and Benchmarks

Goal: define the target precisely before building the kernel.

Deliverables:

- CPU-AI benchmark suite;
- tuned Linux/BSD baselines;
- latency, bandwidth, jitter, and migration counters;
- ARM SoC, Intel Desktop, and Intel Xeon hardware profiles;
- initial service-manifest format;
- app-agent workload definitions;
- model-weight sharing design;
- source-index/build/test workflow design.

Acceptance criteria:

```text
benchmark targets are measurable
baseline tuning is documented
all target claims have counters
the first prototype can prove or disprove the architecture
```

## Phase 1: Bootable Server Core

Goal: boot a minimal server-only OS on x86-64 and AArch64.

Deliverables:

- UEFI boot;
- x86-64 bring-up;
- AArch64 bring-up;
- serial console;
- SSH-only administration path;
- physical memory manager;
- virtual memory manager;
- basic service supervisor;
- static C/C++ service support;
- basic logging and panic reporting.

Acceptance criteria:

```text
system boots on QEMU x86-64
system boots on QEMU AArch64
system can be administered over SSH
static service can be launched and stopped
```

## Phase 2: AI Cell Runtime

Goal: make resource ownership explicit.

Deliverables:

- exclusive core leases;
- no-migration worker model;
- no-hot-path-preemption mode;
- hugepage model arenas;
- shared read-only model mappings;
- private KV/cache arenas;
- per-cell telemetry;
- post-warmup page-fault detection;
- hot-core IRQ exclusion.

Acceptance criteria:

```text
core migrations on AI hot path = 0
unexpected context switches on AI hot path = 0
post-warmup page faults on AI hot path = 0
model weights can be shared by multiple app agents
```

## Phase 3: Low-Latency Networking

Goal: make low-latency TCP/UDP native for app-agent traffic.

Deliverables:

- per-core TCP/UDP stack;
- NIC queue ownership;
- flow-to-core pinning;
- adaptive polling;
- interrupt fallback only when idle;
- copy-small / zero-copy-large packet policy;
- low-latency service API;
- p50/p99 network telemetry.

Acceptance criteria:

```text
TCP/UDP latency improves against tuned kernel TCP/UDP baseline
network processing stays on the owning core
agent command traffic does not disturb inference cores
```

## Phase 4: App-Agent Tooling

Goal: make source-code modification a controlled OS workflow.

Deliverables:

- repository index service;
- symbol search;
- patch generation interface;
- isolated build cells;
- isolated test cells;
- Git worktree integration;
- diff review workflow;
- commit and rollback workflow;
- hot reload / redeploy hooks.

Acceptance criteria:

```text
agent can propose a patch
patch can build in isolation
patch can test in isolation
patch can be committed or rolled back
build/test load does not damage inference latency beyond configured budget
```

## Phase 5: Platform Optimization

Goal: tune the OS for real hardware classes.

Deliverables:

- ARM SoC profiles;
- Intel Desktop profiles;
- Intel Xeon profiles;
- CPU feature dispatch;
- vector/matrix-kernel selection;
- NUMA-aware memory policy;
- memory-bandwidth admission control;
- thermal and sustained-frequency telemetry;
- benchmark reports.

Acceptance criteria:

```text
platform-specific tuning improves effective CPU-AI bandwidth
hot workers remain on intended cores
memory placement follows service contract
network latency remains stable under app-agent load
```

## Phase 6: Developer Preview

Goal: provide a small but usable prototype for early experimentation.

Deliverables:

- boot image;
- install notes;
- example app-agent service;
- example model-weight sharing setup;
- benchmark scripts;
- service-manifest parser;
- architecture documentation;
- known limitations.

Acceptance criteria:

```text
external developer can boot OSAI
run a sample CPU-only app agent
measure basic latency/bandwidth/jitter counters
compare against a documented Linux/BSD baseline
```

## Claim Discipline

OSAI should not claim to beat Linux/BSD broadly. The project should only claim improvements that are measured for the target workload:

```text
CPU-only embedded app agents
server-hosted deployment
exclusive core ownership
controlled memory layout
low-latency TCP/UDP command traffic
shared model-weight runtime
```

A defensible project claim is:

```text
OSAI targets up to 10–45% lower TCP/UDP latency, 3–18% higher effective CPU-AI memory bandwidth, 2–12% higher sustained usable CPU-core performance, and near-zero scheduler jitter/migration for CPU-only embedded app agents.
```