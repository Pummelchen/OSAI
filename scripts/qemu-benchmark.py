#!/usr/bin/env python3
import json
import os
import re
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
        "virtio_block_sectors",
        "network_udp_tx",
        "network_udp_rx",
        "network_queue_bindings",
        "ai_cell_transitions",
    }
    missing = sorted(expected_keys - set(telemetry.keys()))
    if missing:
        print(f"qemu-benchmark: telemetry missing keys: {missing}")
        return 1

    report = {
        "status": "pass",
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
