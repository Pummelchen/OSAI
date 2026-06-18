# Implement userspace SSH daemon with custom freestanding crypto

_Source: coding plans from commit period 43c873f → 8083940 — records intent at planning time; the implementation may lag or differ._

**Status:** accepted

## Context
OSAI requires remote access (SSH) for production readiness. The environment is freestanding (no libc, no standard crypto libraries). The daemon must run in userspace to leverage the new ELF loader and network stack.

## Decision drivers
- No external library dependencies (freestanding constraint)
- Self-containment of security primitives
- Research/testing focus allowing simplified auth

## Considered options
- **Link against external crypto library (e.g., OpenSSL/mbedTLS)** _(rejected)_ — pros: Battle-tested, comprehensive; cons: Violates freestanding/no-libc constraint, adds significant binary bloat and dependency complexity
- **Custom freestanding crypto implementation** — pros: Zero external dependencies, full control over implementation, fits OSAI's bare-metal nature; cons: High development effort (~800 lines), potential for subtle bugs, limited to specific algorithms (Curve25519, AES-128-CTR, SHA-256)

## Decision
Implement SSH-2 protocol support in a userspace daemon (`sshd`) using custom, freestanding C implementations of SHA-256, HMAC-SHA256, AES-128-CTR, and Curve25519. Use hardcoded credentials and a baked-in Ed25519 test keypair for initial bring-up.

## Consequences
The codebase gains ~800 lines of custom crypto code that must be maintained and self-tested against RFC vectors. Security is limited to 'research/testing' status initially due to hardcoded credentials and lack of peer review on custom crypto.