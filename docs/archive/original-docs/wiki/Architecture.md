# Architecture

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

## Kernel Role

The OSAI kernel should be small but not primitive. Its job is to enforce ownership and isolation, not to sit in the hot path.

Kernel responsibilities:

- UEFI boot;
- x86-64 and AArch64 bring-up;
- physical and virtual memory management;
- capability-based resource ownership;
- IOMMU/SMMU protection for device access;
- interrupt routing;
- service startup;
- crash/fault handling;
- signed updates and rollback;
- minimal SSH-controlled administration path.

The kernel should avoid:

- global load balancing on AI cores;
- default thread migration;
- generic kernel TCP hot path for app-agent traffic;
- transparent swap for AI memory;
- background reclaim on model memory;
- broad POSIX semantics in the first design stage.

## Native Service Model

OSAI services should declare their resources upfront:

```text
CPU cores
memory arenas
model files
KV/cache budget
network queues
storage access
Git repository access
build/test permissions
```

The OS should refuse to start a service if the declared resources cannot be granted without violating another service's contract.
