# OSAI 0-to-100% Implementation Plan

This document is the master build plan for implementing OSAI with Codex. It is written as an execution guide: start at the top, implement one phase at a time, and do not move to the next phase until the stated tests and acceptance criteria pass.

OSAI is a server-only operating system for CPU-bound AI models and embedded application agents. The first target is QEMU on macOS for correctness only. Real performance work starts on Intel Desktop, then Intel Xeon, then ARM SoCs such as NVIDIA N1X-class systems.

## 1. Product Objective

OSAI exists to let normal applications embed local CPU-only AI agents that can understand the app source tree, accept human commands, generate patches, rebuild, test, sync with Git, and redeploy or hot reload the app with minimal OS interference.

The hot path is:

```text
human command
  -> low-latency TCP or UDP request
  -> app-local AI agent
  -> shared CPU model runtime
  -> source-code index
  -> patch generation
  -> build and test sandbox
  -> Git operation
  -> hot reload or redeploy
```

OSAI is not a Linux distribution, BSD fork, desktop OS, GPU runtime, CUDA replacement, Metal replacement, or POSIX-first compatibility project.

## 2. Performance Targets

On real hardware, compared with carefully tuned Linux or BSD baselines, OSAI targets:

| Area | Target |
|---|---:|
| TCP/UDP latency | Up to 10-45% lower latency |
| Effective CPU-AI memory bandwidth | 3-18% higher effective bandwidth |
| Sustained usable CPU-core performance | 2-12% higher sustained performance |
| Scheduler jitter and thread migration | Near-zero migration and near-zero hot-path jitter |

These are architecture targets, not magic hardware claims. OSAI cannot reduce physical DRAM timing beyond the silicon. The gains must come from removing avoidable overhead: thread migration, context switching, page faults after warmup, generic socket buffers, background kernel work, duplicate model copies, bad NUMA placement, interrupt noise, and shared-resource contention.

## 3. Hardware Rollout Order

### Stage A: QEMU on macOS

Purpose: correctness, boot architecture, kernel basics, device model, userspace lifecycle, AI Cell API shape. No performance claims count from QEMU.

Initial QEMU target:

```text
arch: x86-64
firmware: UEFI OVMF
machine: q35
memory: 2 GiB
smp: 4 cores
storage: EFI FAT image, later virtio-blk
network: virtio-net with host port forwarding
console: serial stdio
```

### Stage B: Intel Desktop

Purpose: first real performance target. Focus on high single-core performance, P-core/E-core placement, low-latency NICs, NVMe, and shared model-weight memory.

Default policy:

```text
P-cores: AI inference and low-latency network workers
E-cores: SSH, logging, source indexing, Git, build/test, background services
SMT siblings: disabled or reserved until explicitly enabled
migration: forbidden for hot workers
```

### Stage C: Intel Xeon

Purpose: serious multi-agent CPU-AI server target. Focus on NUMA, many memory channels, server NICs, MSI-X, IOMMU, NVMe multi-queue, AMX/vector dispatch where available, and multi-cell admission control.

Default policy:

```text
one latency-sensitive AI Cell stays inside one NUMA node
model weights are NUMA-local or deliberately replicated
network queues are NUMA-local when possible
NVMe queues are NUMA-local when possible
cross-node memory is forbidden unless explicitly configured
```

### Stage D: ARM / NVIDIA N1X-Class

Purpose: CPU-only AI on AArch64 SoCs. No CUDA, Metal, GPU, or NPU dependency.

Default policy:

```text
highest performance cores: AI inference
middle cores: network and tokenizer workers
efficiency cores: SSH, logging, Git, source indexing, build/test
cluster migration: forbidden for AI hot workers
firmware path: UEFI plus ACPI first, Device Tree fallback later
```

## 4. Core Architecture

The native execution unit is an AI Cell.

An AI Cell owns:

```text
fixed CPU cores
fixed memory arenas
fixed shared model-weight mappings
fixed private KV/cache arenas
fixed NIC RX/TX queues
fixed source-code index
fixed build/test sandbox
fixed Git workspace
fixed telemetry counters
```

The kernel owns:

```text
boot
CPU bring-up
memory protection
capabilities
IOMMU or SMMU
interrupt routing
core leases
fault handling
device setup
service supervision
secure update
SSH/admin control plane
```

Hot path rule:

```text
No kernel scheduling, no migration, no page faults, no generic socket buffers, and no unrelated interrupts on AI hot cores after service READY.
```

## 5. Repository Structure Codex Must Create

Codex must evolve the repository toward this layout:

```text
OSAI/
  README.md
  docs/
    README.md
    IMPLEMENTATION_PLAN.md
    ARCHITECTURE.md
    BENCHMARKS.md
    CODEX_RULES.md
    HARDWARE_TARGETS.md
  Makefile
  CMakeLists.txt or build.zig
  scripts/
    macos-bootstrap.sh
    build-image.sh
    run-qemu-x86_64.sh
    run-qemu-aarch64.sh
    test.sh
    format.sh
    check-tree.sh
  boot/
    uefi/
      include/
      x86_64/
      aarch64/
      loader_main.c
      boot_info.h
  kernel/
    include/osai/
      abi.h
      status.h
      types.h
    arch/
      x86_64/
      aarch64/
    core/
    mm/
    irq/
    sched/
    cap/
    dev/
    net/
    fs/
    user/
  userspace/
    init/
    osctl/
    sshd/
    services/
  runtime/
    include/osai/
    src/
  ai/
    runtime/
    gguf/
    tokenizer/
    kernels/
      scalar/
      x86_64/
      aarch64/
    source_index/
    git_agent/
  bench/
    ram_latency/
    ram_bandwidth/
    tcp_latency/
    udp_latency/
    core_jitter/
    model_decode/
  tests/
    unit/
    integration/
    qemu/
```

## 6. Codex Rules

Every Codex task must be scoped to one deliverable.

Task format:

```markdown
## Task ID

### Goal
One specific result.

### Files to create or modify
Exact paths.

### Implementation requirements
Precise behavior, APIs, invariants, and edge cases.

### Tests
Commands and expected output.

### Definition of done
Observable pass/fail criteria.

### Do not
Forbidden shortcuts.
```

Coding rules:

1. Kernel code is freestanding C99 plus minimal architecture assembly.
2. C++ is allowed in userspace, runtime, and AI components, not early kernel core.
3. No libc assumptions inside the kernel.
4. No heap allocation before kernel heap initialization.
5. No dynamic allocation in scheduler, networking, AI Cell, or memory hot paths after service warmup.
6. No migration unless a service explicitly opts into it.
7. No hidden background work on leased AI cores.
8. No post-READY page faults on hot AI cores by default.
9. Every architecture feature needs a generic fallback.
10. Every hot-path structure must document cache-line ownership.

## 7. Phase 0: Build and Host Bootstrap

Goal: create a reproducible macOS build and test skeleton.

Files:

```text
Makefile
scripts/macos-bootstrap.sh
scripts/build-image.sh
scripts/run-qemu-x86_64.sh
scripts/test.sh
scripts/check-tree.sh
```

Implementation requirements:

- `make all` builds the active target.
- `make image` builds a bootable disk image.
- `make qemu` launches QEMU.
- `make test` runs host-side checks.
- Build artifacts live only in `build/`.
- `scripts/macos-bootstrap.sh` verifies QEMU, LLVM, LLD, Python, Git, image tools, and OVMF availability.

Acceptance criteria:

```text
make all succeeds
make image succeeds
make test succeeds
make qemu starts QEMU even before a real kernel exists
```

## 8. Phase 1: UEFI Loader and First QEMU Boot

Goal: boot in QEMU on macOS and print deterministic serial output.

Files:

```text
boot/uefi/loader_main.c
boot/uefi/boot_info.h
boot/uefi/include/uefi_min.h
scripts/build-image.sh
scripts/run-qemu-x86_64.sh
```

Implementation requirements:

- Build `osai_loader.efi` as a UEFI PE/COFF application.
- Use UEFI Boot Services only before ExitBootServices.
- Print `OSAI loader starting` to console and serial where possible.
- Locate `/EFI/OSAI/kernel.elf`.
- Load and validate ELF64.
- Gather UEFI memory map.
- Gather ACPI RSDP pointer.
- Exit boot services.
- Jump to `kernel_entry` with a versioned boot info structure.

Boot info fields:

```text
magic
version
memory_map
memory_map_size
memory_descriptor_size
acpi_rsdp
framebuffer_base
framebuffer_width
framebuffer_height
kernel_phys_base
kernel_virt_base
initramfs_base
initramfs_size
```

Acceptance criteria:

```text
QEMU serial log shows loader start
kernel.elf is loaded
kernel entry is reached
missing kernel.elf produces a useful boot error
```

Do not use GRUB, BIOS boot, or hardcoded QEMU memory assumptions.

## 9. Phase 2: Kernel Entry, Logging, Panic, and PMM

Goal: enter the freestanding kernel and create early memory management.

Files:

```text
kernel/core/kmain.c
kernel/core/klog.c
kernel/core/panic.c
kernel/core/assert.c
kernel/mm/pmm.c
kernel/include/osai/types.h
kernel/include/osai/status.h
```

Required APIs:

```c
void kmain(const osai_boot_info_t* boot);
void klog_init(const osai_boot_info_t* boot);
void klog(const char* fmt, ...);
void panic(const char* fmt, ...);
void pmm_init(const osai_boot_info_t* boot);
void* pmm_alloc_page(void);
void pmm_free_page(void* page);
```

Memory rules:

- Only UEFI conventional memory becomes allocatable.
- Kernel, boot info, loader data, ACPI, MMIO, framebuffer, and reserved regions are never free.
- Page size starts at 4 KiB.
- All counters use 64-bit types.
- Allocator reports total, free, used, and reserved memory.

Acceptance criteria:

```text
kernel entry log appears
memory totals are printed
1024 allocate/free page test passes
reserved regions are not allocated
panic path prints file, line, and message
```

## 10. Phase 3: Virtual Memory, Kernel Heap, and Hugepage Metadata

Goal: build stable kernel address space and a small control-plane heap.

Files:

```text
kernel/arch/x86_64/pmap.c
kernel/mm/vmm.c
kernel/mm/heap.c
kernel/mm/arena.c
kernel/mm/hugepage.c
```

Implementation requirements:

- x86-64 uses 4-level paging first.
- 5-level paging is postponed to Intel hardware phase.
- Kernel text is read/execute.
- Kernel rodata is read-only.
- Kernel data is read/write.
- MMIO is mapped uncached where needed.
- NX is used where supported.
- Kernel is compiled with no red zone.
- Heap is for control-plane work only.
- Add metadata for 2 MiB hugepages, but do not require them yet.

Acceptance criteria:

```text
map/unmap test page passes
heap allocate/free test passes
read-only mapping test fails safely
hugepage support detection is logged
```

## 11. Phase 4: Exceptions, Interrupts, and Timers

Goal: handle CPU exceptions and basic interrupts correctly.

Files:

```text
kernel/arch/x86_64/gdt.c
kernel/arch/x86_64/idt.c
kernel/arch/x86_64/traps.c
kernel/arch/x86_64/apic.c
kernel/arch/x86_64/tsc.c
kernel/irq/irq.c
kernel/irq/route.c
```

Implementation requirements:

- Install GDT and IDT.
- Handle all x86-64 CPU exception vectors.
- Page fault handler prints fault address and error code.
- Unknown fatal exception panics.
- Detect local APIC or x2APIC.
- Provide monotonic time abstraction.
- Do not design a tick scheduler.

Acceptance criteria:

```text
controlled invalid opcode test reports correctly
controlled page fault reports correctly
breakpoint test reports correctly
monotonic time increases
```

Hot-core invariant: future AI cores must not receive periodic scheduler ticks.

## 12. Phase 5: SMP and Per-Core State

Goal: bring up multiple CPUs and prepare no-migration core leasing.

Files:

```text
kernel/arch/x86_64/smp.c
kernel/core/per_cpu.c
kernel/core/cpu.c
kernel/sched/core_lease.c
```

CPU roles:

```text
BOOT
HOUSEKEEPING
NETWORK
AI_INFERENCE
SOURCE_INDEX
BUILD_TEST
IDLE_RESERVED
```

Required API:

```c
osai_status_t core_lease_create(uint32_t cell_id, uint32_t cpu_id, uint64_t flags);
osai_status_t core_lease_release(uint32_t cell_id, uint32_t cpu_id);
bool core_is_leased(uint32_t cpu_id);
```

Initial policy:

- CPU 0 is housekeeping.
- Other CPUs are idle reserved.
- No generic load balancing exists.
- A leased core runs only its assigned loop.

Acceptance criteria:

```text
QEMU with 4 CPUs brings all cores online
CPU registry prints logical id and APIC id
CPU 1 can be leased to a fixed test loop
leased test loop never migrates
```

## 13. Phase 6: User Mode, ELF Loader, and Minimal Syscalls

Goal: run `/init` in user mode from an initramfs.

Files:

```text
kernel/user/elf.c
kernel/user/process.c
kernel/user/syscall.c
userspace/init/main.c
runtime/include/osai/syscall.h
runtime/src/syscall.c
```

Initial syscalls:

```text
SYS_LOG_WRITE
SYS_EXIT
SYS_TIME_NOW
SYS_MEM_MAP
SYS_MEM_UNMAP
SYS_CELL_INFO
```

Process rules:

- Every process belongs to a Cell.
- Control-plane processes run on housekeeping cores.
- AI Cell workers are pinned.
- User memory is isolated from kernel memory.

Acceptance criteria:

```text
/init runs in user mode
/init logs through SYS_LOG_WRITE
/init exits cleanly
bad syscall returns error and does not crash kernel
```

## 14. Phase 7: Device Discovery, PCI, Virtio, and Storage

Goal: discover devices and read files from a block-backed boot image.

Files:

```text
kernel/dev/acpi/
kernel/dev/pci/
kernel/dev/virtio/
kernel/dev/serial/
kernel/fs/initramfs.c
kernel/fs/gpt.c
```

Implementation order:

1. ACPI RSDP from boot info.
2. XSDT/RSDT table walking.
3. MADT parsing for CPU and interrupt topology.
4. PCI ECAM discovery where available.
5. PCI config read/write.
6. Virtio feature negotiation.
7. Virtio-blk read path.
8. GPT parser.
9. Initramfs or simple read-only filesystem loading.

Acceptance criteria:

```text
QEMU virtio-blk is discovered
GPT is parsed
/init is loaded from disk image
/etc/osai.conf is read and logged
block read errors are handled safely
```

## 15. Phase 8: Control-Plane Networking and SSH

Goal: bring up networking in QEMU and make OSAI administrable.

Files:

```text
kernel/dev/virtio/virtio_net.c
kernel/net/eth.c
kernel/net/arp.c
kernel/net/ipv4.c
kernel/net/icmp.c
kernel/net/udp.c
kernel/net/tcp_min.c
userspace/osctl/
userspace/sshd/
```

Implementation order:

1. Virtio-net RX/TX.
2. Ethernet frame parser.
3. ARP request and reply.
4. IPv4 checksums and dispatch.
5. ICMP echo reply.
6. UDP send and receive.
7. Minimal TCP for admin protocol.
8. Static IP first, DHCP later.
9. `osctl` over TCP during development.
10. SSH service with public-key auth only.

SSH can be delivered by porting a small SSH server into userspace once the libc, random, file, and socket subset is sufficient. SSH crypto must not live in the kernel.

Acceptance criteria:

```text
QEMU guest answers ping
UDP echo test passes
TCP admin command returns osctl status
SSH public-key login works through host port forwarding
password login is impossible
```

## 16. Phase 9: Service Manager and AI Cell Manifests

Goal: start services from explicit resource manifests.

Files:

```text
userspace/init/
userspace/osctl/
userspace/services/
runtime/include/osai/cell.h
runtime/src/cell.c
kernel/sched/cell.c
kernel/cap/cap.c
```

Manifest fields:

```text
service.name
service.binary
cores.network
cores.inference
cores.source_index
cores.build_test
cores.migration
cores.preemption
memory.model_arena
memory.kv_arena
memory.hugepages
memory.prefault
memory.swap
memory.numa
network.ports
network.latency_profile
git.repository
git.allow_commit
git.allow_push
```

Cell invariants:

```text
leased AI cores run only the owning Cell
unauthorized ports cannot be bound
unauthorized devices cannot be opened
memory outside the contract cannot be mapped
migration is forbidden unless the manifest permits it
```

Acceptance criteria:

```text
osctl services lists services
osctl start demo-agent starts a Cell
osctl cell demo-agent shows fixed cores
osctl cores shows leases
migration counter remains zero
```

## 17. Phase 10: AI Memory Architecture

Goal: implement memory primitives for CPU-bound AI models.

Files:

```text
kernel/mm/arena.c
kernel/mm/hugepage.c
kernel/mm/model_map.c
kernel/mm/dma.c
runtime/include/osai/mem.h
runtime/src/mem.c
ai/runtime/model_arena.c
ai/runtime/kv_cache.c
```

Required arenas:

```text
W_ARENA: shared read-only model weights
KV_ARENA: private per-agent KV/cache
SRC_ARENA: source index and symbols
BUILD_ARENA: compiler and test output
NET_ARENA: network buffers
LOG_ARENA: telemetry and logs
```

Required behavior:

- Shared model weights are physically shared across Cells.
- KV/cache state is private per Cell.
- Hugepage mappings are preferred for model weights.
- Model and KV arenas are prefaulted during warmup.
- Swap is forbidden for AI memory.
- Page faults after service READY are fatal by default.

Acceptance criteria:

```text
two Cells map one shared model file
write attempt to shared model weights faults
KV arenas are private
prefault test reports zero post-READY page faults
```

## 18. Phase 11: CPU AI Runtime MVP

Goal: run a tiny CPU-only model for correctness, then scale to practical GGUF-style models on real hardware.

Files:

```text
ai/runtime/
ai/gguf/
ai/tokenizer/
ai/kernels/scalar/
ai/kernels/x86_64/
ai/kernels/aarch64/
userspace/services/demo_agent/
```

Implementation order:

1. Tiny test model format for correctness.
2. Scalar matmul and attention kernels.
3. Tokenizer test harness.
4. GGUF metadata parser.
5. Shared model mapping integration.
6. Private KV/cache integration.
7. x86-64 AVX2 path.
8. x86-64 AVX-512 path where available.
9. Xeon AMX-aware path where available.
10. AArch64 NEON path.
11. AArch64 SVE or SVE2 path where available.

Runtime API shape:

```text
osai_model_open
osai_model_share
osai_session_create
osai_model_decode
osai_model_next_token
osai_session_destroy
osai_model_close
```

QEMU acceptance criteria:

```text
tiny model loads
tokenizer test passes
fixed prompt produces deterministic output
shared model mapping is used
KV bounds tests pass
```

Real hardware criteria:

```text
tokens per second measured
time to first token measured
inter-token p99 measured
memory bandwidth measured
post-READY page faults are zero
core migrations are zero
```

## 19. Phase 12: Low-Latency netq Networking

Goal: implement the native low-latency network path for AI commands and token streaming.

Files:

```text
kernel/net/netq.c
runtime/include/osai/netq.h
runtime/src/netq.c
kernel/dev/net/
bench/tcp_latency/
bench/udp_latency/
```

Rules:

```text
one RX/TX queue pair per network worker
stable flow-to-core mapping
no global accept queue in low-latency path
no generic socket buffer in low-latency path
no dynamic allocation per packet
polling while active
interrupt fallback only while idle
copy small payloads
zero-copy large payloads
```

Hardware order:

```text
QEMU: virtio-net correctness
Intel Desktop: Intel I210/I211 or I225/I226 class NICs
Intel Xeon: Intel E810 or similar server NIC with MSI-X/RSS
ARM: normal PCIe NIC or platform NIC available on the board
```

Acceptance criteria:

```text
UDP echo latency benchmark runs
TCP request/response benchmark runs
token-stream benchmark runs
network latency under model load is measured
network latency under build load is measured
packet handler core is reported
flow migration count is zero
```

## 20. Phase 13: Source-Code Agent Runtime

Goal: support AI agents that safely modify their application source tree.

Files:

```text
ai/source_index/
ai/git_agent/
userspace/services/agentd/
userspace/services/indexd/
userspace/services/buildd/
userspace/services/gitd/
userspace/services/patchd/
runtime/include/osai/agent.h
```

Services:

```text
agentd: app-local AI command loop
indexd: source-code index and symbol map
buildd: isolated builds and tests
gitd: worktrees, diffs, commits, sync
patchd: patch application, policy checks, rollback
```

Workflow:

```text
receive request
load source context
generate patch
apply patch to disposable worktree
compile
run tests
prepare diff
commit if allowed
hot reload or redeploy
retain rollback point
```

Security rules:

```text
no remote Git push unless allowed
no service manifest edit unless allowed
no capability changes by the agent
no network access during build by default
no writing outside assigned repository
```

Acceptance criteria:

```text
toy app can be modified by prompt
patch diff is generated
build passes
tests pass
forbidden path edit is rejected
failed test rolls back
```

## 21. Phase 14: Intel Desktop Port

Goal: boot and measure OSAI on modern Intel Desktop hardware.

Implementation tasks:

1. Real UEFI boot.
2. Early serial or fallback logging.
3. ACPI MADT parsing.
4. APIC or x2APIC support.
5. TSC calibration and invariant TSC detection.
6. PCIe enumeration.
7. NVMe support.
8. Intel desktop NIC support.
9. P-core/E-core topology classification where available.
10. Core role policy for P-cores and E-cores.
11. C-state and performance policy where safely available.
12. Hugepage model arenas.
13. x86 vector dispatch.
14. Benchmark suite execution.

Acceptance criteria:

```text
system boots from USB or NVMe
SSH admin works
demo AI agent runs
AI core migrations are zero
post-READY page faults are zero
TCP and UDP latency benchmarks run
RAM latency and bandwidth benchmarks run
model decode benchmark runs
```

## 22. Phase 15: Intel Xeon Port

Goal: build the serious multi-agent CPU-AI server path.

Implementation tasks:

1. Server UEFI boot.
2. ACPI SRAT and SLIT parsing.
3. NUMA-local physical allocator.
4. NUMA-local hugepage pools.
5. Per-node model-weight policy.
6. Optional model replication across NUMA nodes.
7. MSI-X interrupt routing.
8. VT-d IOMMU support for queue ownership.
9. Server NIC driver path.
10. NVMe multi-queue path.
11. Xeon vector and AMX feature detection where available.
12. Extended CPU state handling without hot-path context switching.
13. Multi-cell admission control.
14. Tuned Linux/BSD benchmark comparison.

Acceptance criteria:

```text
multiple AI Cells run concurrently
NUMA-local allocation is proven
server NIC queues are owned per Cell
IOMMU protects direct queue access
shared model weights work across Cells
per-node bandwidth is reported
OSAI produces comparison report versus tuned Linux/BSD
```

## 23. Phase 16: ARM / NVIDIA N1X-Class Port

Goal: port OSAI to AArch64 CPU-only execution.

Implementation tasks:

1. AArch64 UEFI loader.
2. AArch64 kernel entry.
3. Exception vectors.
4. MMU page tables.
5. Generic timer.
6. PSCI CPU bring-up.
7. GICv3 interrupt controller.
8. SMMU support where available.
9. ACPI/SBSA path first.
10. Device Tree fallback later.
11. AArch64 memory barriers and cache maintenance.
12. NEON model kernels.
13. SVE/SVE2 dispatch where available.
14. Core cluster discovery and no-migration policy.
15. netq path on available NIC.

Compatibility definition:

```text
AArch64 UEFI boot works
CPU topology is discovered
performance cores can be leased
generic timer works
GIC interrupts work
PCIe or platform NIC works
CPU-only AI runtime works
netq works
no GPU runtime is required
```

Acceptance criteria:

```text
AArch64 QEMU boots first
real ARM/N1X-class system boots when available
CPU-only demo AI agent runs
cluster migration count is zero
post-READY page faults are zero
benchmarks run
```

## 24. Phase 17: Persistent Storage and System Image

Goal: make OSAI installable and recoverable.

Initial image layout:

```text
p1 EFI System Partition
p2 OSAI system slot A
p3 OSAI system slot B
p4 OSAI data
p5 OSAI recovery/config
```

Start with a read-only system image plus writable data partition. Implement KFS only after storage and memory basics are stable.

KFS v1 requirements:

```text
64-bit file sizes
64-bit block addresses
extent allocation
metadata checksums
crash-consistent metadata updates
direct I/O for model files
read-only shared model mapping
fast source-tree directory lookup
```

Acceptance criteria:

```text
USB image boots
A/B update works
rollback works
model files persist
source repositories persist
crash logs persist
```

## 25. Phase 18: Security and Agent Safety

Goal: make AI-generated code changes safe.

Required features:

```text
W^X memory
kernel/user isolation
per-Cell capabilities
IOMMU or SMMU for direct queues
signed service packages
SSH public-key auth only
read-only system partition
rollback of service deployments
build/test sandbox isolation
Git operation policy
```

Capabilities:

```text
agent.repo.read
agent.repo.patch
agent.repo.commit
agent.repo.push
agent.build.run
agent.test.run
agent.service.reload
agent.net.bind
agent.model.use
```

Acceptance criteria:

```text
agent cannot write forbidden path
agent cannot bind unauthorized port
agent cannot push without permission
failed build does not alter running service
rollback restores previous version
```

## 26. Phase 19: Benchmarks

Goal: prove or disprove OSAI targets.

Benchmark categories:

```text
RAM latency
RAM bandwidth
TCP latency
UDP latency
CPU-core residency
scheduler jitter
page-fault absence
model decode
agent command-to-patch latency
agent patch-to-running latency
```

RAM latency tests:

```text
single-core local arena
multi-core local arena
shared model arena
KV arena
source-index arena
NUMA-local Xeon arena
NUMA-remote Xeon arena
```

RAM bandwidth tests:

```text
sequential read
sequential write
copy
triad
quantized weight scan
KV-cache scan
mixed source-index plus model decode
```

Network tests:

```text
64-byte UDP echo
256-byte UDP echo
1 KiB UDP echo
64-byte TCP request response
token-stream TCP response
AI decode plus TCP command
build/test load plus TCP command
```

Hot-core counters:

```text
core_migrations
involuntary_context_switches
timer_ticks_on_hot_core
unexpected_interrupts_on_hot_core
unexpected_IPIs
post_ready_page_faults
frequency changes
thermal throttling events
SMT sibling contention events
```

Hot AI core targets:

```text
core_migrations = 0
involuntary_context_switches = 0
post_ready_page_faults = 0
timer_ticks_on_hot_core = 0 or explicitly accounted
unexpected_interrupts_on_hot_core = 0 except machine or thermal events
```

Baselines:

```text
stock Linux or BSD
tuned Linux or BSD
tuned Linux with CPU pinning and isolation
tuned Linux with AF_XDP or DPDK where relevant
tuned BSD or netmap where relevant
```

OSAI claims only count against tuned baselines.

## 27. 100% Completion Criteria

OSAI reaches 100% for this roadmap when:

1. QEMU on macOS boots and runs the functional suite.
2. Intel Desktop boots and demonstrates CPU-AI latency or jitter gains.
3. Intel Xeon boots and runs multiple isolated AI Cells.
4. ARM/N1X-class hardware boots CPU-only and runs AI Cells.
5. SSH administration works.
6. Service manifests declare cores, memory, network queues, model mappings, source repo, build/test sandbox, and Git policy.
7. Shared model weights work.
8. Private KV/cache arenas work.
9. Source-code indexing works.
10. Patch/build/test/Git workflow works in a sandbox.
11. Hot AI cores show zero migrations.
12. Hot AI cores show zero unintended context switches.
13. Hot AI cores show zero post-READY page faults.
14. TCP/UDP latency benchmarks run under AI and build load.
15. RAM benchmarks run.
16. Model decode benchmarks run.
17. Tuned Linux/BSD reports are generated.
18. The project honestly reports whether the target ranges were achieved.

## 28. First Ten Codex Tasks

### Task 001: Build Skeleton

Files:

```text
Makefile
scripts/macos-bootstrap.sh
scripts/test.sh
scripts/check-tree.sh
```

Goal: `make test` works on macOS and validates host tools.

### Task 002: UEFI Hello Loader

Files:

```text
boot/uefi/loader_main.c
boot/uefi/include/uefi_min.h
scripts/build-image.sh
scripts/run-qemu-x86_64.sh
```

Goal: QEMU prints `OSAI loader starting` to serial.

### Task 003: Split Loader and Kernel ELF

Files:

```text
boot/uefi/loader_main.c
boot/uefi/boot_info.h
kernel/core/kmain.c
```

Goal: UEFI loader loads `kernel.elf` and jumps to kernel entry.

### Task 004: Parse Memory Map and PMM

Files:

```text
kernel/mm/pmm.c
kernel/core/klog.c
kernel/core/panic.c
```

Goal: kernel reports total/free/reserved memory and passes allocate/free tests.

### Task 005: Exception Diagnostics

Files:

```text
kernel/arch/x86_64/gdt.c
kernel/arch/x86_64/idt.c
kernel/arch/x86_64/traps.c
```

Goal: controlled page fault prints useful diagnostic.

### Task 006: SMP Registry

Files:

```text
kernel/arch/x86_64/smp.c
kernel/core/per_cpu.c
kernel/sched/core_lease.c
```

Goal: QEMU with 4 CPUs brings up all cores and leases CPU 1 to a fixed loop.

### Task 007: Minimal User Init

Files:

```text
kernel/user/elf.c
kernel/user/syscall.c
userspace/init/main.c
```

Goal: `/init` runs in user mode and logs through `SYS_LOG_WRITE`.

### Task 008: Virtio-Net ICMP

Files:

```text
kernel/dev/virtio/virtio_net.c
kernel/net/eth.c
kernel/net/arp.c
kernel/net/ipv4.c
kernel/net/icmp.c
```

Goal: guest answers ping in QEMU.

### Task 009: AI Cell Manifest MVP

Files:

```text
kernel/sched/cell.c
userspace/osctl/
runtime/include/osai/cell.h
```

Goal: `osctl` starts demo Cell with fixed cores and reports zero migrations.

### Task 010: Shared Model Arena MVP

Files:

```text
kernel/mm/model_map.c
ai/runtime/model_arena.c
```

Goal: two demo Cells map one shared read-only model file and private KV arenas.

## 29. Implementation Biases

When in doubt, choose:

```text
correctness before performance
QEMU functionality before hardware performance
Intel Desktop before Xeon complexity
Xeon before ARM SoC complexity
explicit ownership before convenience
determinism before throughput
shared read-only model weights before per-app copies
small TCP/UDP latency before bulk network bandwidth
simple filesystem before clever filesystem
CPU-only AI before accelerator support
```

## 30. Final Rule

Do not accidentally build a generic OS and try to tune it later.

The center of OSAI is:

```text
AI Cell = fixed cores + fixed memory + fixed network queues + fixed source/build/Git context
```

Any implementation that creates hidden migration, hidden context switching, hidden page faults, hidden model duplication, hidden queue sharing, or hidden background work on AI hot cores is wrong for OSAI.
