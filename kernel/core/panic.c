#include <stdarg.h>
#include <osai/klog.h>
#include <osai/panic.h>

void panic_at(const char *file, int line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  klog("\nPANIC at %s:%u: ", file, (unsigned)line);
  for (const char *p = fmt; *p != '\0'; ++p) {
    if (*p == '%' && p[1] == 's') {
      ++p;
      klog("%s", va_arg(args, const char *));
    } else if (*p == '%' && p[1] == 'u') {
      ++p;
      klog("%u", va_arg(args, unsigned));
    } else {
      char tmp[2] = {*p, '\0'};
      klog_puts(tmp);
    }
  }
  va_end(args);
  klog("\n");

  for (;;) {
    __asm__ volatile("wfe");
  }
}
