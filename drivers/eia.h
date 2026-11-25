/**
 * drivers/eia.h — the RP2040's UART as a serial link to another board
 *
 * The chip: the RP2040's own PL011 UART (EIA0_UART in board.h, pins and
 * speed there too): 32-byte FIFOs each way, hardware RTS/CTS. Named
 * after Plan 9's serial ports, /dev/eia0, /dev/eia1 (EIA: the
 * Electronic Industries Alliance, of RS-232).
 *
 * Receiving: an interrupt moves bytes from the FIFO into a ring buffer
 * (EIA_RXBUF bytes); eia_read() takes what has arrived and never waits.
 * When the ring buffer is full the interrupt stops emptying the FIFO, the
 * FIFO fills, and with flow control on the UART drops RTS by itself: the
 * other board stops sending until eia_read() makes room. No byte is lost
 * between two plan2040 boards; without flow control a full buffer drops
 * bytes and counts them.
 *
 * Sending: eia_write() fills the TX FIFO as it empties. It may take what
 * its bytes need at the line speed (1 KB at 1 Mbaud: ~10 ms), plus at
 * most EIA_TX_SLACK_MS waiting for CTS; then it gives up and says why
 * (the other side isn't connected, powered or reading), never hangs.
 *
 * Plain C++ subset, the driver contract (drivers/README.md).
 */
#pragma once

#include <stdint.h>

#ifndef EIA_RXBUF
#define EIA_RXBUF       2048  // power of 2
#endif
#define EIA_TX_SLACK_MS 50

// Set up the UART, its pins, the interrupt; flow control on. True, or
// false with the reason in errstr().
bool eia_init(void);

// Bytes received since the last call, at most len: 0 if none. Never waits.
int eia_read(char *buf, int len);

// Send all len bytes: len, or -1 with the reason in errstr() (and nothing
// more sent after that point).
int eia_write(const char *buf, int len);

// Line speed (300..7800000) and hardware flow control (RTS/CTS).
bool eia_set_baud(uint32_t baud);
void eia_set_flow(bool on);

// One line: "b1000000 m1 cts on rx 12 tx 6 overrun 0 framing 0 break 0 dropped 0\n".
int eia_status(char *buf, int len);
