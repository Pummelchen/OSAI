#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time


TARGETS = [
    "exceptions: self-test",
    "timer: monotonic self-test passed",
    "smp: per-core registry self-test passed",
    "VMM map/unmap self-test passed",
    "VMM MMIO device mappings installed",
    "arena: self-test passed",
    "sandbox: lifecycle self-test passed",
    "\"sandbox_transitions\":4",
    "source-index: fixture loaded files=2 symbols=2 updates=1",
    "git-workspace: self-test passed",
    "persistence: self-test passed snapshots=4 rollbacks=4 rejects=2",
    "core-lease: isolation self-test passed",
    "virtio-blk: read/error/reset self-test passed",
    "virtio-net: malformed packet/drop self-test passed",
    "virtio-net: rx/tx/reset self-test passed",
    "network: stack initialized",
    "network: udp/tcp path self-test passed",
    "initramfs: config service=/init mode=qemu-mvp",
    "initramfs: child service=/svc/source-index parent=/init restart=never",
    "initramfs: mounted rofs version=2",
    "initramfs: rofs metadata/config self-test passed",
    "syscall: table self-test passed entries=3",
    "user: process table initialized slots=4",
    "user: process lifecycle invalid/failed transition self-test passed",
    "user: process pid=1 name=/init state=loaded",
    "security: self-test passed denied=2 credential_rejects=1 signature_accepts=1 signature_rejects=1",
    "model-arena: shared read-only arena self-test passed",
    "core-lease: owner=0 mask=0x2 acquired",
    "nic-conflict-agent",
    "ai-cell: lifecycle self-test passed",
    "kheap: self-test passed",
    "VMM translation test passed",
    "gic: discovery self-test passed",
    "PMM 1024 page allocate/free test passed",
    "cpu-ai-runtime: deterministic decode fixture input=ABCD output=1B1F2327",
    "cpu-ai-runtime: self-test passed",
    "\"arena_committed_pages\"",
    "\"git_workspace_active\"",
    "\"git_workspace_syncs\"",
    "\"git_workspace_applies\"",
    "\"git_workspace_reverts\"",
    "\"git_workspace_conflicts\"",
    "\"persistence_snapshots\":4",
    "\"persistence_rollbacks\":4",
    "\"persistence_rejects\":2",
    "\"migration_total\":0",
    "\"context_switch_total\":0",
    "\"source_index_updates\":1",
    "\"security_denied_ops\":2",
    "\"security_credential_rejects\":1",
    "\"security_signature_accepts\":1",
    "\"security_signature_rejects\":1",
    "user: loaded /init ELF",
    "user: process pid=1 name=/init state=running",
    "user: rejected syscall=99",
    "user: rejected syscall=1",
    "/init: bad syscall tests passed",
    "/init: hello from ELF",
    "service-manager: configured /init restart=never log=serial max_restarts=0",
    "service-manager: log /init manager-ready records=1",
    "/init: service manager policy ready",
    "service-manager: restart denied /init policy=never attempts=1",
    "/init: restart denied by policy",
    "service: /init state=running",
    "service-manager: defined child /svc/source-index parent=/init restart=never",
    "service: /svc/source-index state=running",
    "osctl: /svc/source-index state=running",
    "/init: child service supervised",
    "/init: service setup complete",
    "rejected=3",
    "user: /init exited status=0",
    "user: process pid=1 name=/init state=exited",
    "user: kernel resumed after EL0 pid=1 state=exited exit_code=0",
    "kernel: /init returned to kernel exit_code=0",
    "\"user_process_transitions\":3",
    "\"user_process_loaded\":1",
    "\"user_process_running\":1",
    "\"user_process_exited\":1",
    "\"user_process_failed\":0",
    "\"service_child_descriptors\":1",
]


def main() -> int:
    env = os.environ.copy()
    env["OSAI_QEMU_HOSTFWD_PORT"] = "none"
    proc = subprocess.Popen(
        ["make", "qemu-aarch64"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        env=env,
    )
    seen = []
    deadline = time.time() + int(os.environ.get("OSAI_QEMU_SMOKE_TIMEOUT", "60"))
    try:
        fd = proc.stdout.fileno()
        while time.time() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.2)
            if ready:
                chunk = os.read(fd, 4096).decode("utf-8", errors="replace")
                if not chunk:
                    break
                sys.stdout.write(chunk)
                sys.stdout.flush()
                seen.append(chunk)
                text = "".join(seen)
                if all(target in text for target in TARGETS):
                    print("\nQEMU smoke boot reached all full userspace/resource markers")
                    return 0
            elif proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)

    text = "".join(seen)
    missing = [target for target in TARGETS if target not in text]
    print("\nmissing targets:", missing)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
