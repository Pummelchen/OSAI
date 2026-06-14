#!/usr/bin/env python3
import json
import os
import subprocess
import time
from typing import Any, Dict, List


REPORT_SCHEMA = "osai.qemu.hardware_readiness_gate.v1"
BENCHMARK_SCHEMA = "osai.qemu.correctness_benchmark.v1"
PREVIEW_SCHEMA = "osai.qemu.preview.v1"
CPU_MATRIX_SCHEMA = "osai.qemu.cpu_matrix.v1"
CONTRACT_SCHEMA = "osai.qemu.release_candidate_contract.v1"
CONTRACT_PATH = "contracts/qemu-rc-v1.json"

FROZEN_QEMU_CONTRACTS = [
    {
        "id": "qemu.aarch64.uefi-loader",
        "description": "AArch64 UEFI loader boots and transfers control to kernel.elf.",
    },
    {
        "id": "qemu.memory.pmm-vmm",
        "description": "UEFI memory map is parsed into PMM/VMM state with map/unmap checks.",
    },
    {
        "id": "qemu.protection.controlled-faults",
        "description": "Controlled page, read-only write, and NX execute faults are reported through the exception path.",
    },
    {
        "id": "qemu.userspace.el0-init",
        "description": "Real EL0 /init ELF is loaded from the VirtIO-backed read-only filesystem.",
    },
    {
        "id": "qemu.syscall.capabilities",
        "description": "Syscalls enforce process capabilities and user pointer validation.",
    },
    {
        "id": "qemu.abi.freeze",
        "description": "QEMU RC contract freezes syscall ABI, telemetry schema, filesystem format, persistence format, and service descriptor format.",
    },
    {
        "id": "qemu.virtio.block-net",
        "description": "Split VirtIO transport, block, and net self-tests pass.",
    },
    {
        "id": "qemu.ai-cell.resources",
        "description": "AI Cell lifecycle, core leases, model arenas, KV/cache, source index, workspace, sandbox, and CPU-AI runtime fixtures emit telemetry.",
    },
    {
        "id": "qemu.security.policy",
        "description": "Security policy enforces capabilities, filesystem boundaries, workspace and sandbox ownership, rollback authorization, credential rejection, and signed-update format validation.",
    },
    {
        "id": "qemu.persistence.rollback-metadata",
        "description": "VirtIO-backed persistence snapshots, reloads after reboot, and rolls back boot, service, workspace, and sandbox records.",
    },
    {
        "id": "qemu.telemetry.no-hot-path-migration",
        "description": "Hot AI core telemetry reports zero migration and zero involuntary context switches.",
    },
]

INTEL_DESKTOP_ENTRY_CRITERIA = [
    "make qemu-readiness-gate exits with status 0.",
    "build/qemu-preview-manifest.json exists and uses schema osai.qemu.preview.v1.",
    "build/qemu-benchmark-report.json exists and uses schema osai.qemu.correctness_benchmark.v1.",
    "All benchmark gates are true.",
    "Two-boot persistence reboot validation passes on the same VirtIO state image.",
    "QEMU performance_claims_allowed is false.",
    "QEMU benchmark_type remains qemu-correctness.",
    "Controlled page, read-only write, and NX fault scenarios pass.",
    "QEMU CPU matrix report validates ARM64 boot tiers and x86_64 command tiers.",
    "QEMU RC contract remains frozen at osai.qemu.release_candidate_contract.v1.",
    "Platform and benchmark documentation describe QEMU as correctness-only.",
]

REQUIRED_TELEMETRY_MINIMUMS = {
    "cpu_count": 1,
    "pmm_total_pages": 1,
    "pmm_free_pages": 1,
    "virtio_block_sectors": 1,
    "ai_cell_transitions": 1,
    "cpu_ai_model_loads": 4,
    "cpu_ai_model_load_failures": 3,
    "cpu_ai_tokenizer_calls": 4,
    "cpu_ai_runtime_calls": 4,
    "cpu_ai_kv_writes": 16,
    "cpu_ai_shared_weight_binds": 4,
    "cpu_ai_gpu_rejects": 1,
    "security_denied_ops": 16,
    "security_capability_denials": 3,
    "security_fs_denials": 1,
    "security_workspace_denials": 4,
    "security_sandbox_denials": 3,
    "security_rollback_denials": 1,
    "security_update_policy_rejects": 1,
    "security_credential_rejects": 3,
    "security_signature_rejects": 1,
    "persistence_snapshots": 5,
    "persistence_rollbacks": 5,
    "persistence_disk_writes": 1,
    "persistence_disk_loads": 1,
    "network_udp_tx": 3,
    "network_udp_rx": 3,
    "network_udp_flows": 1,
    "network_udp_flow_hits": 1,
    "network_udp_expired": 1,
    "network_tcp_connections": 1,
    "network_tcp_timeouts": 1,
    "network_tcp_retransmits": 1,
    "network_tcp_established": 1,
    "network_tcp_closed": 1,
    "network_rx_packets": 6,
    "network_tx_packets": 6,
    "network_packet_drops": 2,
    "network_packet_lifecycle": 18,
    "network_queue_rx_enqueues": 6,
    "network_queue_tx_enqueues": 6,
    "network_queue_completions": 6,
    "service_child_descriptors": 1,
    "service_tree_edges": 1,
    "service_transitions": 12,
    "service_restarts": 1,
    "service_crashes": 1,
    "service_cleanups": 3,
    "service_log_records": 2,
    "control_plane_syscalls": 13,
    "control_plane_denials": 4,
    "service_descriptor_reads": 1,
    "user_process_transitions": 6,
    "user_process_loaded": 2,
    "user_process_running": 2,
    "user_process_exited": 2,
    "user_process_reclaims": 2,
}

REQUIRED_TELEMETRY_EQUALS = {
    "migration_total": 0,
    "context_switch_total": 0,
    "user_process_failed": 0,
    "persistence_checksum_errors": 0,
    "network_queue_backpressure_drops": 0,
    "network_flow_core_mismatches": 0,
}


def run(cmd: List[str], env: Dict[str, str]) -> subprocess.CompletedProcess:
    print(f"qemu-readiness-gate: running {' '.join(cmd)}", flush=True)
    return subprocess.run(
        cmd,
        check=False,
        env=env,
        stdout=None,
        stderr=None,
        text=True,
    )


def load_json(path: str, failures: List[str]) -> Dict[str, Any]:
    if not os.path.exists(path):
        failures.append(f"missing artifact: {path}")
        return {}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError as exc:
        failures.append(f"invalid JSON artifact: {path}: {exc}")
        return {}


def check_bool(value: Any, expected: bool, name: str, failures: List[str]) -> None:
    if value is not expected:
        failures.append(f"{name} expected {expected!r}, got {value!r}")


def check_equal(value: Any, expected: Any, name: str, failures: List[str]) -> None:
    if value != expected:
        failures.append(f"{name} expected {expected!r}, got {value!r}")


def validate_benchmark(report: Dict[str, Any], failures: List[str]) -> Dict[str, Any]:
    check_equal(report.get("schema"), BENCHMARK_SCHEMA, "benchmark.schema", failures)
    check_equal(report.get("status"), "pass", "benchmark.status", failures)
    check_equal(report.get("benchmark_type"), "qemu-correctness", "benchmark.benchmark_type", failures)
    check_bool(report.get("baseline_required_for_performance_claims"), True, "benchmark.baseline_required_for_performance_claims", failures)
    check_bool(report.get("performance_claims_allowed"), False, "benchmark.performance_claims_allowed", failures)

    gates = report.get("gates", {})
    if not isinstance(gates, dict) or not gates:
        failures.append("benchmark.gates missing or empty")
    else:
        failed_gates = sorted(name for name, passed in gates.items() if passed is not True)
        if failed_gates:
            failures.append(f"benchmark gates failed: {failed_gates}")

    telemetry = report.get("telemetry", {})
    if not isinstance(telemetry, dict):
        failures.append("benchmark.telemetry missing or not an object")
        return {}

    for key, minimum in REQUIRED_TELEMETRY_MINIMUMS.items():
        value = telemetry.get(key)
        if not isinstance(value, int) or value < minimum:
            failures.append(f"telemetry.{key} expected >= {minimum}, got {value!r}")

    for key, expected in REQUIRED_TELEMETRY_EQUALS.items():
        value = telemetry.get(key)
        if value != expected:
            failures.append(f"telemetry.{key} expected {expected}, got {value!r}")

    return telemetry


def validate_preview(manifest: Dict[str, Any], benchmark: Dict[str, Any], failures: List[str]) -> None:
    check_equal(manifest.get("schema"), PREVIEW_SCHEMA, "preview.schema", failures)
    check_equal(manifest.get("status"), "pass", "preview.status", failures)
    check_equal(manifest.get("benchmark_schema"), BENCHMARK_SCHEMA, "preview.benchmark_schema", failures)
    check_equal(manifest.get("release_candidate_contract"), CONTRACT_PATH, "preview.release_candidate_contract", failures)

    contracts = manifest.get("contracts", {})
    if not isinstance(contracts, dict):
        failures.append("preview.contracts missing or not an object")
        return

    check_equal(contracts.get("architecture"), "aarch64", "preview.contracts.architecture", failures)
    check_equal(contracts.get("firmware"), "UEFI", "preview.contracts.firmware", failures)
    check_equal(contracts.get("machine"), "qemu-virt", "preview.contracts.machine", failures)
    check_equal(contracts.get("release_candidate_contract_schema"), CONTRACT_SCHEMA, "preview.contracts.release_candidate_contract_schema", failures)
    check_bool(contracts.get("performance_claims_allowed"), False, "preview.contracts.performance_claims_allowed", failures)

    benchmark_telemetry = benchmark.get("telemetry", {}) if isinstance(benchmark, dict) else {}
    preview_telemetry = manifest.get("telemetry", {})
    if isinstance(benchmark_telemetry, dict) and isinstance(preview_telemetry, dict):
        for key in REQUIRED_TELEMETRY_EQUALS:
            if preview_telemetry.get(key) != benchmark_telemetry.get(key):
                failures.append(f"preview.telemetry.{key} does not match benchmark telemetry")

def validate_contract(contract: Dict[str, Any], failures: List[str]) -> Dict[str, Any]:
    check_equal(contract.get("schema"), CONTRACT_SCHEMA, "contract.schema", failures)
    check_equal(contract.get("status"), "frozen", "contract.status", failures)
    check_bool(contract.get("scope", {}).get("performance_claims_allowed"), False, "contract.scope.performance_claims_allowed", failures)
    check_equal(contract.get("scope", {}).get("benchmark_type"), "qemu-correctness", "contract.scope.benchmark_type", failures)

    syscall_abi = contract.get("syscall_abi", {})
    syscalls = syscall_abi.get("syscalls", [])
    capabilities = syscall_abi.get("capabilities", [])
    check_equal(syscall_abi.get("version"), 1, "contract.syscall_abi.version", failures)
    if len(syscalls) != 10:
        failures.append(f"contract.syscall_abi.syscalls expected 10 entries, got {len(syscalls)}")
    if len(capabilities) != 7:
        failures.append(f"contract.syscall_abi.capabilities expected 7 entries, got {len(capabilities)}")
    expected_syscall_numbers = list(range(1, 11))
    actual_syscall_numbers = [entry.get("number") for entry in syscalls]
    if actual_syscall_numbers != expected_syscall_numbers:
        failures.append(f"contract.syscall_abi numbers expected {expected_syscall_numbers}, got {actual_syscall_numbers}")

    telemetry_schema = contract.get("telemetry_schema", {})
    check_equal(telemetry_schema.get("schema"), "osai.qemu.telemetry.v1", "contract.telemetry_schema.schema", failures)
    check_equal(telemetry_schema.get("minimums"), REQUIRED_TELEMETRY_MINIMUMS, "contract.telemetry_schema.minimums", failures)
    check_equal(telemetry_schema.get("equals"), REQUIRED_TELEMETRY_EQUALS, "contract.telemetry_schema.equals", failures)

    filesystem = contract.get("filesystem_format", {})
    check_equal(filesystem.get("magic"), "OSAIROFS2", "contract.filesystem.magic", failures)
    check_equal(filesystem.get("version"), 2, "contract.filesystem.version", failures)
    check_equal(filesystem.get("manifest_path"), "/etc/osai-init.conf", "contract.filesystem.manifest_path", failures)
    required_paths = filesystem.get("required_paths", [])
    for path in ["/init", "/bin/service-manager", "/etc/osai-init.conf", "/etc/services/source-index.svc"]:
        if path not in required_paths:
            failures.append(f"contract.filesystem.required_paths missing {path}")

    persistence = contract.get("persistence_format", {})
    check_equal(persistence.get("magic"), "OSAIPST1", "contract.persistence.magic", failures)
    check_equal(persistence.get("version"), 1, "contract.persistence.version", failures)
    check_equal(persistence.get("sector"), 1536, "contract.persistence.sector", failures)

    descriptor = contract.get("service_descriptor_format", {})
    check_equal(descriptor.get("path"), "/etc/services/source-index.svc", "contract.service_descriptor.path", failures)
    for key in ["name", "parent", "restart", "start", "status"]:
        if key not in descriptor.get("required_keys", []):
            failures.append(f"contract.service_descriptor.required_keys missing {key}")

    out_of_scope = contract.get("out_of_scope_before_intel", [])
    if len(out_of_scope) < 5:
        failures.append("contract.out_of_scope_before_intel is too short")
    return contract


def validate_cpu_matrix(report: Dict[str, Any], contract: Dict[str, Any], failures: List[str]) -> None:
    check_equal(report.get("schema"), CPU_MATRIX_SCHEMA, "cpu_matrix.schema", failures)
    check_equal(report.get("status"), "pass", "cpu_matrix.status", failures)
    check_equal(report.get("contract"), CONTRACT_PATH, "cpu_matrix.contract", failures)
    tiers = report.get("tiers", [])
    if not isinstance(tiers, list) or not tiers:
        failures.append("cpu_matrix.tiers missing or empty")
        return
    failed = [tier.get("name") for tier in tiers if tier.get("status") != "pass"]
    if failed:
        failures.append(f"cpu_matrix failed tiers: {failed}")

    contract_matrix = contract.get("cpu_matrix", {})
    required_names = set()
    for tier in contract_matrix.get("arm64_boot_tiers", []):
        required_names.add(tier.get("name"))
    for tier in contract_matrix.get("x86_64_command_tiers", []):
        required_names.add(tier.get("name"))
    actual_names = {tier.get("name") for tier in tiers}
    missing = sorted(name for name in required_names if name not in actual_names)
    if missing:
        failures.append(f"cpu_matrix missing contract tiers: {missing}")


def validate_docs(root: str, failures: List[str]) -> Dict[str, bool]:
    required_snippets = {
        "HARDWARE-READINESS.md": [
            "make qemu-readiness-gate",
            "osai.qemu.hardware_readiness_gate.v1",
            "osai.qemu.release_candidate_contract.v1",
            "correctness benchmark only",
        ],
        "QEMU-FULL-OS-PLAN.md": [
            "QEMU readiness gate",
            "QEMU Release Candidate",
            "Only then start Intel Desktop code",
        ],
    }
    result: Dict[str, bool] = {}
    for relative, snippets in required_snippets.items():
        path = os.path.join(root, relative)
        if not os.path.exists(path):
            failures.append(f"missing documentation: {relative}")
            result[relative] = False
            continue
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        missing = [snippet for snippet in snippets if snippet not in text]
        if missing:
            failures.append(f"{relative} missing snippets: {missing}")
            result[relative] = False
        else:
            result[relative] = True
    return result


def main() -> int:
    root = os.getcwd()
    build_dir = os.path.join(root, "build")
    os.makedirs(build_dir, exist_ok=True)

    env = os.environ.copy()
    env.setdefault("OSAI_QEMU_SMOKE_TIMEOUT", "60")
    matrix = run(["make", "qemu-matrix"], env)

    failures: List[str] = []
    if matrix.returncode != 0:
        failures.append(f"qemu matrix failed with exit code {matrix.returncode}")

    benchmark_path = os.path.join(build_dir, "qemu-benchmark-report.json")
    preview_path = os.path.join(build_dir, "qemu-preview-manifest.json")
    cpu_matrix_path = os.path.join(build_dir, "qemu-cpu-matrix-report.json")
    readiness_path = os.path.join(build_dir, "qemu-readiness-report.json")
    contract_path = os.path.join(root, CONTRACT_PATH)

    contract = load_json(contract_path, failures)
    benchmark = load_json(benchmark_path, failures)
    preview = load_json(preview_path, failures)
    cpu_matrix = load_json(cpu_matrix_path, failures)
    if contract:
        validate_contract(contract, failures)
    telemetry = validate_benchmark(benchmark, failures) if benchmark else {}
    if preview:
        validate_preview(preview, benchmark, failures)
    if cpu_matrix and contract:
        validate_cpu_matrix(cpu_matrix, contract, failures)
    doc_checks = validate_docs(root, failures)

    report = {
        "schema": REPORT_SCHEMA,
        "created_unix": int(time.time()),
        "status": "fail" if failures else "pass",
        "qemu_full_os_complete": False,
        "qemu_full_os_note": "Milestone 33 freezes the QEMU hardware-readiness contract. It does not mark the full QEMU OS complete.",
        "matrix_exit_code": matrix.returncode,
        "artifacts": {
            "benchmark_report": "build/qemu-benchmark-report.json",
            "preview_manifest": "build/qemu-preview-manifest.json",
            "cpu_matrix_report": "build/qemu-cpu-matrix-report.json",
            "release_candidate_contract": CONTRACT_PATH,
            "readiness_report": "build/qemu-readiness-report.json",
        },
        "release_candidate_contract_schema": contract.get("schema") if contract else None,
        "frozen_qemu_contracts": FROZEN_QEMU_CONTRACTS,
        "intel_desktop_entry_criteria": INTEL_DESKTOP_ENTRY_CRITERIA,
        "benchmark_schema": benchmark.get("schema") if benchmark else None,
        "preview_schema": preview.get("schema") if preview else None,
        "cpu_matrix_schema": cpu_matrix.get("schema") if cpu_matrix else None,
        "performance_claims_allowed": benchmark.get("performance_claims_allowed") if benchmark else None,
        "out_of_scope_before_intel": contract.get("out_of_scope_before_intel", []) if contract else [],
        "telemetry": telemetry,
        "documentation": doc_checks,
        "failures": failures,
    }

    with open(readiness_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, sort_keys=True, indent=2)

    print(f"qemu-readiness-gate: report written to {readiness_path}")
    if failures:
        print("qemu-readiness-gate: failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("qemu-readiness-gate: milestone 33 hardware-readiness gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
