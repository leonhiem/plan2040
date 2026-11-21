/*
 * Copyright (c) 2021-2022 Antonio González
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * plan2040 changes (2026-11-21, marked "plan2040:" below; see README.md
 * here): every I2C transfer has a timeout, the wait for a conversion has
 * a limit, and the functions that talk to the chip return true/false.
 * Without that, a missing or badly wired ADS1115 left config holding
 * garbage, and the busy-wait loop could spin until the watchdog reset
 * the board, again after every reboot.
 */

#include "ads1115.h"
#include "pico/time.h"

// plan2040: one transfer is 1-3 bytes, ~0.2 ms at 200 kHz.
#define ADS1115_I2C_TIMEOUT_US 10000
// plan2040: a conversion at 8 SPS (the slowest rate) takes 125 ms.
#define ADS1115_CONV_TIMEOUT_US 200000

bool ads1115_init(i2c_inst_t *i2c_port, uint8_t i2c_addr,
                  ads1115_adc_t *adc) {
    adc->i2c_port = i2c_port;
    adc->i2c_addr = i2c_addr;
    return ads1115_read_config(adc);
}


bool ads1115_read_adc(uint16_t *adc_value, ads1115_adc_t *adc){
    // If mode is single-shot, set bit 15 to start the conversion.
    if ((adc->config & ADS1115_MODE_MASK) == ADS1115_MODE_SINGLE_SHOT) {
        adc->config |= 0x8000;//ADS1115_STATUS_START;
        if (!ads1115_write_config(adc))                       // plan2040
            return false;

        // Wait until the conversion finishes before reading the value
        // plan2040: at most ADS1115_CONV_TIMEOUT_US, and only on good reads.
        absolute_time_t deadline = make_timeout_time_us(ADS1115_CONV_TIMEOUT_US);
        do {
            if (!ads1115_read_config(adc) || time_reached(deadline))
                return false;
        } while ((adc->config & ADS1115_STATUS_MASK) == ADS1115_STATUS_BUSY);
    }

    // Now read the value from last conversion
    uint8_t dst[2];
    if (i2c_write_timeout_us(adc->i2c_port, adc->i2c_addr,      // plan2040
                             &ADS1115_POINTER_CONVERSION, 1, true,
                             ADS1115_I2C_TIMEOUT_US) != 1)
        return false;
    if (i2c_read_timeout_us(adc->i2c_port, adc->i2c_addr, dst, 2,
                            false, ADS1115_I2C_TIMEOUT_US) != 2)
        return false;
    *adc_value = (dst[0] << 8) | dst[1];
    return true;
}

float ads1115_raw_to_volts(uint16_t adc_value, ads1115_adc_t *adc) {
    // Determine the full-scale voltage range (FSR) based on the 
    // PGA set in the configuration.
    float fsr;
    uint16_t pga = adc->config & ADS1115_PGA_MASK;
    switch (pga) {
        case ADS1115_PGA_6_144:
            fsr = 6.144;
            break;
        case ADS1115_PGA_4_096:
            fsr = 4.096;
            break;
        case ADS1115_PGA_2_048:
            fsr = 2.048;
            break;
        case ADS1115_PGA_1_024:
            fsr = 1.024;
            break;
        case ADS1115_PGA_0_512:
            fsr = 0.512;
            break;
        case ADS1115_PGA_0_256:
        default:   // plan2040: PGA codes 110 and 111 are +-0.256 V too (datasheet);
                   // without this, fsr was left unset for them
            fsr = 0.256;
            break;
    }

    // Convert the ADC value from two's complement to voltage
    float voltage;
    if (adc_value & 0x8000) {
        adc_value = (adc_value ^ 0xffff) + 1;
        voltage = -fsr * (float)adc_value / 0x8000;
    } else {
        voltage = fsr * (float)adc_value / 0x8000;
    }
    return voltage;
}

bool ads1115_read_config(ads1115_adc_t *adc){
    // Default configuration after power up should be 34179.
    // Default config with bit 15 cleared is 1411
    uint8_t dst[2];
    // plan2040: timeouts; config is left alone when a transfer fails.
    if (i2c_write_timeout_us(adc->i2c_port, adc->i2c_addr,
                             &ADS1115_POINTER_CONFIGURATION, 1, true,
                             ADS1115_I2C_TIMEOUT_US) != 1)
        return false;
    if (i2c_read_timeout_us(adc->i2c_port, adc->i2c_addr, dst, 2,
                            false, ADS1115_I2C_TIMEOUT_US) != 2)
        return false;
    adc->config = (dst[0] << 8) | dst[1];
    return true;
}

bool ads1115_write_config(ads1115_adc_t *adc) {
    uint8_t src[3];
    src[0] = ADS1115_POINTER_CONFIGURATION;
    src[1] = (uint8_t)(adc->config >> 8);
    src[2] = (uint8_t)(adc->config & 0xff);
    return i2c_write_timeout_us(adc->i2c_port, adc->i2c_addr, src, 3,   // plan2040
                                false, ADS1115_I2C_TIMEOUT_US) == 3;
}

void ads1115_set_input_mux(enum ads1115_mux_t mux, ads1115_adc_t *adc) {
    adc->config &= ~ADS1115_MUX_MASK;
    adc->config |= mux;
}

void ads1115_set_pga(enum ads1115_pga_t pga, ads1115_adc_t *adc){
    adc->config &= ~ADS1115_PGA_MASK;
    adc->config |= pga;
}

void ads1115_set_operating_mode(enum ads1115_mode_t mode,
                                ads1115_adc_t *adc) {
    adc->config &= ~ADS1115_MODE_MASK;
    adc->config |= mode;
}

void ads1115_set_data_rate(enum ads1115_rate_t rate,
                           ads1115_adc_t *adc) {
    adc->config &= ~ADS1115_RATE_MASK;
    adc->config |= rate;
}
