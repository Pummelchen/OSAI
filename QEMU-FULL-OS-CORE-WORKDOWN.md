# QEMU Full OS Core Workdown

This is the execution checklist for moving XAIOS from the current QEMU readiness
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

## Phase Q8: Full Filesystem

Goal: move beyond the read-only boot filesystem and single-sector persistence
records toward a real mutable filesystem area.

Required work:

- [x] Reserve a separate VirtIO-backed mutable filesystem sector range.
- [x] Add checksum-protected filesystem metadata.
- [x] Add explicit read-write mount policy.
- [x] Add create/update/read/delete file operations.
- [x] Add commit and rollback boundaries for mutable files.
- [x] Add QEMU smoke and benchmark gates for mutable filesystem telemetry.
- [x] Add a real block allocator instead of fixed one-sector file slots.
- [x] Add directory records and path traversal beyond fixed prefixes.
- [x] Add larger files spanning multiple sectors.
- [x] Add crash-consistency transaction replay.
- [x] Connect service, workspace, and update state to the mutable filesystem.

Definition of done:

- QEMU proves mutable filesystem mount, mutation, checksum validation,
  allocation, directory records, multi-sector files, journal replay, delete
  behavior, reboot load, and rollback behavior without weakening the read-only
  boot filesystem boundary.
- A later production-ready filesystem can add a POSIX-like surface and larger
  storage area, but the QEMU contract no longer depends on fixed one-sector
  file slots.

## Phase Q9: Process Supervisor

Goal: make the service manager behave like a small supervisor instead of a
single child-service smoke test.

Required work:

- [x] Track parent-child service tree edges.
- [x] Store child parent metadata in the service record.
- [x] Allow service configuration to apply to child services, not only `/init`.
- [x] Enforce child restart policy and `max_restarts`.
- [x] Add a controlled child crash path through the userspace service manager.
- [x] Restart a failed child when policy allows it.
- [x] Clean up service runtime state after exit or crash.
- [x] Count service logs globally and per service.
- [x] Count process address-space reclamation after EL0 process exit.
- [x] Add smoke, benchmark, readiness, and contract gates for supervisor
  telemetry.

Definition of done:

- `/bin/service-manager` defines `/svc/source-index` as a child of `/init`,
  starts it, proves restart denial while policy is `never`, reconfigures it to
  `always`, logs a service event, injects a controlled crash, and observes the
  supervisor cleanup/restart path.
- Telemetry includes `service_tree_edges`, `service_restarts`,
  `service_crashes`, `service_cleanups`, `service_log_records`, and
  `user_process_reclaims`.
- `make qemu-readiness-gate` requires the service-supervisor counters and still
  requires zero user process failures.

## Phase Q10: Network Stack Maturity

Goal: turn the QEMU network path from a queue-backed smoke fixture into a
stricter TCP/UDP lifecycle contract.

Required work:

- [x] Route packets through deterministic flow-to-queue selection.
- [x] Keep established UDP flows pinned to their owning queue and cell.
- [x] Add explicit RX/TX/completion accounting for queue rings.
- [x] Add UDP flow hit tracking.
- [x] Add UDP idle expiry.
- [x] Add TCP established/closed counters.
- [x] Add TCP retransmit-before-timeout policy for SYN-received flows.
- [x] Keep TCP timeout behavior after retransmit.
- [x] Add telemetry for queue backpressure drops and flow/core mismatches.
- [x] Expand smoke, benchmark, readiness, and RC contract gates.

Definition of done:

- QEMU boot proves UDP flow reuse, UDP expiry, TCP establishment, TCP
  retransmit, TCP timeout, queue RX/TX/completion accounting, zero queue
  backpressure drops, and zero flow/core mismatches.
- `make qemu-smoke`, `python3 ./scripts/qemu-benchmark.py`, and
  `make qemu-readiness-gate` require the mature networking telemetry.

## Phase Q13: Security Hardening

Goal: move from broad security fixtures to explicit QEMU policy gates for admin,
signed update, key, rollback, sandbox, and secret-handling paths.

Required work:

- [x] Add `XAIOS_CAP_ADMIN` as a separate administrative capability.
- [x] Require both `XAIOS_CAP_UPDATE` and `XAIOS_CAP_ADMIN` for signed update
  authorization.
- [x] Replace the old update token stub with a structured development
  public-key signature format.
- [x] Require monotonic update generations and reject replayed update
  signatures.
- [x] Track accepted and rejected update keys.
- [x] Reject sandbox path traversal and duplicate slash escape attempts.
- [x] Keep rollback authorization fail-closed.
- [x] Emit telemetry for admin denials, update authorization, replay rejection,
  key accept/reject, and sandbox escape rejection.
- [x] Add smoke, benchmark, readiness, and RC contract gates for the hardened
  policy.

Definition of done:

- QEMU boot proves missing admin capability, bad update key, replayed update
  generation, sandbox path escape, credential material, rollback denial, and
  signed update authorization paths are all exercised and counted.
- `make qemu-smoke`, `python3 ./scripts/qemu-benchmark.py`, and
  `make qemu-readiness-gate` require the security-hardening telemetry.

## Phase Q14: Update and Rollback System

Goal: turn signed-update acceptance into an explicit transaction lifecycle with
rollback points, boot fallback, failed-update recovery, and persisted update
state records.

Required work:

- [x] Add a kernel update transaction runtime.
- [x] Persist update transaction records into the mutable filesystem.
- [x] Create persistence rollback points for update transactions.
- [x] Stage signed update transactions after authorization.
- [x] Commit staged update transactions.
- [x] Mark failed staged transactions and recover them through boot fallback.
- [x] Roll back committed update transactions through the same rollback path.
- [x] Emit telemetry for transactions, stages, commits, failures, recoveries,
  rollbacks, boot fallback, persisted records, rollback points, and rejects.
- [x] Add smoke, benchmark, readiness, and RC contract gates for the update
  rollback system.

Definition of done:

- QEMU boot proves two signed update transactions: one staged update fails and
  is recovered through boot fallback, and one staged update commits and is then
  rolled back through an authorized rollback point.
- Update records are serialized under `/state/updates/update.state` with
  generation, state, target, and rollback metadata.
- `make qemu-smoke`, `python3 ./scripts/qemu-benchmark.py`, and
  `make qemu-readiness-gate` require the update rollback telemetry.

## Phase Q15: Admin Control Plane

Goal: make the QEMU admin surface explicit and testable before the final full
OS release-candidate gate.

Required work:

- [x] Grant admin capability only to the EL0 service-manager process.
- [x] Add `admin policy` to report the SSH-only, no-password-login admin
  policy.
- [x] Add `admin status <service>` for service inspection.
- [x] Add `admin export <service>` to persist admin status under
  `/state/services/admin.state`.
- [x] Add `admin logs <service>` for log retrieval metadata.
- [x] Add `admin remote-safe <command>` with an allowlist.
- [x] Reject unsafe remote admin commands.
- [x] Emit admin telemetry for policy exports, status exports, log reads,
  remote-safe accepts/rejects, and command denials.
- [x] Add smoke, benchmark, readiness, and RC contract gates for the admin
  control plane.

Definition of done:

- QEMU boot proves `/bin/service-manager` runs admin policy, service status,
  status export, log retrieval, and remote-safe command checks from EL0.
- Admin status is serialized to `/state/services/admin.state`.
- `python3 ./scripts/qemu-benchmark.py` reports
  `admin_control_plane_active=true`.
- `make qemu-readiness-gate` requires the admin telemetry and contract section.

## Phase Q16: Full OS Release Candidate

Goal: close the QEMU full-OS track with one final release-candidate gate that
can safely unlock Intel Desktop bring-up.

Required work:

- [x] Add `make qemu-full-os-rc` as the final QEMU full OS gate.
- [x] Run the complete `make qemu-readiness-gate` matrix from the final gate.
- [x] Validate the frozen QEMU contract at `contracts/qemu-rc-v1.json`.
- [x] Verify the source syscall/capability ABI matches the frozen contract.
- [x] Validate benchmark, preview, readiness, and CPU matrix artifacts.
- [x] Require all correctness benchmark gates to pass.
- [x] Preserve `performance_claims_allowed=false`.
- [x] Validate release documentation names the final RC gate and report.
- [x] Emit `build/qemu-full-os-rc-report.json` with
  `qemu_full_os_complete=true` only when every RC check passes.

Definition of done:

- `make qemu-full-os-rc` exits 0 and writes
  `build/qemu-full-os-rc-report.json`.
- The report schema is `xaios.qemu.full_os_release_candidate.v1`.
- The report marks milestone 42 complete with `qemu_full_os_complete=true`.
- Intel Desktop bring-up remains blocked unless this final report passes.
