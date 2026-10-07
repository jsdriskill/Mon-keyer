#include "flash_settings.h"

#include <string.h>
#include "hardware/flash.h"
#include "pico/flash.h"

#define FLASH_TARGET_OFFSET (1024 * 1024)
#define CONFIG_MAGIC 0x4B

ConfigSettings sys_config;

void settings_init(void) {
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    memcpy(&sys_config, flash_target_contents, sizeof(ConfigSettings));

    if (sys_config.magic != CONFIG_MAGIC) {
        sys_config.magic = CONFIG_MAGIC;
        sys_config.wpm = 20;
        sys_config.sidetone_en = 1;
        sys_config.sidetone_freq = 700;
        sys_config.weighting = 50;
        sys_config.dash_ratio = 30;
        sys_config.mode = KEYER_MODE_IAMBIC_B;
        sys_config.paddle_swap = 0;
        sys_config.autospace = 0;
        sys_config.first_extension = 0;
        settings_save();
    }
}

static void do_flash_write(void *param) {
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)param, FLASH_PAGE_SIZE);
}

void settings_save(void) {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);
    memcpy(buffer, &sys_config, sizeof(ConfigSettings));

    /* Core 1 runs the USB stack from flash, which is unreadable while flash
     * is erased or programmed, so it has to be paused first. flash_safe_execute()
     * does that (core 1 registers itself in usb_device.c) and also disables
     * interrupts on this core. USB audio drops out for the ~50 ms this takes. */
    flash_safe_execute(do_flash_write, buffer, 500);
}
