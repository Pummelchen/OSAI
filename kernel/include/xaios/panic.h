#ifndef XAIOS_PANIC_H
#define XAIOS_PANIC_H

/*
 * Kernel panic — cyan screen of death.
 *
 * On fatal error: dumps registers + stack trace to UART with ANSI cyan
 * background, then halts forever (no auto-reboot) so the operator can
 * read the diagnostics.
 */

void panic_at(const char *file, int line, const char *fmt, ...);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#endif
#ifndef XAIOS_PANIC_H
#define XAIOS_PANIC_H

void panic_at(const char *file, int line, const char *fmt, ...);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#endif
