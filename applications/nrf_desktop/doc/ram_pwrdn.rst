.. _nrf_desktop_ram_pwrdn:

RAM power down module
#####################

.. contents::
   :local:
   :depth: 2

The RAM power down module powers down unused SRAM to reduce the current consumption in the System ON mode.
The module is supported on the nRF54L05, nRF54L10, nRF54L15, nRF54LC10, nRF54LM20, and nRF54LS05 application cores.

Module events
*************

.. include:: event_propagation.rst
    :start-after: table_ram_pwrdn_start
    :end-before: table_ram_pwrdn_end

.. note::
    |nrf_desktop_module_event_note|

Configuration
*************

To enable the module, use the :option:`CONFIG_DESKTOP_RAM_PWRDN` Kconfig option.
The option is enabled by default on the SoCs that meet dependencies.
The module is not available in most configurations, because the malloc arena is placed in the SRAM it would power down

The module uses the :ref:`nrf:lib_ram_pwrdn` (:kconfig:option:`CONFIG_RAM_POWER_DOWN_LIBRARY`), which is automatically selected.
The library does not support nRF54LS05.
On nRF54LS05, the module powers down the RAM sections directly using MEMCONF and implies the :kconfig:option:`CONFIG_NRF_FORCE_RAM_ON_REBOOT` Kconfig option.
The option powers on all RAM sections before a soft reset, because the powered down RAM sections stay off through the reset.

MEMCONF can be accessed only from the secure domain.
For this reason, the module is not available if the application is built with TF-M (:kconfig:option:`CONFIG_BUILD_WITH_TFM`).

The module is also not available if the ``malloc`` arena is placed right after the linked application image, in the SRAM that the module would power down.
This is the case if the :kconfig:option:`CONFIG_COMMON_LIBC_MALLOC` Kconfig option is enabled and the :kconfig:option:`CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE` Kconfig option is set to ``-1``, or if Newlib (:kconfig:option:`CONFIG_NEWLIB_LIBC`) is used.

If the FLPR core is enabled in devicetree (``cpuflpr_vpr`` node), the :option:`CONFIG_DESKTOP_RAM_PWRDN_LIMIT_TO_CPUAPP_SRAM` Kconfig option is enabled by default.
The option limits the powered down SRAM to the end of the ``cpuapp_sram`` devicetree node.
On some SoCs (for example, nRF54LM20), the last RAM section extends beyond ``cpuapp_sram`` and holds the VPR context.

Implementation details
**********************

The module powers down SRAM when the :ref:`nrf_desktop_main` reports ``MODULE_STATE_READY`` using :c:struct:`module_state_event`.
Then, the module reports ``MODULE_STATE_READY``.

The module powers down the following SRAM:

* RAM sections located after the linked application image (``_image_ram_end``).
  Only complete 32 KiB RAM sections are powered down.
* The instruction cache SRAM (MEMCONF block 33), if the :kconfig:option:`CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD` Kconfig option is enabled.
  In the RAM load mode, the application code executes from SRAM and the instruction cache is not needed.
  The module disables the instruction cache before powering down its SRAM.
* The USB high-speed SRAM (MEMCONF block 34) on nRF54LM20, if the application does not use the USB high-speed controller (:option:`CONFIG_DESKTOP_RAM_PWRDN_USB_HS_IN_USE`).

The module never powers down the SRAM of CRACEN.
