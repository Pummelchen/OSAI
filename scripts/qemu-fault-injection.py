#!/usr/bin/env python3
from qemu_gate_lib import BUILD, check_markers, now, result, run, status_from_failures, write_report


SCHEMA = "osai.qemu.fault_injection.v1"
REPORT = BUILD / "qemu-milestone-53-fault-injection.json"

SMOKE_FAILURE_MARKERS = {
    "bad_elf_and_process_failure": [
        "user: process lifecycle invalid/failed transition self-test passed",
    ],
    "invalid_syscalls": [
        "user: rejected syscall=99",
        "user: rejected syscall=1",
        "/init: bad syscall tests passed",
    ],
    "malformed_virtio_net": [
        "virtio-net: malformed packet/drop self-test passed",
        "\"network_udp_malformed\":1",
        "\"network_packet_drops\":2",
    ],
    "block_read_error_reset": [
        "virtio-blk: read/write/error/reset self-test passed",
    ],
    "corrupt_state_rejection": [
        "\"persistence_checksum_errors\":0",
        "\"mutable_fs_checksum_errors\":0",
        "\"mutable_fs_rejects\":6",
    ],
    "failed_update_and_rollback": [
        "update: self-test passed transactions=2 staged=2 committed=1 failed=1 recovered=1 rollbacks=1",
        "\"update_failures\":1",
        "\"update_recoveries\":1",
    ],
    "ai_cell_admission_conflicts": [
        "ai-cell: resource contract self-test passed admissions=2 rejects=10",
        "\"ai_cell_conflicts\":3",
    ],
}


def main() -> int:
    failures = []
    checks = []

    fault_proc = run(["python3", "./scripts/qemu-fault-matrix.py"], timeout=260)
    checks.append(result("controlled_page_ro_nx_faults", fault_proc.returncode == 0,
                         exit_code=fault_proc.returncode))
    if fault_proc.returncode != 0:
        failures.append(f"qemu-fault-matrix exited {fault_proc.returncode}")

    smoke_proc = run(["python3", "./scripts/qemu-smoke.py"], timeout=140)
    if smoke_proc.returncode != 0:
        failures.append(f"qemu-smoke exited {smoke_proc.returncode}")

    for name, markers in SMOKE_FAILURE_MARKERS.items():
        missing = check_markers(smoke_proc.stdout, markers)
        checks.append(result(name, not missing, missing_markers=missing))
        failures.extend(f"{name} missing marker: {marker}" for marker in missing)

    report = {
        "schema": SCHEMA,
        "status": status_from_failures(failures),
        "milestone": 53,
        "created_at_unix": now(),
        "description": "QEMU fault injection for controlled faults and expected denial/error paths.",
        "fault_matrix_exit_code": fault_proc.returncode,
        "smoke_exit_code": smoke_proc.returncode,
        "checks": checks,
        "failures": failures,
    }
    write_report(REPORT, report)
    if failures:
        print(fault_proc.stdout)
        print(smoke_proc.stdout)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
