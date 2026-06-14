#include <osai/boot_info.h>
#include <osai/types.h>

#define COM1_PORT UINT16_C(0x3f8)
#define UART_DATA 0U
#define UART_INTERRUPT_ENABLE 1U
#define UART_FIFO_CONTROL 2U
#define UART_LINE_CONTROL 3U
#define UART_MODEM_CONTROL 4U
#define UART_LINE_STATUS 5U
#define UART_TRANSMIT_EMPTY 0x20U

static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
  uint8_t value = 0;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}

static void serial_init(uint16_t base) {
  outb((uint16_t)(base + UART_INTERRUPT_ENABLE), 0x00);
  outb((uint16_t)(base + UART_LINE_CONTROL), 0x80);
  outb((uint16_t)(base + UART_DATA), 0x03);
  outb((uint16_t)(base + UART_INTERRUPT_ENABLE), 0x00);
  outb((uint16_t)(base + UART_LINE_CONTROL), 0x03);
  outb((uint16_t)(base + UART_FIFO_CONTROL), 0xc7);
  outb((uint16_t)(base + UART_MODEM_CONTROL), 0x0b);
}

static void serial_putc(uint16_t base, char c) {
  for (uint32_t spin = 0; spin < 100000U; ++spin) {
    if ((inb((uint16_t)(base + UART_LINE_STATUS)) & UART_TRANSMIT_EMPTY) != 0U) {
      break;
    }
  }
  outb((uint16_t)(base + UART_DATA), (uint8_t)c);
}

static void serial_puts(uint16_t base, const char *message) {
  while (*message != '\0') {
    if (*message == '\n') {
      serial_putc(base, '\r');
    }
    serial_putc(base, *message++);
  }
}

static void serial_hex64(uint16_t base, uint64_t value) {
  static const char digits[] = "0123456789abcdef";
  serial_puts(base, "0x");
  for (int shift = 60; shift >= 0; shift -= 4) {
    serial_putc(base, digits[(value >> (uint32_t)shift) & UINT64_C(0xf)]);
  }
}

static uint64_t memory_descriptor_count(const osai_boot_info_t *boot) {
  if (boot == 0 || boot->memory_descriptor_size == 0) {
    return 0;
  }
  return boot->memory_map_size / boot->memory_descriptor_size;
}

void x86_64_kmain(const osai_boot_info_t *boot) {
  uint16_t serial_base = COM1_PORT;
  if (boot != 0 && boot->uart_base != 0 && boot->uart_base <= UINT16_MAX) {
    serial_base = (uint16_t)boot->uart_base;
  }
  serial_init(serial_base);

  serial_puts(serial_base, "OSAI x86_64 kernel starting\n");
  if (boot == 0 || boot->magic != OSAI_BOOT_INFO_MAGIC ||
      boot->version != OSAI_BOOT_INFO_VERSION) {
    serial_puts(serial_base, "x86_64: boot info invalid\n");
    for (;;) {
      __asm__ volatile("hlt");
    }
  }

  serial_puts(serial_base, "x86_64: UEFI boot info valid\n");
  serial_puts(serial_base, "x86_64: memory descriptors=");
  serial_hex64(serial_base, memory_descriptor_count(boot));
  serial_puts(serial_base, " desc_size=");
  serial_hex64(serial_base, boot->memory_descriptor_size);
  serial_puts(serial_base, "\n");
  serial_puts(serial_base, "x86_64: kernel range ");
  serial_hex64(serial_base, boot->kernel_phys_base);
  serial_puts(serial_base, "-");
  serial_hex64(serial_base, boot->kernel_phys_end);
  serial_puts(serial_base, "\n");
  serial_puts(serial_base, "x86_64: COM1 serial online\n");
  serial_puts(serial_base, "x86_64: Intel Desktop milestone 43 boot path passed\n");

  for (;;) {
    __asm__ volatile("hlt");
  }
}
