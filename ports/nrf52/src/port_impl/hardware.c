// Copyright 2019 jem@seethis.link
// Licensed under the MIT license (http://opensource.org/licenses/MIT)

#include "core/hardware.h"

#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_nvic.h"

#include "serial_num.h"

void hardware_init(void) {
    nrf52_init_serial_number();
}

void bootloader_jmp(void) {
#if BOOTLOADER_RESET_PIN
    NRF_LOG_FINAL_FLUSH();
    nrf_gpio_cfg_output(BOOTLOADER_RESET_PIN);
    nrf_gpio_pin_clear(BOOTLOADER_RESET_PIN);
    while (1) { }
#endif
}

void reset_mcu(void) {
#if HAS_SOFTDEVICE
    sd_nvic_systemreset();
#else
    NVIC_SystemReset();
#endif
}

void wdt_kick(void) {
    // TODO:
}
