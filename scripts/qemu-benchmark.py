#!/usr/bin/env python3
import json
import os
import subprocess
import sys


def parse_telemetry(text: str):
    marker = "telemetry: "
    start = text.rfind(marker)
    if start < 0:
        return None
    payload = text[start + len(marker):].strip().splitlines()[0]
    if not payload.startswith("{"):
        return None
    try:
        return json.loads(payload)
    except json.JSONDecodeError:
        return None


def main() -> int:
    env = os.environ.copy()
    env.setdefault("OSAI_QEMU_SMOKE_TIMEOUT", "60")
    proc = subprocess.run(
        ["python3", "./scripts/qemu-smoke.py"],
        check=False,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    if proc.returncode != 0:
        sys.stdout.write(proc.stdout)
        print("qemu-benchmark: smoke failed; benchmark not collected")
        return 1

    telemetry = parse_telemetry(proc.stdout)
    if telemetry is None:
        sys.stdout.write(proc.stdout)
        print("qemu-benchmark: missing telemetry payload")
        return 1

    expected_keys = {
        "cpu_count",
        "pmm_total_pages",
        "pmm_free_pages",
        "kheap_pages",
        "arena_committed_pages",
        "persistence_snapshots",
        "persistence_rollbacks",
        "persistence_rejects",
        "security_denied_ops",
        "security_credential_rejects",
        "security_signature_accepts",
        "security_signature_rejects",
        "virtio_block_sectors",
        "network_udp_tx",
        "network_udp_rx",
        "network_udp_malformed",
        "network_udp_dropped",
        "network_tcp_connections",
        "network_queue_bindings",
        "ai_cell_transitions",
        "migration_total",
        "context_switch_total",
        "user_process_transitions",
        "user_process_loaded",
        "user_process_running",
        "user_process_exited",
        "user_process_failed",
    }
    missing = sorted(expected_keys - set(telemetry.keys()))
    if missing:
        print(f"qemu-benchmark: telemetry missing keys: {missing}")
        return 1

    gates = {
        "cpu_count_min": telemetry["cpu_count"] >= 1,
        "pmm_has_free_pages": telemetry["pmm_free_pages"] > 0,
        "virtio_block_visible": telemetry["virtio_block_sectors"] > 0,
        "ai_cell_transitions_present": telemetry["ai_cell_transitions"] >= 1,
        "security_denials_recorded": telemetry["security_denied_ops"] >= 2,
        "persistence_rollbacks_present": telemetry["persistence_rollbacks"] >= 4,
        "no_hot_path_migration": telemetry["migration_total"] == 0,
        "no_hot_path_context_switches": telemetry["context_switch_total"] == 0,
        "udp_path_exercised": telemetry["network_udp_tx"] >= 1
        and telemetry["network_udp_rx"] >= 1,
        "tcp_path_exercised": telemetry["network_tcp_connections"] >= 1,
        "user_process_lifecycle_complete": telemetry["user_process_loaded"] >= 1
        and telemetry["user_process_running"] >= 1
        and telemetry["user_process_exited"] >= 1
        and telemetry["user_process_failed"] == 0,
    }
    failed_gates = sorted(name for name, passed in gates.items() if not passed)
    if failed_gates:
        print(f"qemu-benchmark: failed gates: {failed_gates}")
        return 1

    report = {
        "schema": "osai.qemu.correctness_benchmark.v1",
        "status": "pass",
        "benchmark_type": "qemu-correctness",
        "baseline_required_for_performance_claims": True,
        "performance_claims_allowed": False,
        "notes": "QEMU results validate boot correctness and contracts only; they are not tuned Linux/BSD performance claims.",
        "gates": gates,
        "telemetry": telemetry,
        "smoke_exit_code": proc.returncode,
    }
    print(f"qemu-benchmark: report={json.dumps(report, sort_keys=True)}")

    output = os.environ.get("OSAI_QEMU_BENCHMARK_OUTPUT")
    if output:
        with open(output, "w", encoding="utf-8") as handle:
            json.dump(report, handle, sort_keys=True, indent=2)
        print(f"qemu-benchmark: report written to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
