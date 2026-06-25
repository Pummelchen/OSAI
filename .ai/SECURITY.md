<!--
AI onboarding file.
Mode: bootstrap
Indexed commit: 8458ff956831e1b3b44a0cbcb396352ce28e3a01
Last generated: 2026-06-25T09:20:22Z
Generator: generic high-end AI coding agent
Purpose: Help future AI sessions understand this repository quickly.
Audience: Any high-capability AI coding agent, regardless of vendor or model family.
Human edits are allowed. Future refreshes should preserve valid human edits.
-->
# Security notes for AI agents

## Verified security surfaces

| Area | Files | Notes |
|---|---|---|
| Capability model | `kernel/include/xaios/syscall.h`, `kernel/user/syscall.c` | Syscalls map to required capabilities. |
| Credential-material rejection | `kernel/runtime/security.c` | Rejects selected credential/private-key/password/token/secret patterns in inputs and log buffers. |
| Filesystem policy | `kernel/runtime/security.c`, `kernel/fs/` | Read/write paths are policy constrained. |
| Sandbox/Git workspace | `kernel/runtime/sandbox.c`, `kernel/runtime/git_workspace.c`, `kernel/runtime/security.c` | Ownership and path checks. |
| Update/rollback | `kernel/runtime/update.c`, `kernel/runtime/security.c` | Admin/update capability and dev-mode signature format checks. |
| SSH/SFTP | `userspace/sshd/` | Remote login and file-transfer surface. |
| QEMU host forwarding | `scripts/run-qemu-aarch64.sh` | Default host forwarding maps host port 2222 to guest port 22 unless disabled. |

## Rules for AI agents

- Never commit credentials, API keys, private keys, tokens, passwords, secret benchmark data, or host-specific auth material.
- Do not weaken capability checks, user-buffer validation, credential-material rejection, sandbox path validation, update authorization, or admin gating without explicit human approval.
- Treat comments in `security_validate_update_signature()` as authoritative for current source: QEMU dev-mode signature validation is format/generation validation, not production cryptographic verification.
- Do not claim the SSH/update/security model is production-ready solely from docs; inspect current source and tests.
- Do not expose local host usernames, paths, SSH keys, or environment values in docs/logs.

## Sensitive implementation details

- `kernel/runtime/security.c` includes counters for denied operations, capability denials, filesystem denials, workspace/sandbox denials, rollback denials, update policy rejects, signature/key accepts/rejects, credential rejects, admin denials, and sandbox escape rejects.
- `kernel/user/syscall.c` validates syscall numbers, capabilities, and selected user buffers.
- `userspace/sshd/` is security-sensitive because it implements remote access behavior and SFTP.
- `scripts/run-qemu-aarch64.sh` may expose local service ports through QEMU user networking.

## Validation

For security-sensitive changes, run the narrowest relevant gate plus smoke:

```sh
make compile-check
make qemu-security-gate
make qemu-smoke
```

For update or filesystem changes, also consider:

```sh
make qemu-update-gate
make qemu-filesystem-gate
make qemu-readiness-gate
```

Record any skipped gate with a concrete reason.
