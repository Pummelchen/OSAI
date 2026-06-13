# Performance Model

OSAI targets four measurable outcomes for CPU-only embedded app agents.

## Summary Targets

Compared with a carefully tuned Linux/BSD baseline, OSAI targets:

| Area | Target improvement |
|---|---:|
| TCP/UDP latency | **Up to 10–45% lower latency** |
| Effective CPU-AI memory bandwidth | **3–18% higher effective bandwidth** |
| Sustained usable CPU-core performance | **2–12% higher sustained performance** |
| Scheduler jitter and thread migration | **Near-zero migration and near-zero hot-path jitter** |

These targets are realistic only for workloads that match OSAI's design assumptions: CPU-only AI inference, embedded app agents, predictable service placement, exclusive core ownership, controlled memory layout, and low-latency network traffic.

## 1. TCP/UDP Latency

Target:

```text
10–45% lower TCP/UDP latency
```

Expected source of improvement:

- direct NIC queue ownership;
- no generic socket hot path for agent traffic;
- fewer cross-core wakeups;
- stable flow affinity;
- no unrelated interrupts on network or AI cores;
- smaller queues optimized for latency instead of bulk throughput;
- adaptive polling under active traffic;
- interrupt fallback only when idle;
- copy-small / zero-copy-large packet policy.

This is expected to be the clearest performance win compared with normal kernel TCP/UDP paths. Against DPDK, AF_XDP, or netmap-style baselines, the advantage is smaller because those systems already avoid much of the kernel packet path.

## 2. Effective CPU-AI Memory Bandwidth

Target:

```text
3–18% higher effective memory bandwidth
```

Expected source of improvement:

- shared model weights;
- fewer duplicate memory copies;
- prefaulted hugepage arenas;
- no swap/reclaim interference;
- no background filesystem or logging pressure on model memory domains;
- NUMA-local allocation;
- memory-bandwidth admission control;
- isolated arenas for model weights, KV/cache, source index, build output, network buffers, and logs.

OSAI cannot raise the physical DRAM or LPDDR bandwidth ceiling. The goal is to increase effective usable bandwidth for CPU AI by preventing unrelated OS work and other services from stealing bandwidth or polluting caches.

## 3. Sustained Usable CPU-Core Performance

Target:

```text
2–12% higher sustained usable core performance
```

Expected source of improvement:

- no thread migration;
- no involuntary context switches;
- no timer tick on hot cores;
- fixed high-performance core policy;
- no SMT sibling interference unless explicitly allowed;
- no background kernel work on AI cores;
- architecture-specific vector and memory-kernel dispatch;
- build/test jobs isolated from inference and network cores.

The biggest gain is not raw peak IPC. The main gain is sustained useful work under load: hot cores stay hot, assigned, and undisturbed.

## 4. Scheduler Jitter and Migration

Target:

```text
near-zero scheduler jitter and migration
```

Expected source of improvement:

- exclusive core leasing;
- app-owned worker placement;
- IRQ routing away from hot cores;
- service manifests that declare CPU, memory, network, and build resources;
- telemetry that treats unwanted migration as a bug.

Core invariants:

```text
core migrations on AI hot path = 0
unexpected context switches on AI hot path = 0
post-warmup page faults on AI hot path = 0
unexpected timer ticks on AI hot path = 0
unexpected IRQs on AI hot path = 0
```

## Benchmark Philosophy

OSAI should not claim to beat Linux/BSD without a disciplined benchmark suite.

The baseline must be serious:

```text
Linux/BSD with CPU affinity
isolated cores
huge pages
NUMA policy
IRQ affinity
performance governor
nohz/tick isolation where available
AF_XDP, DPDK, or netmap where relevant
well-tuned model runtime
```

Minimum benchmark categories:

- TCP p50/p99 latency under inference load;
- UDP p50/p99 latency under inference load;
- token latency and inter-token jitter;
- command-to-patch latency;
- command-to-rebuild latency;
- shared model-weight memory savings;
- effective RAM bandwidth under many app agents;
- source-index and build interference tests;
- migration/context-switch/page-fault counters;
- thermal throttling and sustained CPU frequency tests.

## Honest Claim Boundary

OSAI should avoid vague claims like:

```text
2x faster than Linux
```

A more defensible claim is:

```text
OSAI targets up to 10–45% lower TCP/UDP latency, 3–18% higher effective CPU-AI memory bandwidth, 2–12% higher sustained usable CPU-core performance, and near-zero scheduler jitter/migration for CPU-only embedded app agents.
```
