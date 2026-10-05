#include "flash_settings.h"

#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

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

void settings_save(void) {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);
    memcpy(buffer, &sys_config, sizeof(ConfigSettings));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}
