# QEMU Full OS Plan

The milestone 1-33 track is a QEMU readiness gate, not the end of the QEMU OS.
OSAI should stay on QEMU until the core OS behavior is implemented deeply enough
to justify Intel Desktop bring-up.

## Current Status

The current QEMU image boots through AArch64 UEFI, initializes kernel memory,
drivers, telemetry, `/init`, security fixtures, persistence metadata, AI Cell
runtime fixtures, and correctness benchmark/preview gates.

The current implementation still has important MVP/stub areas:

- single foreground EL0 `/init` path;
- kernel-hosted service policy;
- metadata-only persistence;
- correctness-only network paths;
- deterministic CPU-AI runtime stub;
- no real filesystem mutation or disk-backed rollback;
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
- Only then start Intel Desktop code.

## Current Slice

The active engineering slice is real process lifecycle under the full-core
workdown. The detailed checklist lives in `QEMU-FULL-OS-CORE-WORKDOWN.md`.
