/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>

#include <hal/nrf_memconf.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <ram_pwrdn.h>

#define MODULE ram_pwrdn
#include <caf/events/module_state_event.h>

/* The ram_pwrdn log module name is already used by the RAM power down library. */
LOG_MODULE_REGISTER(desktop_ram_pwrdn, CONFIG_DESKTOP_RAM_PWRDN_LOG_LEVEL);

extern char _image_ram_end[];

#define MEMCONF_BLOCK_ICACHE		33U
#define MEMCONF_BLOCK_USBHS_NRF54LM20	34U
#define MEMCONF_BLOCKS_PER_POWER	32U

#define RAM_SECTION_SIZE		0x8000UL

/* Must match the banks[] table of the RAM power down library (nrf/lib/ram_pwrdn).
 * nRF54LS05 is not supported by the library and its sections are handled directly.
 */
#if defined(CONFIG_SOC_NRF54LS05A_CPUAPP)
#define RAM_SECTION_COUNT		2U
#elif defined(CONFIG_SOC_NRF54LS05B_CPUAPP)
#define RAM_SECTION_COUNT		3U
#elif defined(CONFIG_SOC_NRF54L05_CPUAPP)
#define RAM_SECTION_COUNT		3U
#elif defined(CONFIG_SOC_NRF54L10_CPUAPP) || defined(CONFIG_SOC_NRF54LC10A_CPUAPP)
#define RAM_SECTION_COUNT		6U
#elif defined(CONFIG_SOC_NRF54L15_CPUAPP)
#define RAM_SECTION_COUNT		8U
#elif defined(CONFIG_SOC_NRF54LM20A_CPUAPP) || defined(CONFIG_SOC_NRF54LM20B_CPUAPP)
#define RAM_SECTION_COUNT		16U
#else
#error "Unsupported SoC"
#endif

#define RAM_BANK_START			0x20000000UL
#define RAM_BANK_END			(RAM_BANK_START + (RAM_SECTION_COUNT * RAM_SECTION_SIZE))

#define CPUAPP_SRAM_END			(DT_REG_ADDR(DT_NODELABEL(cpuapp_sram)) + \
					 DT_REG_SIZE(DT_NODELABEL(cpuapp_sram)))

static void memconf_block_power_down(uint8_t block_id)
{
	LOG_INF("Power down MEMCONF block %u", block_id);
	nrf_memconf_ramblock_control_enable_set(NRF_MEMCONF,
						block_id / MEMCONF_BLOCKS_PER_POWER,
						block_id % MEMCONF_BLOCKS_PER_POWER,
						false);
}

static void ram_sections_power_down(uintptr_t start, uintptr_t end)
{
	/* Power down only RAM sections located entirely within the range. */
	uint32_t first = DIV_ROUND_UP(start - RAM_BANK_START, RAM_SECTION_SIZE);
	uint32_t last = (end - RAM_BANK_START) / RAM_SECTION_SIZE;

	if (first < last) {
		nrf_memconf_ramblock_control_mask_enable_set(NRF_MEMCONF, 0,
							     GENMASK(last - 1, first),
							     false);
	}
}

static void app_ram_tail_power_down(void)
{
	/* The bank end can be above cpuapp_sram (for example, by 1 KiB on nRF54LM20).
	 * CONFIG_DESKTOP_RAM_PWRDN_LIMIT_TO_CPUAPP_SRAM keeps that part of the last RAM
	 * section powered.
	 */
	uintptr_t end = IS_ENABLED(CONFIG_DESKTOP_RAM_PWRDN_LIMIT_TO_CPUAPP_SRAM) ?
			MIN(RAM_BANK_END, CPUAPP_SRAM_END) : RAM_BANK_END;
	uintptr_t start = (uintptr_t)_image_ram_end;

	if (start < end) {
		LOG_INF("Power down unused SRAM 0x%08lx-0x%08lx",
			(unsigned long)start, (unsigned long)end);
		if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
			power_down_ram(start, end);
		} else {
			ram_sections_power_down(start, end);
		}
	}
}

/* MEMCONF block 32 (VPR context restore) is not handled, because it is a retention
 * feature bit and not a powered SRAM block. The CRACEN SRAM blocks (PKE code and
 * KeyRAM) are never powered down for security reasons.
 */
static void special_memconf_blocks_power_down(void)
{
	if (IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) &&
	    !IS_ENABLED(CONFIG_DESKTOP_RAM_PWRDN_ICACHE_IN_USE)) {
		/* Code executes from SRAM, so the instruction cache is not needed.
		 * The cache must be disabled before its MEMCONF block is turned off.
		 */
		sys_cache_instr_disable();
		memconf_block_power_down(MEMCONF_BLOCK_ICACHE);
	}

	if ((IS_ENABLED(CONFIG_SOC_NRF54LM20A_CPUAPP) ||
	     IS_ENABLED(CONFIG_SOC_NRF54LM20B_CPUAPP)) &&
	    !IS_ENABLED(CONFIG_DESKTOP_RAM_PWRDN_USB_HS_IN_USE)) {
		memconf_block_power_down(MEMCONF_BLOCK_USBHS_NRF54LM20);
	}
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event =
				cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			static bool initialized;

			__ASSERT_NO_MSG(!initialized);
			initialized = true;

			app_ram_tail_power_down();
			special_memconf_blocks_power_down();
			module_set_state(MODULE_STATE_READY);
		}

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, module_state_event);
