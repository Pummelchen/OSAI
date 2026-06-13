# OSAI

**OSAI** is a proposed server-only operating system architecture for **CPU-bound AI models** and **embedded app agents**.

OSAI is not trying to be another general-purpose Linux or BSD replacement. It is designed for a narrower target: applications that embed small CPU-only AI models so they can understand their own source code, accept human instructions, generate code changes, rebuild, test, sync with Git, and improve themselves with minimal operating-system interference.

The central idea is simple:

> Give CPU AI agents predictable memory bandwidth, low network latency, exclusive CPU ownership, and near-zero scheduler jitter.

## Target Benefits

When compared with a carefully tuned Linux/BSD baseline, OSAI targets:

| Area | Target improvement |
|---|---:|
| TCP/UDP latency | **Up to 10–45% lower latency** |
| Effective CPU-AI memory bandwidth | **3–18% higher effective bandwidth** |
| Sustained usable CPU-core performance | **2–12% higher sustained performance** |
| Scheduler jitter and thread migration | **Near-zero migration and near-zero hot-path jitter** |

These are **architecture targets**, not guaranteed benchmark results. OSAI cannot make DRAM, LPDDR, cache fabric, or CPU cores physically faster than the underlying silicon. The expected gains come from removing avoidable overhead: scheduler migration, unnecessary context switches, page faults after warmup, background kernel work, generic network paths, memory duplication, bad NUMA placement, and shared resource contention.

## Why CPU-Only AI?

Most AI infrastructure is optimized for large GPU workloads. OSAI focuses on a different market:

- small and medium CPU-only models embedded into normal applications;
- app-local agents that understand the Git repository they run inside;
- fast command-to-code workflows;
- local agents that can patch, rebuild, test, and deploy changes;
- server-hosted AI services where predictable latency matters more than GPU throughput;
- ARM, Intel Desktop, and Intel Xeon systems running without CUDA, Metal, or vendor GPU runtimes.

A target app-agent loop looks like this:

```text
human request
  -> app-local AI agent
  -> source-code index
  -> patch generation
  -> rebuild / test
  -> Git sync
  -> hot reload or redeploy
  -> running app becomes smarter
```

OSAI is designed around making this loop fast, deterministic, and safe.

## Hardware Focus

OSAI targets CPU-bound AI workloads on three hardware classes.

### A. ARM-Based SoCs

Examples: NVIDIA N1X/GB10-class ARM SoCs and similar high-bandwidth integrated systems.

Expected strengths:

- high memory capacity per watt;
- unified or tightly integrated memory systems;
- strong appliance form factor;
- good fit for compact AI-agent servers;
- useful for many small agents sharing model weights.

OSAI focus on ARM SoCs:

- isolate performance cores from background system work;
- pin AI agents to specific cores or clusters;
- keep housekeeping, Git sync, logging, and rebuild tasks off hot inference cores;
- use large prefaulted model arenas;
- avoid runtime page faults and background memory pressure;
- control NIC queue ownership for low-latency command traffic.

### B. Modern Intel Desktop CPUs

Examples: Intel Core Ultra desktop-class CPUs and similar high-frequency client/server-appliance systems.

Expected strengths:

- high single-thread and low-latency core performance;
- inexpensive developer and small-server deployment;
- strong compile/test performance;
- good fit for local app-agent development appliances.

OSAI focus on Intel Desktop CPUs:

- avoid P-core/E-core misplacement;
- prevent thread migration between unlike cores;
- reserve hot cores for inference and agent control loops;
- isolate build/test jobs from model-serving cores;
- disable unwanted timer ticks and power-state latency on hot cores;
- compensate for limited memory channels through shared model weights and careful memory layout.

### C. Modern Intel Xeon CPUs

Examples: Xeon systems with many cores, high memory-channel count, ECC memory, server NICs, and AI-oriented CPU instructions.

Expected strengths:

- best serious CPU-AI server target;
- many isolated app-agent cells per host;
- high absolute RAM bandwidth;
- large memory capacity;
- strong networking and storage options;
- good fit for multi-tenant embedded-agent hosting.

OSAI focus on Xeon CPUs:

- strict NUMA-local model and KV-cache allocation;
- shared read-only model weights across many app agents;
- per-agent private state and memory contracts;
- AMX/vector-aware worker placement where available;
- NIC queue pinning to app-agent network workers;
- deterministic low-jitter execution across many concurrent agents.

## Core Architecture

OSAI is built as a **server-only AI appliance OS**. It is not designed around desktop applications, GUI workloads, or broad POSIX compatibility first.

The native execution unit is an **AI cell**.

An AI cell owns:

```text
fixed CPU cores
fixed memory arena
fixed model-weight mapping
fixed KV/cache arena
fixed NIC RX/TX queues
fixed source-code index
fixed build/test sandbox
fixed Git workspace
fixed telemetry counters
```

The kernel and system services handle the slow path:

```text
boot
SSH administration
resource ownership
memory mapping
IOMMU/SMMU protection
interrupt routing
fault handling
package updates
service supervision
logging
```

The hot path avoids the kernel whenever possible:

```text
NIC queue -> app-agent TCP/UDP stack -> command parser -> CPU model runtime -> source/code tools -> response stream
```

## Design Principles

### 1. No Thread Migration by Default

AI agent cores are leased exclusively. A hot worker stays on its assigned core unless the service explicitly changes its placement.

Target invariant:

```text
core migrations on AI hot path = 0
```

### 2. No Involuntary Context Switching on Hot Cores

OSAI is designed so model workers, source-index workers, and low-latency network workers do not get interrupted by unrelated OS work.

Target invariant:

```text
unexpected context switches on AI hot path = 0
```

### 3. Memory Layout Is a First-Class Feature

CPU AI is often memory-bound. OSAI treats memory placement as part of the application contract.

OSAI memory features:

- shared read-only model weights;
- large page / hugepage model mappings;
- prefaulted model and KV/cache arenas;
- no swap for AI memory;
- no post-warmup page faults;
- NUMA-local allocation by default;
- isolated arenas for model weights, KV/cache, source index, build output, network buffers, and logs.

### 4. Shared Model Weights Across Apps

A host running 50 smart apps should not load 50 duplicate copies of the same model.

OSAI should support:

```text
one physical model-weight copy
many app-agent mappings
private per-agent KV/cache state
private source-code context
private permission boundary
```

This is one of the most important ways to improve effective CPU-AI memory bandwidth and memory capacity.

### 5. Low-Latency TCP/UDP Is Native

Many embedded AI agents will be controlled over local APIs, RPC, HTTP, WebSocket, custom TCP, or UDP protocols. OSAI gives network queues to app-agent cells directly where safe.

Network goals:

- per-core NIC RX/TX queue ownership;
- stable flow-to-core mapping;
- no generic kernel socket buffer on the hot path;
- no softirq-style cross-core packet handling;
- adaptive polling for active traffic;
- interrupt fallback only when idle;
- copy-small / zero-copy-large packet strategy;
- lower p99 latency under AI and build load.

### 6. Build, Test, and Git Are OS-Level Workflows

The AI agent is not only a chatbot. It must be able to modify the app safely.

OSAI should provide controlled services for:

- repository indexing;
- symbol search;
- patch generation;
- isolated builds;
- isolated tests;
- Git diff review;
- signed commits;
- Git sync;
- hot reload;
- rollback.

The OS should make the safe path the fast path.

## Performance Model

OSAI targets four measurable outcomes.

### TCP/UDP Latency

Target:

```text
10–45% lower TCP/UDP latency
```

Expected source of improvement:

- direct NIC queue ownership;
- no generic socket hot path;
- fewer cross-core wakeups;
- stable flow affinity;
- no unrelated interrupts on network or AI cores;
- smaller queues optimized for latency instead of bulk throughput.

### Effective CPU-AI Memory Bandwidth

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
- memory-bandwidth admission control.

### Sustained Usable CPU-Core Performance

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
- architecture-specific vector and memory-kernel dispatch.

### Scheduler Jitter and Migration

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

## Example Service Manifest

```toml
[service]
name = "smart-app-agent"
binary = "/apps/smart-app-agent/bin/agent"

[model]
name = "local-coder-small"
mode = "cpu-only"
weights = "/models/local-coder-small/model.gguf"
shared_weights = true
context_tokens = 32768
quantization = "int4"

[cores]
network = "2"
inference = "3-7"
source_index = "8"
build_test = "9-15"
migration = "forbidden"
preemption = "forbidden_on_hot_cores"

[memory]
model_arena = "shared"
kv_arena = "private"
hugepages = true
prefault = true
swap = false
numa = "local-only"
page_faults_after_ready = "fatal"

[network]
stack = "osai-lowlatency"
ports = [8080]
rx_queues = "owned"
tx_queues = "owned"
latency_profile = "min"

[git]
repository = "/srv/apps/smart-app"
mode = "worktree"
allow_commit = true
allow_push = false
require_tests = true
rollback = true
```

## What OSAI Is Not

OSAI is not:

- a general-purpose desktop OS;
- a Linux distribution;
- a BSD fork;
- a GPU AI runtime;
- a CUDA or Metal replacement;
- a Kubernetes clone;
- a POSIX compatibility project first;
- an attempt to make hardware exceed its physical limits.

OSAI is a specialized server OS for CPU-only AI agents embedded into applications.

## Initial Roadmap

### Phase 0: Architecture and Benchmarks

- Define CPU-AI benchmark suite.
- Define tuned Linux/BSD baselines.
- Define latency, bandwidth, jitter, and migration counters.
- Define ARM, Intel Desktop, and Xeon hardware profiles.

### Phase 1: Bootable Server Core

- UEFI boot.
- x86-64 and AArch64 bring-up.
- SSH-only administration.
- Basic memory manager.
- Basic service supervisor.
- Static C/C++ service support.

### Phase 2: AI Cell Runtime

- Exclusive core leases.
- No-migration worker model.
- Hugepage model arenas.
- Shared read-only model mappings.
- Private KV/cache arenas.
- Per-cell telemetry.

### Phase 3: Low-Latency Networking

- Per-core TCP/UDP stack.
- NIC queue ownership.
- Flow-to-core pinning.
- Adaptive polling.
- Low-latency service API.

### Phase 4: App-Agent Tooling

- Repository index service.
- Patch generation interface.
- Isolated build/test cells.
- Git worktree integration.
- Commit and rollback workflow.

### Phase 5: Platform Optimization

- ARM SoC profiles.
- Intel Desktop profiles.
- Intel Xeon profiles.
- CPU feature dispatch.
- NUMA-aware memory policy.
- Bandwidth and jitter tuning.

## Project Status

OSAI is currently a design-stage project. The README defines the product direction and target architecture. The next step is to add design documents, benchmark definitions, and a minimal bootable prototype plan.

## License

License to be decided.
