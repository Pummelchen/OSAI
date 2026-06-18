# QEMU Full OS Plan

The milestone 1-33 track is a QEMU readiness gate, not the end of the QEMU OS.
XAIOS should stay on QEMU until the core OS behavior is implemented deeply enough
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
The network-maturity slice adds deterministic flow-to-queue routing, queue ring
accounting, UDP flow hit/expiry handling, TCP retransmit-before-timeout policy,
and telemetry gates for queue backpressure and flow/core mismatches.
The CPU-only model-runtime slice adds a VirtIO ROFS-backed model file,
manifest/checksum validation, tokenizer-table binding, CPU dispatch telemetry,
and admission checks for malformed, GPU-required, and undersized private
KV/cache configurations.
The AI Cell production-contract slice adds a stable descriptor ABI, descriptor
checksum validation, required resource flags, PMM/VMM-backed arena accounting,
real NIC queue binding and release, workspace lifecycle accounting, conflict
tests, and readiness-gate telemetry for the AI Cell resource contract.
The security-hardening slice adds an explicit admin capability, stricter
signed-update format with development public-key id, monotonic update generation
and replay rejection, sandbox path escape rejection, and QEMU gates for the
new policy counters.
The update-and-rollback slice adds an update transaction runtime, persisted
mutable filesystem update records, persistence rollback points, failed staged
update recovery through boot fallback, committed update rollback, and readiness
gates for the update lifecycle telemetry.
The final Milestone 42 QEMU full OS release candidate slice adds
`make qemu-full-os-rc`,
which runs the full readiness gate, validates the frozen contract against source
ABI definitions, checks all generated QEMU reports, and writes
`build/qemu-full-os-rc-report.json` with `qemu_full_os_complete=true` only when
milestone 42 is complete.

The current implementation still has important MVP/stub areas:

- two foreground EL0 process path (`/init` and `/bin/service-manager`);
- userspace-driven service policy with QEMU supervisor and admin coverage;
- persistence records plus a mutable filesystem layer;
- QEMU-matured network paths with queue ownership and TCP/UDP lifecycle gates;
- QEMU CPU-only deterministic model format, with production model runtimes still
  out of scope;
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

### Phase Q9: Full OS Release Candidate Gate

- Run `make qemu-full-os-rc`.
- Re-run the complete QEMU readiness matrix from the final gate.
- Validate `contracts/qemu-rc-v1.json` against the source syscall and
  capability ABI.
- Validate benchmark, preview, readiness, and CPU matrix artifacts.
- Emit `build/qemu-full-os-rc-report.json` with schema
  `xaios.qemu.full_os_release_candidate.v1`.
- Treat `qemu_full_os_complete=true` as the local gate that allows Intel
  Desktop planning to move into implementation.

### Phase Q10: Post-51 QEMU Hardening

- Run a dedicated QEMU regression suite for process lifecycle, filesystem
  rollback, AI Cell conflicts, network state, security denials, and telemetry.
- Run controlled fault injection beyond the normal smoke path.
- Validate ABI and format contracts directly against source and generator code.
- Run repeated QEMU boots to catch nondeterministic boot-state drift.
- Gate userspace control-plane behavior, network-stack behavior, and CPU-only
  AI runtime simulator behavior as separate reports.
- Validate developer UX targets and documentation markers.
- Keep this phase on macOS/QEMU until physical hardware is available.

## Current Slice

Milestones 34 through 42 are complete in QEMU when `make qemu-full-os-rc`
passes. Milestones 52 through 59 are complete when `make qemu-post51-gate`
passes. The detailed checklist lives in `QEMU-FULL-OS-CORE-WORKDOWN.md`.
Physical Intel Desktop, Xeon, AMD, and ARM/NVIDIA hardware work remains a later
phase.

## Milestones 52-59

| Milestone | Gate | Report |
|---:|---|---|
| Milestone 52 | `make qemu-regression-suite` | `build/qemu-milestone-52-regression-suite.json` |
| 53 | `make qemu-fault-injection` | `build/qemu-milestone-53-fault-injection.json` |
| 54 | `make qemu-abi-contract` | `build/qemu-milestone-54-abi-contract.json` |
| 55 | `make qemu-boot-loop` | `build/qemu-milestone-55-boot-loop.json` |
| 56 | `make qemu-userspace-suite` | `build/qemu-milestone-56-userspace-suite.json` |
| 57 | `make qemu-network-suite` | `build/qemu-milestone-57-network-suite.json` |
| 58 | `make qemu-cpu-ai-suite` | `build/qemu-milestone-58-cpu-ai-suite.json` |
| Milestone 59 | `make qemu-developer-ux` | `build/qemu-milestone-59-developer-ux.json` |

The aggregate post-51 gate is `make qemu-post51-gate`, which writes
`build/qemu-post51-gate-report.json`.
