#ifndef OSAI_PANIC_H
#define OSAI_PANIC_H

void panic_at(const char *file, int line, const char *fmt, ...);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#endif
