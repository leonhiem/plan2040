/**
 * drivers/eia.cpp — the RP2040's UART as a serial link (see eia.h)
 *
 * From the RP2040 datasheet, chapter 4.2 (the ARM PL011 UART):
 * - DR: bits 7..0 the byte, bit 8 framing error, 9 parity, 10 break,
 *   11 overrun (the 32-byte RX FIFO was full and a byte was lost).
 * - Auto-RTS: with RTSEN the UART holds RTS asserted (low) until the RX
 *   FIFO reaches its watermark (IFLS RXIFLSEL); auto-CTS: with CTSEN it
 *   sends only while CTS is asserted (low).
 * - RX interrupt at the watermark, RX timeout interrupt when bytes sit in
 *   the FIFO and the line has been quiet for 32 bit times.
 * Watermark 1/2 (16 bytes): an interrupt per 16 bytes at full speed, and
 * RTS drops with 16 bytes of room left, plenty for the other side's
 * UART to stop after the byte it's sending.
 */
#include "drivers/eia.h"
#include "kernel/errstr.h"
#include "board.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"
#include <cstdio>
#include <cstring>

#define RXMASK (EIA_RXBUF - 1)

static char rxbuf[EIA_RXBUF];
static volatile uint32_t rx_head;    // written by the interrupt only
static volatile uint32_t rx_tail;    // written by eia_read() only
static volatile bool rx_stalled;     // the interrupt stopped: buffer full

static uint32_t baud = EIA0_BAUD;
static bool flow = true;
static volatile uint32_t n_rx, n_overrun, n_framing, n_break, n_dropped;
static uint32_t n_tx;

static uart_hw_t *hw(void) { return uart_get_hw(EIA0_UART); }

static void rx_irq_on(bool on)
{
    const uint32_t bits = UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS;
    if (on) hw_set_bits(&hw()->imsc, bits);
    else    hw_clear_bits(&hw()->imsc, bits);
}

static void eia_irq(void)
{
    while (!(hw()->fr & UART_UARTFR_RXFE_BITS)) {
        if (rx_head - rx_tail >= EIA_RXBUF) {
            if (flow) {             // leave the rest in the FIFO: RTS drops
                rx_stalled = true;
                rx_irq_on(false);
                return;
            }
            (void)hw()->dr;          // no flow control: nowhere to put it
            n_dropped++;
            continue;
        }
        uint32_t d = hw()->dr;
        if (d & UART_UARTDR_OE_BITS) n_overrun++;
        if (d & UART_UARTDR_BE_BITS) { n_break++; continue; }   // not a byte
        if (d & UART_UARTDR_FE_BITS) { n_framing++; continue; } // garbled
        rxbuf[rx_head & RXMASK] = (char)(d & 0xff);
        rx_head = rx_head + 1;
        n_rx++;
    }
}

bool eia_init(void)
{
    uart_init(EIA0_UART, baud);
    uart_set_format(EIA0_UART, 8, 1, UART_PARITY_NONE);
    gpio_set_function(EIA0_PIN_TX,  GPIO_FUNC_UART);
    gpio_set_function(EIA0_PIN_RX,  GPIO_FUNC_UART);
    gpio_set_function(EIA0_PIN_CTS, GPIO_FUNC_UART);
    gpio_set_function(EIA0_PIN_RTS, GPIO_FUNC_UART);
    gpio_pull_up(EIA0_PIN_RX);
    gpio_pull_up(EIA0_PIN_CTS);
    uart_set_hw_flow(EIA0_UART, flow, flow);
    uart_set_fifo_enabled(EIA0_UART, true);
    hw_write_masked(&hw()->ifls, 2u << UART_UARTIFLS_RXIFLSEL_LSB,
                    UART_UARTIFLS_RXIFLSEL_BITS);              // 1/2 full

    int irq = uart_get_index(EIA0_UART) == 0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, eia_irq);
    irq_set_enabled(irq, true);
    rx_irq_on(true);
    return true;
}

int eia_read(char *buf, int len)
{
    int n = 0;
    while (n < len && rx_tail != rx_head) {
        buf[n++] = rxbuf[rx_tail & RXMASK];
        rx_tail = rx_tail + 1;
    }
    if (rx_stalled && rx_head - rx_tail < EIA_RXBUF) {   // room again
        rx_stalled = false;
        rx_irq_on(true);
    }
    return n;
}

static bool cts_on(void) { return !gpio_get(EIA0_PIN_CTS); }  // active low

int eia_write(const char *buf, int len)
{
    // The bytes' own time at this speed (10 bits each), plus the slack.
    uint64_t us = (uint64_t)len * 10 * 1000000 / baud + EIA_TX_SLACK_MS * 1000;
    absolute_time_t deadline = make_timeout_time_us(us);
    for (int i = 0; i < len; i++) {
        while (!uart_is_writable(EIA0_UART)) {
            if (time_reached(deadline)) {
                // Short: errstr holds ERRMAX (96) characters, the path
                // ("/dev/eia0: ") included; the full story is in eia.h.
                if (flow && !cts_on())
                    werrstr("CTS off: other board not reading, off or unwired; %d of %d sent",
                            i, len);
                else
                    werrstr("%s TX stuck; %d of %d sent", EIA0_NAME, i, len);
                n_tx += i;
                return -1;
            }
        }
        uart_putc_raw(EIA0_UART, buf[i]);
    }
    n_tx += len;
    return len;
}

bool eia_set_baud(uint32_t b)
{
    if (b < 300 || b > 7800000) {
        werrstr("speed %lu out of range (300-7800000)", (unsigned long)b);
        return false;
    }
    baud = uart_set_baudrate(EIA0_UART, b);
    return true;
}

void eia_set_flow(bool on)
{
    flow = on;
    uart_set_hw_flow(EIA0_UART, on, on);
    if (!on && rx_stalled) { rx_stalled = false; rx_irq_on(true); }
}

int eia_status(char *buf, int len)
{
    return snprintf(buf, len,
        "b%lu m%d cts %s rx %lu tx %lu overrun %lu framing %lu break %lu dropped %lu\n",
        (unsigned long)baud, flow ? 1 : 0, cts_on() ? "on" : "off",
        (unsigned long)n_rx, (unsigned long)n_tx, (unsigned long)n_overrun,
        (unsigned long)n_framing, (unsigned long)n_break, (unsigned long)n_dropped);
}
