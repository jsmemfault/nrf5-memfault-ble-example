# Memfault nRF5 SDK example: nRF5 SDK 17.1.0 + S140 7.3.0 + Memfault Firmware SDK (nRF52840-DK)
#
# Based on the nRF5 SDK 17.1.0 ble_app_template (pca10056/s140) Makefile.
# Dependencies live in ../deps (run ../scripts/setup.sh once).

PROJECT_NAME     := nrf5_memfault_demo
APP_TARGET       := nrf5_memfault_demo_nrf52840_s140
TARGETS          := $(APP_TARGET)
OUTPUT_DIRECTORY := _build

SDK_ROOT ?= ../deps/nRF5_SDK_17.1.0_ddde560
PROJ_DIR := src
SD_HEX   ?= ../deps/s140_nrf52_7.3.0/s140_nrf52_7.3.0_softdevice.hex

# Firmware version reported to Memfault (device info + release tracking)
APP_FW_VERSION ?= 1.0.0
# 1 = the diagnostics characteristic requires an encrypted (bonded) link. 0 = open (for gateways
# that can't complete pairing unattended, e.g. macOS + bleak).
DIAG_REQUIRE_BOND ?= 1
# Memfault heartbeat interval (s). Production default is 3600. 300 keeps charts lively for a
# demo without tripping per-device rate limits.
HEARTBEAT_SECS ?= 300

$(OUTPUT_DIRECTORY)/$(APP_TARGET).out: \
  LINKER_SCRIPT  := app.ld

#
# Memfault SDK integration
#
MEMFAULT_SDK_ROOT  ?= $(abspath ../deps/memfault-firmware-sdk)
MEMFAULT_PORT_ROOT := $(abspath third_party/memfault)

# "demo" is only needed for the console commands (crash triggers, chunk export)
MEMFAULT_COMPONENTS := core demo panics util metrics
include $(MEMFAULT_SDK_ROOT)/makefiles/MemfaultWorker.mk
MEMFAULT_NRF5_SDK_PORT_ROOT := $(MEMFAULT_SDK_ROOT)/ports/nrf5_sdk

SRC_FILES += \
  $(MEMFAULT_COMPONENTS_SRCS) \
  $(MEMFAULT_NRF5_SDK_PORT_ROOT)/nrf5_coredump_regions.c \
  $(MEMFAULT_NRF5_SDK_PORT_ROOT)/nrf5_coredump_storage.c \
  $(MEMFAULT_PORT_ROOT)/app_reboot_tracking.c \
  $(MEMFAULT_PORT_ROOT)/memfault_platform_port.c \
  $(MEMFAULT_PORT_ROOT)/nrf5_software_watchdog_sd.c \
  $(MEMFAULT_PORT_ROOT)/nrf_log_backend_memfault.c \
  $(MEMFAULT_PORT_ROOT)/app_memfault_timer.c \

INC_FOLDERS += \
  $(MEMFAULT_COMPONENTS_INC_FOLDERS) \
  $(MEMFAULT_SDK_ROOT)/ports/include \
  $(MEMFAULT_PORT_ROOT) \
  $(MEMFAULT_PORT_ROOT)/sdk_overrides \
  $(PROJ_DIR) \
  $(SDK_ROOT)/components/libraries/bootloader \
  $(SDK_ROOT)/components/libraries/bootloader/dfu \
  $(SDK_ROOT)/components/libraries/bootloader/ble_dfu \
  $(SDK_ROOT)/components/libraries/svc \
  $(SDK_ROOT)/components/libraries/cli/uart \

# GNU build ID ties each uploaded .elf to the exact binary on the device
LDFLAGS += -Wl,--build-id
# Memfault compact logs need GNU C extensions; the nRF5 SDK Makefile.common passes -std=c99
# first, so this later flag wins
CFLAGS += -std=gnu11
# nRF5 SDK 17.1 nrf_log_frontend.c trips -Warray-bounds on GCC >= 12 (SDK code, not ours)
CFLAGS += -Wno-array-bounds
CFLAGS += -DAPP_FW_VERSION=\"$(APP_FW_VERSION)\"
CFLAGS += -DDIAG_REQUIRE_BOND=$(DIAG_REQUIRE_BOND)
CFLAGS += -DMEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS=$(HEARTBEAT_SECS)
# Buttonless DFU (examples/ble_peripheral/ble_app_buttonless_dfu)
CFLAGS += -DBL_SETTINGS_ACCESS_ONLY -DNRF_DFU_SVCI_ENABLED -DNRF_DFU_TRANSPORT_BLE=1

# Source files common to all targets
SRC_FILES += \
  $(SDK_ROOT)/modules/nrfx/mdk/gcc_startup_nrf52840.S \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_backend_rtt.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_backend_serial.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_default_backends.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_frontend.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_str_formatter.c \
  $(SDK_ROOT)/components/libraries/button/app_button.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/scheduler/app_scheduler.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/crc16/crc16.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/fds/fds.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/atomic_flags/nrf_atflags.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/balloc/nrf_balloc.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf_format.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage_sd.c \
  $(SDK_ROOT)/components/libraries/memobj/nrf_memobj.c \
  $(SDK_ROOT)/components/libraries/pwr_mgmt/nrf_pwr_mgmt.c \
  $(SDK_ROOT)/components/libraries/ringbuf/nrf_ringbuf.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52840.c \
  $(SDK_ROOT)/components/boards/boards.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_clock.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_clock.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_gpiote.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/prs/nrfx_prs.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT_Syscalls_GCC.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT_printf.c \
  $(SDK_ROOT)/components/ble/peer_manager/auth_status_tracker.c \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_params.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_state.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/peer_manager/gatt_cache_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/gatts_cache_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/id_manager.c \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt/nrf_ble_gatt.c \
  $(SDK_ROOT)/components/ble/nrf_ble_qwr/nrf_ble_qwr.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_data_storage.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_database.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_id.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_manager_handler.c \
  $(SDK_ROOT)/components/ble/peer_manager/pm_buffer.c \
  $(SDK_ROOT)/components/ble/peer_manager/security_dispatcher.c \
  $(SDK_ROOT)/components/ble/peer_manager/security_manager.c \
  $(SDK_ROOT)/external/utf_converter/utf.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \

SRC_FILES += \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_rtc.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_wdt.c \
  $(SDK_ROOT)/modules/nrfx/hal/nrf_nvmc.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_dis/ble_dis.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_dfu/ble_dfu.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_dfu/ble_dfu_unbonded.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_svci.c \
  $(PROJ_DIR)/dfu_service.c \
  $(PROJ_DIR)/main.c \
  $(PROJ_DIR)/diag_service.c \
  $(PROJ_DIR)/demo_faults.c \
  $(PROJ_DIR)/app_metrics.c \
  $(PROJ_DIR)/console.c \
  $(SDK_ROOT)/components/libraries/cli/nrf_cli.c \
  $(SDK_ROOT)/components/libraries/cli/uart/nrf_cli_uart.c \
  $(SDK_ROOT)/components/libraries/queue/nrf_queue.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_uart.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_uart.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_uarte.c \

# Include folders common to all targets
INC_FOLDERS += \
  $(SDK_ROOT)/components/nfc/ndef/generic/message \
  $(SDK_ROOT)/components/nfc/t2t_lib \
  $(SDK_ROOT)/components/nfc/t4t_parser/hl_detection_procedure \
  $(SDK_ROOT)/components/ble/ble_services/ble_ancs_c \
  $(SDK_ROOT)/components/ble/ble_services/ble_ias_c \
  $(SDK_ROOT)/components/libraries/pwm \
  $(SDK_ROOT)/components/libraries/usbd/class/cdc/acm \
  $(SDK_ROOT)/components/libraries/usbd/class/hid/generic \
  $(SDK_ROOT)/components/libraries/usbd/class/msc \
  $(SDK_ROOT)/components/libraries/usbd/class/hid \
  $(SDK_ROOT)/modules/nrfx/hal \
  $(SDK_ROOT)/components/nfc/ndef/conn_hand_parser/le_oob_rec_parser \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/ble/ble_services/ble_gls \
  $(SDK_ROOT)/components/libraries/fstorage \
  $(SDK_ROOT)/components/nfc/ndef/text \
  $(SDK_ROOT)/components/libraries/mutex \
  $(SDK_ROOT)/components/libraries/gpiote \
  $(SDK_ROOT)/components/libraries/bootloader/ble_dfu \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/common \
  $(SDK_ROOT)/components/boards \
  $(SDK_ROOT)/components/nfc/ndef/generic/record \
  $(SDK_ROOT)/components/nfc/t4t_parser/cc_file \
  $(SDK_ROOT)/components/ble/ble_advertising \
  $(SDK_ROOT)/external/utf_converter \
  $(SDK_ROOT)/components/ble/ble_services/ble_bas_c \
  $(SDK_ROOT)/modules/nrfx/drivers/include \
  $(SDK_ROOT)/components/libraries/experimental_task_manager \
  $(SDK_ROOT)/components/ble/ble_services/ble_hrs_c \
  $(SDK_ROOT)/components/softdevice/s140/headers/nrf52 \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/le_oob_rec \
  $(SDK_ROOT)/components/libraries/queue \
  $(SDK_ROOT)/components/libraries/pwr_mgmt \
  $(SDK_ROOT)/components/ble/ble_dtm \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/components/ble/ble_services/ble_rscs_c \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/ble/ble_services/ble_lls \
  $(SDK_ROOT)/components/nfc/platform \
  $(SDK_ROOT)/components/libraries/bsp \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/ac_rec \
  $(SDK_ROOT)/components/ble/ble_services/ble_bas \
  $(SDK_ROOT)/components/libraries/mpu \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/ble/ble_services/ble_ans_c \
  $(SDK_ROOT)/components/libraries/slip \
  $(SDK_ROOT)/components/libraries/delay \
  $(SDK_ROOT)/components/libraries/csense_drv \
  $(SDK_ROOT)/components/libraries/memobj \
  $(SDK_ROOT)/components/ble/ble_services/ble_nus_c \
  $(SDK_ROOT)/components/softdevice/common \
  $(SDK_ROOT)/components/ble/ble_services/ble_ias \
  $(SDK_ROOT)/components/libraries/usbd/class/hid/mouse \
  $(SDK_ROOT)/components/libraries/low_power_pwm \
  $(SDK_ROOT)/components/nfc/ndef/conn_hand_parser/ble_oob_advdata_parser \
  $(SDK_ROOT)/components/ble/ble_services/ble_dfu \
  $(SDK_ROOT)/external/fprintf \
  $(SDK_ROOT)/components/libraries/svc \
  $(SDK_ROOT)/components/libraries/atomic \
  $(SDK_ROOT)/components \
  $(SDK_ROOT)/components/libraries/scheduler \
  $(SDK_ROOT)/components/libraries/cli \
  $(SDK_ROOT)/components/ble/ble_services/ble_lbs \
  $(SDK_ROOT)/components/ble/ble_services/ble_hts \
  $(SDK_ROOT)/components/libraries/crc16 \
  $(SDK_ROOT)/components/nfc/t4t_parser/apdu \
  $(SDK_ROOT)/components/libraries/util \
  config \
  $(SDK_ROOT)/components/libraries/usbd/class/cdc \
  $(SDK_ROOT)/components/libraries/csense \
  $(SDK_ROOT)/components/libraries/balloc \
  $(SDK_ROOT)/components/libraries/ecc \
  $(SDK_ROOT)/components/libraries/hardfault \
  $(SDK_ROOT)/components/ble/ble_services/ble_cscs \
  $(SDK_ROOT)/components/libraries/hci \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/softdevice/s140/headers \
  $(SDK_ROOT)/integration/nrfx \
  $(SDK_ROOT)/components/nfc/t4t_parser/tlv \
  $(SDK_ROOT)/components/libraries/sortlist \
  $(SDK_ROOT)/components/libraries/spi_mngr \
  $(SDK_ROOT)/components/libraries/led_softblink \
  $(SDK_ROOT)/components/nfc/ndef/conn_hand_parser \
  $(SDK_ROOT)/components/libraries/sdcard \
  $(SDK_ROOT)/components/nfc/ndef/parser/record \
  $(SDK_ROOT)/modules/nrfx/mdk \
  $(SDK_ROOT)/components/ble/ble_services/ble_cts_c \
  $(SDK_ROOT)/components/ble/ble_services/ble_nus \
  $(SDK_ROOT)/components/libraries/twi_mngr \
  $(SDK_ROOT)/components/ble/ble_services/ble_hids \
  $(SDK_ROOT)/components/libraries/strerror \
  $(SDK_ROOT)/components/libraries/crc32 \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/ble_oob_advdata \
  $(SDK_ROOT)/components/nfc/t2t_parser \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/ble_pair_msg \
  $(SDK_ROOT)/components/libraries/usbd/class/audio \
  $(SDK_ROOT)/components/libraries/sensorsim \
  $(SDK_ROOT)/components/nfc/t4t_lib \
  $(SDK_ROOT)/components/ble/peer_manager \
  $(SDK_ROOT)/components/libraries/mem_manager \
  $(SDK_ROOT)/components/libraries/ringbuf \
  $(SDK_ROOT)/components/ble/ble_services/ble_tps \
  $(SDK_ROOT)/components/nfc/ndef/parser/message \
  $(SDK_ROOT)/components/ble/ble_services/ble_dis \
  $(SDK_ROOT)/components/nfc/ndef/uri \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt \
  $(SDK_ROOT)/components/ble/nrf_ble_qwr \
  $(SDK_ROOT)/components/libraries/gfx \
  $(SDK_ROOT)/components/libraries/button \
  $(SDK_ROOT)/modules/nrfx \
  $(SDK_ROOT)/components/libraries/twi_sensor \
  $(SDK_ROOT)/integration/nrfx/legacy \
  $(SDK_ROOT)/components/libraries/usbd/class/hid/kbd \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/ep_oob_rec \
  $(SDK_ROOT)/external/segger_rtt \
  $(SDK_ROOT)/components/libraries/atomic_fifo \
  $(SDK_ROOT)/components/ble/ble_services/ble_lbs_c \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/ble_pair_lib \
  $(SDK_ROOT)/components/libraries/crypto \
  $(SDK_ROOT)/components/ble/ble_racp \
  $(SDK_ROOT)/components/libraries/fds \
  $(SDK_ROOT)/components/nfc/ndef/launchapp \
  $(SDK_ROOT)/components/libraries/atomic_flags \
  $(SDK_ROOT)/components/ble/ble_services/ble_hrs \
  $(SDK_ROOT)/components/ble/ble_services/ble_rscs \
  $(SDK_ROOT)/components/nfc/ndef/connection_handover/hs_rec \
  $(SDK_ROOT)/components/libraries/usbd \
  $(SDK_ROOT)/components/nfc/ndef/conn_hand_parser/ac_rec_parser \
  $(SDK_ROOT)/components/libraries/stack_guard \
  $(SDK_ROOT)/components/libraries/log/src \

# Libraries common to all targets
LIB_FILES += \

# Optimization flags
OPT = -Os -g3
# Uncomment the line below to enable link time optimization
#OPT += -flto

# C flags common to all targets
CFLAGS += $(OPT)
CFLAGS += -DAPP_TIMER_V2
CFLAGS += -DAPP_TIMER_V2_RTC1_ENABLED
CFLAGS += -DBOARD_PCA10056
CFLAGS += -DCONFIG_GPIO_AS_PINRESET
CFLAGS += -DFLOAT_ABI_HARD
CFLAGS += -DNRF52840_XXAA
CFLAGS += -DNRF_SD_BLE_API_VERSION=7
CFLAGS += -DS140
CFLAGS += -DSOFTDEVICE_PRESENT
CFLAGS += -mcpu=cortex-m4
CFLAGS += -mthumb -mabi=aapcs
CFLAGS += -Wall -Werror
CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
# keep every function in a separate section, this allows linker to discard unused ones
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin -fshort-enums

# C++ flags common to all targets
CXXFLAGS += $(OPT)
# Assembler flags common to all targets
ASMFLAGS += -g3
ASMFLAGS += -mcpu=cortex-m4
ASMFLAGS += -mthumb -mabi=aapcs
ASMFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
ASMFLAGS += -DAPP_TIMER_V2
ASMFLAGS += -DAPP_TIMER_V2_RTC1_ENABLED
ASMFLAGS += -DBOARD_PCA10056
ASMFLAGS += -DCONFIG_GPIO_AS_PINRESET
ASMFLAGS += -DFLOAT_ABI_HARD
ASMFLAGS += -DNRF52840_XXAA
ASMFLAGS += -DNRF_SD_BLE_API_VERSION=7
ASMFLAGS += -DS140
ASMFLAGS += -DSOFTDEVICE_PRESENT

# Linker flags
LDFLAGS += $(OPT)
LDFLAGS += -mthumb -mabi=aapcs -L$(SDK_ROOT)/modules/nrfx/mdk -T$(LINKER_SCRIPT)
LDFLAGS += -mcpu=cortex-m4
LDFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
# let linker dump unused sections
LDFLAGS += -Wl,--gc-sections
# use newlib in nano version
LDFLAGS += --specs=nano.specs

$(APP_TARGET): CFLAGS += -D__HEAP_SIZE=8192
$(APP_TARGET): CFLAGS += -D__STACK_SIZE=8192
$(APP_TARGET): ASMFLAGS += -D__HEAP_SIZE=8192
$(APP_TARGET): ASMFLAGS += -D__STACK_SIZE=8192

# Add standard libraries at the very end of the linker input, after all objects
# that may need symbols provided by these libraries.
LIB_FILES += -lc -lnosys -lm


.PHONY: default help

# Default target - first one defined
default: $(APP_TARGET)

# Print all targets that can be built
help:
	@echo "targets: default (build), flash, flash_all, flash_nobl, flash_softdevice, erase,"
	@echo "         console, upload_symbols, dfu_pkg, memfault_release, bootloader, sdk_config"

TEMPLATE_PATH := $(SDK_ROOT)/components/toolchain/gcc


include $(TEMPLATE_PATH)/Makefile.common

$(foreach target, $(TARGETS), $(call define_target, $(target)))

.PHONY: flash flash_softdevice erase

# Programming uses `nrfutil device` (nrfjprog is deprecated and doesn't work with newer J-Link
# installs). Set NRF_SN to the DK's J-Link serial (`nrfutil device list`) when more than one
# device is attached.
NRF_SN ?=
NRFUTIL_DEV = $(if $(NRF_SN),--serial-number $(NRF_SN))

# Flash the program (app + regenerated bootloader settings; keeps SoftDevice, bootloader,
# coredump and bonds). The settings page has to be rewritten with the app: the bootloader
# checks the app's CRC against it at boot and otherwise stays in DFU mode ("DfuTarg").
flash: default
	$(gen_settings)
	mergehex -m $(OUTPUT_DIRECTORY)/$(APP_TARGET).hex $(SETTINGS_HEX) -o $(OUTPUT_DIRECTORY)/app_settings.hex
	nrfutil device program $(NRFUTIL_DEV) --firmware $(OUTPUT_DIRECTORY)/app_settings.hex \
	  --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,verify=VERIFY_READ
	nrfutil device reset $(NRFUTIL_DEV)

# Flash softdevice
flash_softdevice:
	nrfutil device program $(NRFUTIL_DEV) --firmware $(SD_HEX) \
	  --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,verify=VERIFY_READ
	nrfutil device reset $(NRFUTIL_DEV)

erase:
	nrfutil device erase --all $(NRFUTIL_DEV)

#
# Bootloader + DFU (Nordic secure BLE bootloader at 0xF8000)
#
BL_DIR      := ../bootloader
BL_HEX      := $(BL_DIR)/_build/nrf52840_xxaa_s140.hex
DFU_KEY     ?= ../keys/dfu_private_key.pem
APP_HEX     := $(OUTPUT_DIRECTORY)/$(APP_TARGET).hex
SETTINGS_HEX := $(OUTPUT_DIRECTORY)/bl_settings.hex
# "3.1.1" -> 30101: the integer the bootloader uses for downgrade prevention
APP_VERSION_INT = $(shell echo $(APP_FW_VERSION) | awk -F. '{print $$1*10000 + $$2*100 + $$3}')
DFU_PKG     := $(OUTPUT_DIRECTORY)/dfu/nrf5_memfault_demo_$(APP_FW_VERSION).zip
# S140 7.3.0 firmware ID (from the SoftDevice info struct at 0x300C)
SD_FWID     := 0x0123

bootloader:
	$(MAKE) -C $(BL_DIR)

# Settings page so the bootloader accepts the app flashed over SWD (CRC + version).
# Regenerated on every flash (cheap) rather than tracked as a dependency: the app .hex is produced
# by the SDK's Makefile.common, so make's view of its timestamp can be stale.
define gen_settings
	nrfutil nrf5sdk-tools settings generate --family NRF52840 --application $(APP_HEX) \
	  --application-version $(APP_VERSION_INT) --bootloader-version 1 --bl-settings-version 2 \
	  --app-boot-validation VALIDATE_GENERATED_CRC $(SETTINGS_HEX) >/dev/null
endef

# Full erase + SoftDevice + bootloader + app + settings (first time on a DK).
# nrfutil erases the chip on each program by default, so merge everything into one image.
FULL_HEX := $(OUTPUT_DIRECTORY)/full_image.hex
$(FULL_HEX): default bootloader
	$(gen_settings)
	mergehex -m $(SD_HEX) $(BL_HEX) -o $(OUTPUT_DIRECTORY)/sd_bl.hex
	mergehex -m $(OUTPUT_DIRECTORY)/sd_bl.hex $(APP_HEX) $(SETTINGS_HEX) -o $@

flash_all: $(FULL_HEX)
	nrfutil device program $(NRFUTIL_DEV) --firmware $(FULL_HEX) \
	  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ
	nrfutil device reset $(NRFUTIL_DEV)

# No bootloader: SoftDevice + app only (quick iteration, no DFU)
flash_nobl: default
	mergehex -m $(SD_HEX) $(APP_HEX) -o $(OUTPUT_DIRECTORY)/sd_app.hex
	nrfutil device program $(NRFUTIL_DEV) --firmware $(OUTPUT_DIRECTORY)/sd_app.hex \
	  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ
	nrfutil device reset $(NRFUTIL_DEV)

# Signed DFU package, as a phone app would deliver it over BLE via Nordic DFU.
# Bump APP_FW_VERSION: the bootloader rejects downgrades and same-version packages.
dfu_pkg: default
	@mkdir -p $(dir $(DFU_PKG))
	nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req $(SD_FWID) \
	  --application $(APP_HEX) --application-version $(APP_VERSION_INT) \
	  --key-file $(DFU_KEY) $(DFU_PKG)
	@echo "DFU package: $(DFU_PKG)  (apply with nRF Connect / nRF Device Manager mobile app)"

# Memfault release for this version: upload the DFU package as an OTA payload and deploy it to a
# cohort. A phone app can then ask Memfault which release a device should run and install it
# with its existing Nordic DFU code. Needs MEMFAULT_* env vars (and a Manager role to deploy).
MEMFAULT_COHORT ?= default
ROLLOUT_PERCENT ?= 100
memfault_release: dfu_pkg upload_symbols
	memfault --org-token $(MEMFAULT_ORG_TOKEN) --org $(MEMFAULT_ORG) --project $(MEMFAULT_PROJECT) \
	  upload-ota-payload --hardware-version nrf52840dk --software-type nrf5-memfault-demo \
	  --software-version $(APP_FW_VERSION) $(DFU_PKG)
	memfault --org-token $(MEMFAULT_ORG_TOKEN) --org $(MEMFAULT_ORG) --project $(MEMFAULT_PROJECT) \
	  deploy-release --release-version $(APP_FW_VERSION) --cohort $(MEMFAULT_COHORT) \
	  --rollout-percent $(ROLLOUT_PERCENT)

.PHONY: bootloader flash_nobl dfu_pkg memfault_release

# APP_FW_VERSION is only a -D flag, which make doesn't track. A per-version stamp file
# forces the objects that use it to rebuild (and the app to relink) when the version changes,
# so `make APP_FW_VERSION=x.y.z dfu_pkg` never packages a stale binary.
VERSION_STAMP := $(OUTPUT_DIRECTORY)/.fw_version_$(APP_FW_VERSION)_bond$(DIAG_REQUIRE_BOND)_hb$(HEARTBEAT_SECS)
$(VERSION_STAMP):
	@mkdir -p $(OUTPUT_DIRECTORY)
	@rm -f $(OUTPUT_DIRECTORY)/.fw_version_*
	@touch $@
$(addprefix $(OUTPUT_DIRECTORY)/$(APP_TARGET)/,main.c.o memfault_platform_port.c.o diag_service.c.o memfault_metrics.c.o): $(VERSION_STAMP)

# Console: logs + demo commands on the DK's USB serial port (J-Link VCOM0). Tab lists commands.
# Exit screen with Ctrl-A then K.
# macOS names J-Link VCOM0 /dev/tty.usbmodem00<serial>1; on Linux it is usually /dev/ttyACM0.
CONSOLE_PORT ?= /dev/tty.usbmodem00$(NRF_SN)1
console:
	screen $(CONSOLE_PORT) 115200

# Upload the .elf so Memfault can symbolicate coredumps and decode compact logs.
# Needs MEMFAULT_ORG_TOKEN, MEMFAULT_ORG, MEMFAULT_PROJECT in the environment.
upload_symbols: default
	memfault --org-token $(MEMFAULT_ORG_TOKEN) --org $(MEMFAULT_ORG) --project $(MEMFAULT_PROJECT) \
	  upload-mcu-symbols $(OUTPUT_DIRECTORY)/$(APP_TARGET).out

.PHONY: flash_all console upload_symbols $(FULL_HEX)

SDK_CONFIG_FILE := config/sdk_config.h
CMSIS_CONFIG_TOOL := $(SDK_ROOT)/external_tools/cmsisconfig/CMSIS_Configuration_Wizard.jar
sdk_config:
	java -jar $(CMSIS_CONFIG_TOOL) $(SDK_CONFIG_FILE)
