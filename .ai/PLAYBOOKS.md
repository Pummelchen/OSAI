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
# Playbooks

## Add or change a syscall

1. Inspect `kernel/include/xaios/syscall.h`, `kernel/user/syscall.c`, and `userspace/include/xaios_user.h`.
2. Add/adjust capability requirements and request structs.
3. Add userspace wrapper support in `userspace/lib/xaios_user.c` if needed.
4. Update `docs/API.md` and decide whether `contracts/qemu-rc-v1.json` must change.
5. Add/adjust self-tests and QEMU markers.
6. Validate with `make compile-check`, `python3 scripts/qemu-abi-contract.py`, and `make qemu-smoke` where possible.

## Add a userspace app

1. Create `userspace/apps/<name>.c` with `#include <xaios_user.h>`.
2. Add the app to `USER_APPS` in `scripts/build-image.sh`.
3. Launch it from `kernel/core/kmain.c` with the least-privilege capability mask.
4. Add smoke markers to `scripts/qemu-smoke.py` if the output is part of validation.
5. Run `make image && make qemu-smoke`.

## Change kernel subsystem behavior

1. Inspect the subsystem source and matching headers.
2. Check `kmain()` init/self-test order.
3. Maintain or add `*_self_test()` coverage.
4. Update gate markers/reports only if behavior intentionally changes.
5. Validate with `make compile-check` and a relevant QEMU gate.

## Change filesystem/initramfs format

1. Inspect `kernel/fs/`, `scripts/create-initfs.py`, and `contracts/qemu-rc-v1.json`.
2. Keep constants and contract schema aligned.
3. Update ABI/format gate logic if the contract changes.
4. Validate with filesystem gate, ABI contract, and smoke.

## Change AI runtime/model format

1. Inspect `kernel/runtime/cpu_ai_runtime.c`, `kernel/runtime/model_arena.c`, `kernel/runtime/ai_cell.c`, and `tools/convert_gguf_to_xaios.py`.
2. Keep model headers, tokenizer assumptions, quantization IDs, and admission checks aligned.
3. Avoid hardware performance claims without measured baselines.
4. Validate with CPU-AI suite, AI Cell gate, ABI/contract checks, and smoke.

## Change security/update/SSH behavior

1. Inspect `kernel/runtime/security.c`, `kernel/runtime/update.c`, `kernel/user/syscall.c`, and `userspace/sshd/`.
2. Preserve least privilege and credential-material rejection.
3. Do not relax auth/admin/update policy without human approval.
4. Validate with security/update/network gates and smoke.

## Refresh AI onboarding docs

1. Read `.ai/MANIFEST.json` and previous indexed commit.
2. Compare previous commit to current HEAD for high-impact paths.
3. Re-scan source/config/docs touched by build, test, API, architecture, security, deployment, or major docs changes.
4. Preserve correct human edits.
5. Update `AI_INDEX.md`, `AGENTS.md`, `.ai/*`, and README block idempotently.
6. Validate manifest JSON, local links, documentation-only diff, and secret-like patterns.
