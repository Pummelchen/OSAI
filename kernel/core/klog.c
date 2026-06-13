#include <stdarg.h>
#include <osai/klog.h>
#include <osai/types.h>

#define PL011_UARTDR 0x00U

static volatile uint32_t *g_uart_base;

static void uart_putc(char c) {
  if (g_uart_base == 0) {
    return;
  }

  g_uart_base[PL011_UARTDR / 4] = (uint32_t)c;
}

static void klog_char(char c) {
  if (c == '\n') {
    uart_putc('\r');
  }
  uart_putc(c);
}

void klog_init(const osai_boot_info_t *boot) {
  g_uart_base = (volatile uint32_t *)(uintptr_t)boot->uart_base;
}

void klog_puts(const char *message) {
  while (*message != '\0') {
    klog_char(*message++);
  }
}

static void klog_u64(uint64_t value, unsigned base) {
  char buffer[32];
  unsigned index = 0;

  if (value == 0) {
    klog_char('0');
    return;
  }

  while (value != 0 && index < sizeof(buffer)) {
    unsigned digit = (unsigned)(value % base);
    buffer[index++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    value /= base;
  }

  while (index != 0) {
    klog_char(buffer[--index]);
  }
}

void klog(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  for (const char *p = fmt; *p != '\0'; ++p) {
    if (*p != '%') {
      klog_char(*p);
      continue;
    }

    ++p;
    if (*p == '\0') {
      break;
    }

    if (*p == 's') {
      const char *s = va_arg(args, const char *);
      klog_puts(s == 0 ? "(null)" : s);
    } else if (*p == 'u') {
      klog_u64((uint64_t)va_arg(args, unsigned), 10);
    } else if (*p == 'x') {
      klog_u64((uint64_t)va_arg(args, unsigned), 16);
    } else if (*p == 'p') {
      klog_puts("0x");
      klog_u64((uint64_t)(uintptr_t)va_arg(args, void *), 16);
    } else if (*p == 'l' && p[1] == 'u') {
      ++p;
      klog_u64(va_arg(args, uint64_t), 10);
    } else if (*p == 'l' && p[1] == 'x') {
      ++p;
      klog_u64(va_arg(args, uint64_t), 16);
    } else if (*p == '%') {
      klog_char('%');
    } else {
      klog_char('%');
      klog_char(*p);
    }
  }

  va_end(args);
}
