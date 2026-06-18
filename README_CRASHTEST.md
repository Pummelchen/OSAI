# XAI OS Crash Test Suite

Comprehensive crash testing system for XAI OS with 200+ destructive test scenarios to harden the OS for production deployment.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  HOST MAC (crashtest_server.py)                              │
│  - Python 3 TCP server                                       │
│  - 100+ OUTSIDE tests (network attacks)                      │
│  - Test orchestration & reporting                            │
│                                                              │
│  Attacks via: TCP/UDP/ICMP/SSH/ARP/Network Fuzzing          │
└─────────────────────────────────────────────────────────────┘
                            │
                    QEMU port forwarding
                    host:9999 → guest:9999
                            │
┌─────────────────────────────────────────────────────────────┐
│  XAI OS (QEMU AArch64)                                       │
│                                                              │
│  crashtest_client.c (userspace app)                         │
│  - Connects to host server                                   │
│  - Executes 100+ INSIDE tests                               │
│  - Memory corruption, syscall abuse, filesystem destruction │
│  - CPU exhaustion, AI attacks, threading chaos              │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### 1. Run Outside Tests (Network Attacks from Host)

```bash
# Start QEMU with XAI OS
make qemu

# In another terminal, run crash tests
cd tests/crashtest
python3 crashtest_server.py --mode outside --count 50
```

### 2. Run Inside Tests (Inside XAI OS)

```bash
# Build XAI OS with crash test client
make all

# Start QEMU
make qemu

# Inside XAI OS, run:
/bin/crashtest_client
```

### 3. Dry Run (No Actual Attacks)

```bash
python3 crashtest_server.py --mode dry-run
```

## Test Categories

### OUTSIDE Tests (100 tests) - Network Attacks from Host

| Category | Tests | Description |
|----------|-------|-------------|
| TCP Stack Attacks | 1-20 | SYN flood, half-open connections, reset storms, sequence attacks |
| UDP Stack Attacks | 21-35 | UDP flood, port scan, amplification, fragmentation |
| ICMP Attacks | 36-45 | Ping flood, smurf attack, redirect abuse |
| SSH Protocol Attacks | 46-65 | Version overflow, KEXINIT flood, auth brute force |
| ARP Attacks | 66-75 | Cache poisoning, spoofing, flood |
| Network Protocol Fuzzing | 76-90 | Random frames, oversized packets, IP manipulation |
| Connection Management | 91-100 | Rapid connect/disconnect, connection leaks, timeouts |

### INSIDE Tests (100 tests) - Executed Inside XAI OS

| Category | Tests | Description |
|----------|-------|-------------|
| Memory Corruption | 101-120 | Null deref, use-after-free, buffer overflow, stack smash |
| Syscall Abuse | 121-135 | Invalid syscalls, parameter overflow, resource exhaustion |
| Filesystem Destruction | 136-155 | Delete /, create 1M files, path traversal, corruption |
| CPU Exhaustion | 156-170 | Infinite loops, division by zero, scheduler starvation |
| AI Stack Attacks | 171-185 | Malformed models, tensor overflow, KV cache exhaustion |
| Threading Chaos | 186-200 | Thread bomb, deadlock, race conditions, priority inversion |

## Communication Protocol

Binary protocol over TCP port 9999:

```
Message Format:
┌────────┬────────┬──────────┬─────────────────────────────┐
│ Type   │ Length │ Test ID  │ Payload                     │
│ 1 byte │ 2 bytes│ 2 bytes  │ Variable length             │
└────────┴────────┴──────────┴─────────────────────────────┘

Message Types:
0x01 = TEST_COMMAND (server → client)
0x02 = TEST_RESULT (client → server)
0x03 = CRASH_REPORT (client → server)
0x04 = HEARTBEAT (bidirectional, every 5s)
0x05 = LOG_MESSAGE (client → server)
0x06 = TEST_ABORT (server → client)
```

## Results & Reporting

### Test Results JSON

After running tests, results are saved to:
- `tests/crashtest/crashtest_results.json`

### Markdown Report

A comprehensive report is generated:
- `tests/crashtest/crashtest_report.md`

Example report:
```markdown
# XAI OS Crash Test Report

## Summary
| Metric | Value |
|--------|-------|
| Total Tests | 100 |
| Passed | 45 ✅ |
| Failed | 35 ❌ |
| Crashed | 20 💥 |
| Crash Rate | 20.0% |

## Recommendations
🟡 WARNING: XAI OS has moderate vulnerabilities
Priority: Address crashes in networking stack
```

## File Structure

```
xai-os/
├── tests/crashtest/
│   ├── crashtest_protocol.h        # Protocol definitions (C header)
│   ├── crashtest_server.py         # Server orchestrator (Python)
│   ├── crashtest_results.json      # Test results (generated)
│   └── crashtest_report.md         # Report (generated)
│
├── userspace/apps/
│   └── crashtest_client.c          # Client app (inside XAI OS)
│
└── README_CRASHTEST.md             # This file
```

## Expected Outcomes

### Initial Run
- **Crash Rate**: 60-80%
- XAI OS will fail many tests (expected - it's a new OS)
- Use results to identify vulnerabilities

### After Hardening
- **Crash Rate**: <10%
- Most critical bugs fixed
- Networking stack hardened

### Production Ready
- **Crash Rate**: <1%
- Only edge cases fail
- Ready for real-world deployment

## Error Handling

### Crash Detection
1. **Heartbeat Timeout**: No heartbeat for 10s → assume crash
2. **TCP Connection Lost**: Connection reset → crash detected
3. **Test Timeout**: 30s max per test → abort and mark as timeout

### Auto-Recovery
1. **Test Skip**: Mark failed test, continue with next
2. **State Reset**: Clean environment between tests (future)
3. **Checkpoint**: Save progress every 10 tests (future)

## Implementation Status

### Phase 1: Infrastructure ✅ COMPLETE
- [x] Protocol definitions (`crashtest_protocol.h`)
- [x] Server implementation (`crashtest_server.py`)
- [x] Client implementation (`crashtest_client.c`)
- [x] Heartbeat mechanism
- [x] Logging infrastructure
- [x] Test result reporting

### Phase 2: Outside Tests 🚧 IN PROGRESS
- [x] TCP Stack Attacks (10/20 implemented)
- [x] UDP Stack Attacks (5/15 implemented)
- [x] ICMP Attacks (3/10 implemented)
- [x] SSH Protocol Attacks (5/20 implemented)
- [ ] ARP Attacks (0/10)
- [ ] Network Protocol Fuzzing (0/15)
- [ ] Connection Management (0/10)

### Phase 3: Inside Tests 🚧 IN PROGRESS
- [x] Memory Corruption (10/20 implemented)
- [ ] Syscall Abuse (0/15)
- [x] Filesystem Destruction (3/20 implemented)
- [x] CPU Exhaustion (2/15 implemented)
- [ ] AI Stack Attacks (0/15)
- [ ] Threading Chaos (0/15)

### Phase 4: AI & Threading Tests ⏳ PENDING
### Phase 5: Automation & Reporting ⏳ PENDING

## Building

```bash
# Build XAI OS with crash test client
make all

# Build only crash test client
make userspace/apps/crashtest_client
```

## Troubleshooting

### Cannot Connect to XAI OS
- Ensure QEMU is running: `make qemu`
- Check port forwarding: host:9999 → guest:9999
- Verify firewall isn't blocking port 9999

### Tests Not Running
- Check XAI OS logs for errors
- Verify network stack is initialized
- Ensure crash test client is running inside XAI OS

### High Crash Rate
- This is EXPECTED for initial runs
- Use crash reports to identify vulnerabilities
- Fix bugs iteratively, re-run tests after each fix

## Contributing

When adding new crash tests:

1. Add test to appropriate category in `crashtest_server.py`
2. Follow naming convention: `test_<category>_<description>`
3. Include detailed logging
4. Test should be idempotent (safe to run multiple times)
5. Update test count in documentation

## License

XAI OS Project - Internal Testing Tool
