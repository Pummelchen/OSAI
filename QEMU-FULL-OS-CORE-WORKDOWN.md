# QEMU Full OS Core Workdown

This is the execution checklist for moving OSAI from the current QEMU readiness
gate toward a real QEMU full OS core. The QEMU hardware-readiness gate remains
useful, but it is not the same as a complete OS.

## Completion Rule

Do not start Intel hardware code until this QEMU workdown has a release
candidate gate with stable ABI, telemetry, filesystem, process, security, and
control-plane contracts.

## Phase Q1: Real Process Lifecycle

Goal: stop treating `/init` as a one-way kernel-hosted test and make EL0
process execution return cleanly to kernel supervision.

Required work:

- [x] Add process states: empty, loaded, running, exited, failed.
- [x] Count process lifecycle transitions.
- [x] Make `sys_exit` mark the current process exited or failed.
- [x] Return cleanly from EL0 exit to a kernel continuation.
- [x] Emit process lifecycle telemetry.
- [x] Add child service descriptors under `/init`.
- [x] Allow `/init` to supervise at least one child-style service record.
- [x] Add process table tests for invalid transitions and failed exits.

Definition of done:

- QEMU boot proves `/init` loads, runs, exits, and the kernel resumes after EL0.
- Telemetry includes loaded, running, exited, failed, and transition counters.
- Smoke and benchmark gates fail if process lifecycle telemetry disappears.
- Smoke and benchmark gates fail if `/init` no longer defines, starts, and
  reports the child `/svc/source-index` service record.

## Phase Q2: Real Userspace Control Plane

Goal: move policy decisions out of kernel fixtures and into userland service
management.

Required work:

- [x] Split userland into `/init` and a service-manager payload.
- [x] Add a service descriptor format in the read-only filesystem.
- [x] Add syscalls for status, start, stop, restart, rollback, and update.
- [x] Keep kernel APIs as capability-checked primitives.
- [x] Add denied-operation tests for missing capabilities.
- [x] Emit userspace control-plane state telemetry.

Definition of done:

- `/init` loads service-manager policy from filesystem data.
- Service-manager commands are issued from userland, not kernel self-tests.
- Missing capabilities fail closed.
- QEMU boot proves `/bin/service-manager` reads the ROFS service descriptor,
  manages `/svc/source-index`, and emits control-plane telemetry.

## Phase Q3: Real Filesystem and Persistence

Goal: replace metadata-only persistence with serialized state records and
mutable VirtIO-backed state.

Required work:

- [x] Add a mutable state region to the VirtIO test block image.
- [x] Serialize boot, service, workspace, sandbox, and update records.
- [x] Add checksums/versioning for persistence records.
- [x] Reload persisted records after a QEMU reboot.
- [x] Prove rollback restores prior service/workspace state.
- [x] Keep read-only system image and mutable state boundaries explicit.

Definition of done:

- A two-boot QEMU test proves state survives reboot and rollback works.
- `make qemu-persistence-reboot` proves the same VirtIO state image is reused
  across two QEMU boots and reports `persistence_boot_loads`.
- The QEMU matrix includes persistence reboot validation.

## Phase Q4: Networking Depth

Goal: move beyond parser smoke paths into queue-backed packet flow.

Required work:

- [x] Add queue-backed RX/TX packet ownership.
- [x] Add UDP flow table with per-core ownership.
- [x] Add minimal TCP state machine and timeouts.
- [x] Add packet lifetime/drop counters.
- [x] Preserve malformed packet tests.
- [x] Emit p50/p95/p99/p999 correctness telemetry.

Definition of done:

- QEMU network smoke proves packets move through queue metadata, not only
  parser fixtures.
- `make qemu-smoke` requires queue-backed packet lifecycle telemetry,
  UDP flow ownership, TCP timeout accounting, and p999 latency fields.

## Phase Q5: CPU-Only AI Runtime Depth

Goal: turn the deterministic CPU-AI stub into real boundaries that can later
host actual CPU-only model code.

Required work:

- [x] Add model-loader interface and manifest format.
- [x] Add tokenizer/runtime boundary.
- [x] Keep shared read-only model weights.
- [x] Keep private KV/cache arenas per AI Cell.
- [x] Add multi-cell shared-weight tests.
- [x] Add model load failure tests.
- [x] Keep GPU acceleration out of the runtime contract.

Definition of done:

- AI Cells can bind a model manifest, shared weights, tokenizer boundary, and
  private KV/cache arena through enforceable resource contracts.
- `make qemu-smoke` requires model load, load failure, tokenizer, runtime,
  KV/cache write, shared-weight bind, and GPU rejection telemetry.

## Phase Q6: Security Enforcement

Goal: make capability policy enforce service, filesystem, Git/workspace, update,
rollback, and build/test permissions.

Required work:

- [x] Extend capability checks across service operations.
- [x] Enforce filesystem read/write boundaries.
- [x] Enforce Git workspace write and patch permissions.
- [x] Enforce build/test sandbox permissions.
- [x] Strengthen signed update validation beyond the current stub.
- [x] Add rollback authorization tests.
- [x] Ensure secrets are rejected from logs, updates, and benchmark records.

Definition of done:

- Denied operations fail closed and increment telemetry counters.
- `make qemu-smoke` requires capability, filesystem, workspace, sandbox,
  rollback, update-policy, credential, and signature rejection telemetry.
- `make qemu-readiness-gate` validates the Q6 security enforcement counters as
  part of the QEMU correctness benchmark.

## Phase Q7: QEMU Release Candidate

Goal: freeze the QEMU full OS core contract.

Required work:

- [x] Expand `make qemu-readiness-gate` to include CPU matrix tiers.
- [x] Freeze syscall ABI.
- [x] Freeze telemetry schema.
- [x] Freeze filesystem format.
- [x] Freeze persistence record format.
- [x] Freeze service descriptor format.
- [x] Document what remains out of scope for Intel bring-up.

Definition of done:

- A single QEMU release-candidate gate proves process, userspace control,
  persistence, networking, CPU-AI runtime, security, and documentation
  contracts are stable enough to start Intel Desktop work.
- The frozen contract lives at `contracts/qemu-rc-v1.json`.
- The readiness gate writes and validates `build/qemu-cpu-matrix-report.json`.
