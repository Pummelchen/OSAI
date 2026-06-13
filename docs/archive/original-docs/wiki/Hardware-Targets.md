# Hardware Targets

OSAI targets CPU-bound AI workloads on three hardware classes.

## A. ARM-Based SoCs

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

Realistic improvement targets:

| Area | Target range |
|---|---:|
| RAM latency | Small p50 win, meaningful p99 win |
| Effective CPU-AI RAM bandwidth | **8–18%** |
| TCP/UDP latency | **15–35%** lower vs tuned kernel TCP/UDP |
| Sustained CPU-core performance | **5–12%** |

ARM SoCs are attractive for compact CPU-AI appliances, but low-level memory-controller, thermal, and power controls may be partly hidden behind firmware. OSAI should treat vendor firmware cooperation as a major practical factor.

## B. Modern Intel Desktop CPUs

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

Realistic improvement targets:

| Area | Target range |
|---|---:|
| RAM latency | Small p50 win, useful p99 win |
| Effective CPU-AI RAM bandwidth | **5–12%** |
| TCP/UDP latency | **20–45%** lower vs tuned kernel TCP/UDP |
| Sustained CPU-core performance | **4–10%** |

Intel desktop CPUs are a strong target for developer appliances and small app-agent servers. The main constraint is memory bandwidth: two-channel DDR-class systems cannot match high-end server platforms for many simultaneous CPU AI workers.

## C. Modern Intel Xeon CPUs

Examples: Xeon systems with many cores, high memory-channel count, ECC memory, server NICs, and AI-oriented CPU instructions.

Expected strengths:

- best serious CPU-AI server target;
- many isolated app-agent cells per host;
- high absolute RAM bandwidth;
- large memory capacity;
- strong networking and storage options;
- good fit for multi-tenant embedded-agent hosting.

OSAI focus on Xeon CPUs:

- strict NUMA-local model and KV/cache allocation;
- shared read-only model weights across many app agents;
- per-agent private state and memory contracts;
- AMX/vector-aware worker placement where available;
- NIC queue pinning to app-agent network workers;
- deterministic low-jitter execution across many concurrent agents.

Realistic improvement targets:

| Area | Target range |
|---|---:|
| RAM latency | Small raw win, moderate p99 win |
| Effective CPU-AI RAM bandwidth | **3–10%**, or higher if baseline NUMA policy is poor |
| TCP/UDP latency | **10–30%** lower vs tuned kernel TCP/UDP |
| Sustained CPU-core performance | **2–8%**, or higher for badly isolated AMX/vector workloads |

Xeon is likely the strongest serious commercial platform for OSAI because it combines many cores, large memory capacity, high memory-channel count, server NICs, and enterprise firmware controls.

## Target Priority

| Rank | Platform | Reason |
|---:|---|---|
| 1 | Modern Intel Xeon | Best serious multi-tenant CPU-AI server target |
| 2 | ARM SoCs / N1X / GB10-class systems | Best compact unified-memory appliance target |
| 3 | Modern Intel Desktop CPUs | Best low-cost developer and small-server target |
