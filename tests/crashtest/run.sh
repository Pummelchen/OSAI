#!/bin/bash
#
# XAI OS Crash Test Runner
#
# Quick start scripts for running crash tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
XAIOS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================"
echo "XAI OS CRASH TEST SUITE"
echo "========================================"
echo ""

case "${1:-help}" in
    outside)
        echo "Running OUTSIDE tests (network attacks from host)..."
        echo ""
        cd "$SCRIPT_DIR"
        python3 crashtest_server.py --mode outside --count ${2:-100}
        ;;
    
    inside)
        echo "Running INSIDE tests (inside XAI OS)..."
        echo ""
        echo "1. Start QEMU: cd $XAIOS_ROOT && make qemu"
        echo "2. crashtest_client runs automatically"
        echo "3. Check QEMU console for results"
        ;;
    
    quick)
        echo "Running QUICK validation (20 tests)..."
        echo ""
        cd "$SCRIPT_DIR"
        python3 crashtest_server.py --mode outside --count 20
        ;;
    
    full)
        echo "Running FULL crash test suite (200 tests)..."
        echo ""
        cd "$SCRIPT_DIR"
        python3 run_full_crashtest.py
        ;;
    
    report)
        echo "Generating crash test report..."
        echo ""
        cd "$SCRIPT_DIR"
        python3 run_full_crashtest.py --report-only
        ;;
    
    help|*)
        echo "Usage: $0 {outside|inside|quick|full|report}"
        echo ""
        echo "Commands:"
        echo "  outside [count]  Run outside network attacks (default: 100)"
        echo "  inside           Run inside tests (requires QEMU)"
        echo "  quick            Quick validation (20 tests)"
        echo "  full             Full test suite (200 tests)"
        echo "  report           Generate report from existing results"
        echo "  help             Show this help"
        echo ""
        echo "Examples:"
        echo "  $0 outside 50        # Run 50 outside tests"
        echo "  $0 quick             # Quick validation"
        echo "  $0 full              # Full suite"
        echo "  $0 report            # Generate report"
        ;;
esac
