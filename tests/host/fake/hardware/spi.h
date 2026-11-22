#pragma once
// Fake RP2040 SPI for the PC tests: counts the bytes written (bwsim
// reports how much went to the TFT).
#include <stdint.h>
#include <stddef.h>
typedef struct spi_inst spi_inst_t;
extern spi_inst_t *fake_spi0;
extern unsigned long fake_spi_bytes;
#define spi0 fake_spi0
static inline unsigned spi_init(spi_inst_t *s, unsigned baud) { (void)s; return baud; }
static inline int spi_write_blocking(spi_inst_t *s, const uint8_t *src, size_t len) { (void)s; (void)src; fake_spi_bytes += len; return (int)len; }
