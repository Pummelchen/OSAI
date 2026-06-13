# OSAI Wiki

Welcome to the OSAI technical wiki.

**OSAI** is a server-only operating system architecture for CPU-bound AI models and embedded app agents. It is designed for applications that embed local CPU-only AI workers capable of understanding their own source code, accepting human instructions, generating patches, rebuilding, testing, syncing with Git, and improving the application with minimal operating-system interference.

## Target Benefits

Compared with a carefully tuned Linux/BSD baseline, OSAI targets:

| Area | Target improvement |
|---|---:|
| TCP/UDP latency | **Up to 10–45% lower latency** |
| Effective CPU-AI memory bandwidth | **3–18% higher effective bandwidth** |
| Sustained usable CPU-core performance | **2–12% higher sustained performance** |
| Scheduler jitter and thread migration | **Near-zero migration and near-zero hot-path jitter** |

These are architecture targets, not guaranteed benchmark results. OSAI cannot make DRAM, LPDDR, cache fabric, or CPU cores physically faster than the silicon. The expected gains come from removing avoidable overhead: scheduler migration, unnecessary context switches, page faults after warmup, background kernel work, generic network paths, memory duplication, bad NUMA placement, and shared resource contention.

## Wiki Pages

- [Architecture](Architecture.md)
- [Hardware Targets](Hardware-Targets.md)
- [Performance Model](Performance-Model.md)
- [Embedded App Agents](Embedded-App-Agents.md)
- [Roadmap](Roadmap.md)

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
