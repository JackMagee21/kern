#include "serial.h"
#include "vga.h"
#include "io.h"
#include <stdint.h>

/*
 * 16550 UART register offsets from the base port.
 * COM1 base = 0x3F8.
 */
#define COM1          0x3F8u

#define UART_DATA     0u   /* read: receive buffer, write: transmit buffer  */
#define UART_IER      1u   /* interrupt enable register                     */
#define UART_FCR      2u   /* FIFO control register (write)                 */
#define UART_LCR      3u   /* line control register                         */
#define UART_MCR      4u   /* modem control register                        */
#define UART_LSR      5u   /* line status register (read)                   */

#define LCR_DLAB      0x80u  /* divisor latch access bit (set to program baud) */
#define LCR_8N1       0x03u  /* 8 data bits, no parity, 1 stop bit            */
#define LSR_THRE      0x20u  /* transmit-holding-register empty                */
#define MCR_DTR_RTS   0x03u  /* data-terminal-ready + request-to-send          */
#define FCR_ENABLE    0xC7u  /* enable + clear FIFOs, 14-byte trigger level    */

/* Divisor for 115200 baud: PIT_BASE_HZ-like base is 1.8432 MHz → div = 1. */
#define BAUD_DIV_LO   0x01u
#define BAUD_DIV_HI   0x00u

static inline void uart_out(uint16_t reg, uint8_t val) {
    outb((uint16_t)(COM1 + reg), val);
}
static inline uint8_t uart_in(uint16_t reg) {
    return inb((uint16_t)(COM1 + reg));
}

void serial_init(void) {
    uart_out(UART_IER, 0x00);             /* disable all interrupts           */

    uart_out(UART_LCR, LCR_DLAB);        /* enable DLAB to set baud rate     */
    uart_out(UART_DATA, BAUD_DIV_LO);    /* divisor low  byte: 115200 baud   */
    uart_out(UART_IER,  BAUD_DIV_HI);    /* divisor high byte                */

    uart_out(UART_LCR, LCR_8N1);         /* 8N1, clear DLAB                  */
    uart_out(UART_FCR, FCR_ENABLE);      /* enable + reset FIFOs             */
    uart_out(UART_MCR, MCR_DTR_RTS);     /* assert DTR and RTS               */

    /* Mirror all terminal_putchar output to this port. */
    terminal_set_output_hook(serial_putchar);
}

void serial_putchar(char c) {
    /* Wait until the transmit holding register is free. */
    while (!(uart_in(UART_LSR) & LSR_THRE));
    /* QEMU / real hardware: send LF as CR+LF for sane line endings. */
    if (c == '\n') {
        uart_out(UART_DATA, '\r');
        while (!(uart_in(UART_LSR) & LSR_THRE));
    }
    uart_out(UART_DATA, (uint8_t)c);
}

void serial_print(const char *s) {
    while (*s) serial_putchar(*s++);
}
