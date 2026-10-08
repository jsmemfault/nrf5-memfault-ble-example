/* Linker script to configure memory regions. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

/*
 * Flash layout: nRF52840, S140 7.3.0, Nordic secure BLE bootloader at 0xF8000 (the SDK's
 * pca10056_s140_ble placement).
 *
 *   0x00000000  MBR + SoftDevice S140 7.3.0
 *   0x00027000  Application (bank 0) + free space DFU uses as bank 1
 *   ----------  preserved across DFU: NRF_DFU_APP_DATA_AREA_SIZE = 32 KB ----------
 *   0x000F0000  SCRATCH (4 KB)          demo-only page for the flash double-write scenario
 *   0x000F1000  MEMFAULT_CORES (16 KB)  coredump storage, internal flash
 *   0x000F5000  FDS (3 pages, 12 KB)    Peer Manager bonds. FDS places itself just below
 *                                       the bootloader
 *   ----------
 *   0x000F8000  Secure bootloader (24 KB)
 *   0x000FE000  MBR params page
 *   0x000FF000  Bootloader settings page
 *
 * Bootloader preservation rules (nRF5 SDK 17.1 nrf_dfu_utils.c): dual-bank DFU writes the new
 * image starting on the page after the current one, so a region near the top of flash is only
 * erased if old + new images exceed the space below it (~800 KiB here). The SDK's default
 * NRF_DFU_APP_DATA_AREA_SIZE (12 KB) preserves only FDS; this example's bootloader preserves
 * 32 KB, which guarantees the coredump region survives every DFU.
 */
MEMORY
{
  FLASH (rx)          : ORIGIN = 0x27000, LENGTH = 0xC9000
  SCRATCH (rw)        : ORIGIN = 0xF0000, LENGTH = 0x1000
  MEMFAULT_CORES (rw) : ORIGIN = 0xF1000, LENGTH = 0x4000
  /* RAM: 0x20004000 is the app RAM start given to the SoftDevice (generous; the SoftDevice
     logs its exact need at boot). The top 16 KB (0x2003C000-0x20040000) is left to the
     bootloader's stack, so nothing the app keeps across a reset (Memfault reboot tracking at
     the bottom of the app stack, NOINIT below) is overwritten when the bootloader runs. */
  RAM (rwx)           : ORIGIN = 0x20004000, LENGTH = 0x37F00
  /* 256 B that neither the C startup nor the bootloader initializes. Holds the last VDD
     reading for brownout classification. */
  NOINIT (rwx)        : ORIGIN = 0x2003BF00, LENGTH = 0x100
}

__MemfaultCoreStorageStart = ORIGIN(MEMFAULT_CORES);
__MemfaultCoreStorageEnd = ORIGIN(MEMFAULT_CORES) + LENGTH(MEMFAULT_CORES);
__AppScratchFlashStart = ORIGIN(SCRATCH);
__AppScratchFlashEnd = ORIGIN(SCRATCH) + LENGTH(SCRATCH);

SECTIONS
{
  .app_noinit (NOLOAD) :
  {
    KEEP(*(.app_noinit))
  } > NOINIT
}

/* Memfault compact log format strings: kept in the .elf only, never loaded on the device */
SECTIONS
{
  log_fmt 0xF0000000 (INFO):
  {
    KEEP(*(*.log_fmt_hdr))
    KEEP(*(log_fmt))
  }
}

SECTIONS
{
}

SECTIONS
{
  . = ALIGN(4);
  .mem_section_dummy_ram :
  {
  }
  .cli_sorted_cmd_ptrs :
  {
    PROVIDE(__start_cli_sorted_cmd_ptrs = .);
    KEEP(*(.cli_sorted_cmd_ptrs))
    PROVIDE(__stop_cli_sorted_cmd_ptrs = .);
  } > RAM
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
  .log_dynamic_data :
  {
    PROVIDE(__start_log_dynamic_data = .);
    KEEP(*(SORT(.log_dynamic_data*)))
    PROVIDE(__stop_log_dynamic_data = .);
  } > RAM
  .log_filter_data :
  {
    PROVIDE(__start_log_filter_data = .);
    KEEP(*(SORT(.log_filter_data*)))
    PROVIDE(__stop_log_filter_data = .);
  } > RAM

} INSERT AFTER .data;

SECTIONS
{
  .mem_section_dummy_rom :
  {
  }
  .sdh_soc_observers :
  {
    PROVIDE(__start_sdh_soc_observers = .);
    KEEP(*(SORT(.sdh_soc_observers*)))
    PROVIDE(__stop_sdh_soc_observers = .);
  } > FLASH
  .pwr_mgmt_data :
  {
    PROVIDE(__start_pwr_mgmt_data = .);
    KEEP(*(SORT(.pwr_mgmt_data*)))
    PROVIDE(__stop_pwr_mgmt_data = .);
  } > FLASH
  .sdh_ble_observers :
  {
    PROVIDE(__start_sdh_ble_observers = .);
    KEEP(*(SORT(.sdh_ble_observers*)))
    PROVIDE(__stop_sdh_ble_observers = .);
  } > FLASH
  .sdh_req_observers :
  {
    PROVIDE(__start_sdh_req_observers = .);
    KEEP(*(SORT(.sdh_req_observers*)))
    PROVIDE(__stop_sdh_req_observers = .);
  } > FLASH
  .sdh_state_observers :
  {
    PROVIDE(__start_sdh_state_observers = .);
    KEEP(*(SORT(.sdh_state_observers*)))
    PROVIDE(__stop_sdh_state_observers = .);
  } > FLASH
  .sdh_stack_observers :
  {
    PROVIDE(__start_sdh_stack_observers = .);
    KEEP(*(SORT(.sdh_stack_observers*)))
    PROVIDE(__stop_sdh_stack_observers = .);
  } > FLASH
    .nrf_queue :
  {
    PROVIDE(__start_nrf_queue = .);
    KEEP(*(.nrf_queue))
    PROVIDE(__stop_nrf_queue = .);
  } > FLASH
    .nrf_balloc :
  {
    PROVIDE(__start_nrf_balloc = .);
    KEEP(*(.nrf_balloc))
    PROVIDE(__stop_nrf_balloc = .);
  } > FLASH
    .cli_command :
  {
    PROVIDE(__start_cli_command = .);
    KEEP(*(.cli_command))
    PROVIDE(__stop_cli_command = .);
  } > FLASH
  .crypto_data :
  {
    PROVIDE(__start_crypto_data = .);
    KEEP(*(SORT(.crypto_data*)))
    PROVIDE(__stop_crypto_data = .);
  } > FLASH
  .log_const_data :
  {
    PROVIDE(__start_log_const_data = .);
    KEEP(*(SORT(.log_const_data*)))
    PROVIDE(__stop_log_const_data = .);
  } > FLASH
  .log_backends :
  {
    PROVIDE(__start_log_backends = .);
    KEEP(*(SORT(.log_backends*)))
    PROVIDE(__stop_log_backends = .);
  } > FLASH

} INSERT AFTER .text

SECTIONS
{
  /* GNU build ID, read by Memfault (memfault/core/build_info.h) */
  .note.gnu.build-id :
  {
     __start_gnu_build_id_start = .;
     KEEP(*(.note.gnu.build-id))
  } > FLASH

} INSERT AFTER .text

INCLUDE "nrf_common.ld"
