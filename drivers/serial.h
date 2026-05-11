#ifndef SERIAL_H
#define SERIAL_H

/*
 * Minimal 16550 UART driver for COM1 (0x3F8).
 * After serial_init() all output through terminal_putchar() (and therefore
 * kprintf, terminal_print, kpanic) is mirrored to the serial port in addition
 * to the VGA screen.
 *
 * QEMU: add  -serial stdio  to see the output on the host terminal.
 */

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *s);

#endif
