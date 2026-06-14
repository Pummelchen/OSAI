# QEMU Full OS Plan

The milestone 1-33 track is a QEMU readiness gate, not the end of the QEMU OS.
OSAI should stay on QEMU until the core OS behavior is implemented deeply enough
to justify Intel Desktop bring-up.

## Current Status

The current QEMU image boots through AArch64 UEFI, initializes kernel memory,
drivers, telemetry, `/init`, security fixtures, persistence metadata, AI Cell
runtime fixtures, and correctness benchmark/preview gates.
The QEMU release-candidate contract is frozen in
`contracts/qemu-rc-v1.json`.
The full-OS filesystem slice adds a VirtIO-backed mutable filesystem region
with checksum-protected metadata, read-write mount policy, directory records,
allocator-backed multi-sector files, journal replay, create/update/read/delete
operations, and commit/rollback self-tests.
The process-supervisor slice adds a parent-child service tree, child restart
policy enforcement, supervised crash handling, service cleanup, service log
accounting, and process address-space reclaim telemetry.

The current implementation still has important MVP/stub areas:

- two foreground EL0 process path (`/init` and `/bin/service-manager`);
- userspace-driven service policy with QEMU supervisor coverage;
- persistence records plus a mutable filesystem layer;
- correctness-only network paths;
- deterministic CPU-AI runtime stub;
- no POSIX filesystem surface yet;
- no real multi-process scheduling.

## Full QEMU OS Phases

### Phase Q1: Kernel Fault and Protection Gate

- Build and boot controlled page fault, read-only write fault, and NX execute
  fault images.
- Keep those fault modes in the standard QEMU matrix.
- Preserve clean normal-image rebuild after fault testing.

### Phase Q2: Process Lifecycle

- Track process states: empty, loaded, running, exited, failed.
- Support kernel return from EL0 exit without permanently parking the kernel.
- Add process table counters and lifecycle telemetry.
- Allow `/init` to supervise at least one child-style service descriptor.

### Phase Q3: Userspace Control Plane

- Move service-manager decisions into a real userspace control-plane binary.
- Keep kernel service APIs as capability-checked primitives.
- Add restart, rollback, update, and status as userspace-driven flows.

### Phase Q4: Filesystem and Persistence

- Replace metadata-only persistence with serialized snapshot records.
- Add disk-backed state in the VirtIO block image.
- Add rollback validation after reboot.

### Phase Q5: Networking Stack Depth

- Replace parser smoke paths with queue-backed packet processing.
- Add UDP flow table and TCP state/timeouts.
- Keep per-core ownership and telemetry.

### Phase Q6: CPU-AI Runtime Depth

- Add real tokenizer/model-loader boundaries.
- Keep CPU-only execution and shared model arenas.
- Add multi-cell shared-weight and private KV/cache tests.

### Phase Q7: QEMU Release Candidate

- Run smoke, fault matrix, benchmark, preview, and documentation checks.
- Freeze the QEMU ABI and telemetry schema.
- Freeze the read-only filesystem, persistence, and service descriptor formats.
- Include ARM64 CPU boot-probe tiers and Intel/AMD x86_64 command-profile tiers
  in the readiness gate.
- Document what remains out of scope before Intel Desktop bring-up.
- Only then start Intel Desktop code.

### Phase Q8: Full Filesystem

- Add a separate mutable filesystem region on the VirtIO block image.
- Keep the read-only boot filesystem and mutable state area explicitly
  separated.
- Validate metadata checksums, mount policy, allocation, directory traversal,
  multi-sector files, file mutation, delete behavior, journal replay, and
  commit/rollback boundaries.
- Extend QEMU telemetry and correctness gates for mutable filesystem counters.
- Keep this as the QEMU filesystem contract until later production work adds a
  POSIX-like surface, larger persistent state area, and hardware-specific
  storage tuning.

## Current Slice

Milestones 34 and 35 are complete in QEMU. The next active slice starts at
milestone 36 under the full-core workdown. The detailed checklist lives in
`QEMU-FULL-OS-CORE-WORKDOWN.md`.
