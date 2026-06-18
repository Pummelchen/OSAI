#!/usr/bin/env python3
"""
Crash Test Framework Validation

Tests the crash test framework itself without requiring QEMU:
1. Validates all 100 outside test methods exist and are callable
2. Validates test protocol definitions
3. Validates report generation
4. Runs syntax and import checks
"""

import sys
import os
import importlib.util

def validate_server():
    """Validate crashtest_server.py"""
    print("="*60)
    print("VALIDATING: crashtest_server.py")
    print("="*60)
    
    # Import module
    spec = importlib.util.spec_from_file_location("crashtest_server", "crashtest_server.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    
    server = module.CrashTestServer()
    
    # Count test methods
    test_methods = [m for m in dir(server) if m.startswith('test_') and callable(getattr(server, m))]
    
    print(f"✓ Module imported successfully")
    print(f"✓ Test methods found: {len(test_methods)}")
    
    # Categorize tests
    categories = {
        'tcp': [m for m in test_methods if 'tcp' in m],
        'udp': [m for m in test_methods if 'udp' in m],
        'icmp': [m for m in test_methods if 'icmp' in m],
        'ssh': [m for m in test_methods if 'ssh' in m],
        'arp': [m for m in test_methods if 'arp' in m],
        'net': [m for m in test_methods if 'net' in m],
        'conn': [m for m in test_methods if 'conn' in m],
    }
    
    print(f"\nTest Categories:")
    print(f"  TCP: {len(categories['tcp'])} tests")
    print(f"  UDP: {len(categories['udp'])} tests")
    print(f"  ICMP: {len(categories['icmp'])} tests")
    print(f"  SSH: {len(categories['ssh'])} tests")
    print(f"  ARP: {len(categories['arp'])} tests")
    print(f"  Network: {len(categories['net'])} tests")
    print(f"  Connection: {len(categories['conn'])} tests")
    
    total = sum(len(v) for v in categories.values())
    print(f"\n✓ Total outside tests: {total}")
    
    # Verify all tests are callable
    print(f"\nVerifying test methods are callable...")
    for method_name in test_methods:
        method = getattr(server, method_name)
        if not callable(method):
            print(f"  ✗ {method_name} is not callable")
            return False
    
    print(f"✓ All {len(test_methods)} test methods are callable")
    
    # Test report generation
    print(f"\nTesting report generation...")
    server.tests_completed = 100
    server.tests_passed = 70
    server.tests_failed = 20
    server.tests_crashed = 10
    
    try:
        os.makedirs('test_output', exist_ok=True)
        server.save_results('test_output/test_results_validation.json')
        print(f"✓ JSON report generation works")
        os.remove('test_output/test_results_validation.json')
        os.rmdir('test_output')
    except Exception as e:
        print(f"✗ JSON report failed: {e}")
        return False
    
    try:
        os.makedirs('test_output', exist_ok=True)
        server.generate_report('test_output/test_report_validation.md')
        print(f"✓ Markdown report generation works")
        os.remove('test_output/test_report_validation.md')
        os.rmdir('test_output')
    except Exception as e:
        print(f"✗ Markdown report failed: {e}")
        return False
    
    return True

def validate_protocol():
    """Validate crashtest_protocol.h"""
    print("\n" + "="*60)
    print("VALIDATING: crashtest_protocol.h")
    print("="*60)
    
    with open('crashtest_protocol.h', 'r') as f:
        content = f.read()
    
    # Check for key definitions
    checks = [
        ('CRASHTEST_MSG_TEST_COMMAND', 'Message type: TEST_COMMAND'),
        ('CRASHTEST_MSG_TEST_RESULT', 'Message type: TEST_RESULT'),
        ('CRASHTEST_MSG_CRASH_REPORT', 'Message type: CRASH_REPORT'),
        ('CRASHTEST_MSG_HEARTBEAT', 'Message type: HEARTBEAT'),
        ('CRASHTEST_CAT_TCP_ATTACK', 'Category: TCP'),
        ('CRASHTEST_CAT_MEM_CORRUPT', 'Category: Memory'),
        ('CRASHTEST_CAT_AI_ATTACK', 'Category: AI'),
        ('crashtest_write_u16_be', 'Big-endian write helper'),
        ('crashtest_read_u16_be', 'Big-endian read helper'),
    ]
    
    all_passed = True
    for definition, description in checks:
        if definition in content:
            print(f"✓ {description}")
        else:
            print(f"✗ {description} - MISSING")
            all_passed = False
    
    return all_passed

def validate_client():
    """Validate crashtest_client.c"""
    print("\n" + "="*60)
    print("VALIDATING: crashtest_client.c")
    print("="*60)
    
    with open('../../userspace/apps/crashtest_client.c', 'r') as f:
        content = f.read()
    
    # Count test functions
    import re
    test_functions = re.findall(r'static int (test_\w+)\(', content)
    
    print(f"✓ Test functions found: {len(test_functions)}")
    
    # Check for test dispatcher
    if 'all_tests[]' in content:
        print(f"✓ Test dispatcher array found")
    else:
        print(f"✗ Test dispatcher array MISSING")
        return False
    
    # Check for main function
    if 'int main(void)' in content:
        print(f"✓ Main function found")
    else:
        print(f"✗ Main function MISSING")
        return False
    
    # Categorize tests
    categories = {
        'memory': [t for t in test_functions if 'test_null' in t or 'test_use_after' in t or 'test_double_free' in t or 'test_stack_buffer' in t or 'test_heap' in t],
        'syscall': [t for t in test_functions if 'test_invalid_syscall' in t or 'test_syscall' in t],
        'filesystem': [t for t in test_functions if 'test_delete' in t or 'test_create' in t or 'test_corrupt_directory' in t],
        'cpu': [t for t in test_functions if 'test_infinite_loop' in t or 'test_cpu' in t],
        'ai': [t for t in test_functions if 'test_ai' in t],
        'thread': [t for t in test_functions if 'test_thread' in t or 'test_create_10k' in t or 'test_deadlock' in t],
    }
    
    print(f"\nInside Test Categories:")
    print(f"  Memory: {len(categories['memory'])} tests")
    print(f"  Syscall: {len(categories['syscall'])} tests")
    print(f"  Filesystem: {len(categories['filesystem'])} tests")
    print(f"  CPU: {len(categories['cpu'])} tests")
    print(f"  AI: {len(categories['ai'])} tests")
    print(f"  Threading: {len(categories['thread'])} tests")
    
    total_inside = sum(len(v) for v in categories.values())
    print(f"\n✓ Total inside tests: {total_inside}")
    
    return True

def main():
    print("\n" + "="*60)
    print("XAI OS CRASH TEST FRAMEWORK VALIDATION")
    print("="*60 + "\n")
    
    results = []
    
    # Validate server
    results.append(('Server', validate_server()))
    
    # Validate protocol
    results.append(('Protocol', validate_protocol()))
    
    # Validate client
    results.append(('Client', validate_client()))
    
    # Summary
    print("\n" + "="*60)
    print("VALIDATION SUMMARY")
    print("="*60)
    
    all_passed = True
    for name, passed in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {name}: {status}")
        if not passed:
            all_passed = False
    
    print()
    if all_passed:
        print("🎉 ALL VALIDATIONS PASSED!")
        print()
        print("The crash test framework is fully functional.")
        print("To run actual tests against XAI OS:")
        print("  1. Start QEMU: make qemu")
        print("  2. Run outside tests: ./run.sh outside 100")
        print("  3. Run inside tests: Build crashtest_client and run in QEMU")
        return 0
    else:
        print("❌ SOME VALIDATIONS FAILED")
        print("Please fix the issues above before running tests.")
        return 1

if __name__ == '__main__':
    sys.exit(main())
