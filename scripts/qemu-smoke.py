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
    "virtio-blk: read/write/error/reset self-test passed",
    "persistence: mutable state region sector=1536 sectors=1",
    "persistence: disk write sector=1536 version=1 records=5",
    "persistence: disk loaded sector=1536 version=1 records=5",
    "persistence: disk reload/rollback self-test passed snapshots=5 rollbacks=5 rejects=2 disk_writes=1 disk_loads=1 checksum_errors=0",
    "mutable-fs: mounted start=1600 metadata=4 data=1604 sectors=16 policy=rw",
    "mutable-fs: write path=/state/service.db",
    "mutable-fs: snapshot committed",
    "mutable-fs: snapshot rollback",
    "mutable-fs: self-test passed files=2 writes=4 reads=3 deletes=1 commits=1 rollbacks=1 rejects=4 checksum_errors=0",
    "core-lease: isolation self-test passed",
    "virtio-net: malformed packet/drop self-test passed",
    "virtio-net: rx/tx/reset self-test passed",
    "network: stack initialized",
    "network: udp flow id=",
    "network: tcp flow id=",
    "network: queue-backed udp/tcp self-test passed rx=4 tx=4 drops=2 lifecycle=12 udp_flows=1 tcp_timeouts=1",
    "initramfs: config service=/init mode=qemu-mvp",
    "initramfs: service-manager path=/bin/service-manager descriptor=/etc/services/source-index.svc",
    "initramfs: child service=/svc/source-index parent=/init restart=never",
    "initramfs: mounted rofs version=2 files=4",
    "initramfs: rofs metadata/config self-test passed",
    "syscall: table self-test passed entries=10",
    "user: process table initialized slots=4",
    "user: process lifecycle invalid/failed transition self-test passed",
    "user: process pid=1 name=/init state=loaded",
    "security: self-test passed denied=8 capability_denials=1 fs_denials=1 workspace_denials=1 sandbox_denials=1 rollback_denials=1 update_policy_rejects=1 credential_rejects=2 signature_accepts=1 signature_rejects=1",
    "model-arena: shared read-only arena self-test passed",
    "core-lease: owner=0 mask=0x2 acquired",
    "nic-conflict-agent",
    "ai-cell: lifecycle self-test passed",
    "kheap: self-test passed",
    "VMM translation test passed",
    "gic: discovery self-test passed",
    "PMM 1024 page allocate/free test passed",
    "cpu-ai-runtime: model manifest loaded",
    "cpu-ai-runtime: tokenizer/runtime boundary self-test passed tokenizer_calls=2 runtime_calls=2",
    "cpu-ai-runtime: multi-cell shared weights self-test passed loads=2 shared_binds=2 kv_writes=8",
    "cpu-ai-runtime: model load failure self-test passed failures=3 gpu_rejects=1",
    "cpu-ai-runtime: deterministic decode fixture input=ABCD output=1B1F2327",
    "cpu-ai-runtime: self-test passed",
    "ai-cell: multi-cell shared model/private kv self-test passed",
    "\"arena_committed_pages\"",
    "\"git_workspace_active\"",
    "\"git_workspace_syncs\"",
    "\"git_workspace_applies\"",
    "\"git_workspace_reverts\"",
    "\"git_workspace_conflicts\"",
    "\"persistence_snapshots\":5",
    "\"persistence_rollbacks\":5",
    "\"persistence_rejects\":2",
    "\"persistence_disk_writes\":1",
    "\"persistence_disk_loads\":1",
    "\"persistence_checksum_errors\":0",
    "\"mutable_fs_mounts\":1",
    "\"mutable_fs_files\":2",
    "\"mutable_fs_writes\":4",
    "\"mutable_fs_reads\":3",
    "\"mutable_fs_deletes\":1",
    "\"mutable_fs_commits\":1",
    "\"mutable_fs_rollbacks\":1",
    "\"mutable_fs_rejects\":4",
    "\"mutable_fs_checksum_errors\":0",
    "\"migration_total\":0",
    "\"context_switch_total\":0",
    "\"source_index_updates\":1",
    "\"security_denied_ops\":16",
    "\"security_capability_denials\":3",
    "\"security_fs_denials\":1",
    "\"security_workspace_denials\":4",
    "\"security_sandbox_denials\":3",
    "\"security_rollback_denials\":1",
    "\"security_update_policy_rejects\":1",
    "\"security_credential_rejects\":3",
    "\"security_signature_accepts\":1",
    "\"security_signature_rejects\":1",
    "\"network_udp_tx\":1",
    "\"network_udp_rx\":1",
    "\"network_udp_malformed\":1",
    "\"network_udp_dropped\":1",
    "\"network_udp_flows\":1",
    "\"network_tcp_connections\":1",
    "\"network_tcp_timeouts\":1",
    "\"network_rx_packets\":4",
    "\"network_tx_packets\":4",
    "\"network_packet_drops\":2",
    "\"network_packet_lifecycle\":12",
    "\"network_udp_p999\"",
    "\"network_tcp_p999\"",
    "\"cpu_ai_model_loads\":4",
    "\"cpu_ai_model_load_failures\":3",
    "\"cpu_ai_tokenizer_calls\":4",
    "\"cpu_ai_runtime_calls\":4",
    "\"cpu_ai_kv_writes\":16",
    "\"cpu_ai_shared_weight_binds\":4",
    "\"cpu_ai_gpu_rejects\":1",
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
    "/init: service setup complete",
    "rejected=3",
    "user: /init exited status=0",
    "user: process pid=1 name=/init state=exited",
    "user: kernel resumed after EL0 pid=1 state=exited exit_code=0",
    "kernel: /init returned to kernel exit_code=0",
    "user: reclaimed address space pid=1",
    "user: loaded /bin/service-manager ELF",
    "user: process pid=2 name=/bin/service-manager state=running",
    "/service-manager: hello from ELF",
    "user: service descriptor read path=/etc/services/source-index.svc",
    "/service-manager: descriptor loaded",
    "user: rejected syscall=9",
    "user: rejected syscall=10",
    "/service-manager: missing capability tests passed",
    "service-manager: defined child /svc/source-index parent=/init restart=never",
    "service: /svc/source-index state=running",
    "osctl: /svc/source-index state=running",
    "service-manager: restart denied /svc/source-index policy=never",
    "/service-manager: child service supervised",
    "/service-manager: control plane complete",
    "user: /bin/service-manager exited status=0",
    "kernel: /bin/service-manager returned to kernel exit_code=0",
    "\"user_process_transitions\":6",
    "\"user_process_loaded\":2",
    "\"user_process_running\":2",
    "\"user_process_exited\":2",
    "\"user_process_failed\":0",
    "\"service_child_descriptors\":1",
    "\"control_plane_syscalls\"",
    "\"control_plane_denials\"",
    "\"service_descriptor_reads\":1",
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
