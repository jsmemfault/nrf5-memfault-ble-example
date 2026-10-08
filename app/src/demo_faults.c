//! @file
//!
//! Failure scenarios for the demo, each a common class of field issue.
//!
//!   Button 1  flash double write         a flash region written twice without an erase
//!   Button 2  HardFault                  bad function pointer in a driver callback table
//!   Button 3  hang (in the button callback, an app_timer interrupt; the console `hang`
//!             command hangs the main loop): software watchdog fires before the HW watchdog
//!   Button 4  forget bonds on the device next reconnect from the old bond fails security
//!                                        (a bond mismatch between phone and device)

#include <string.h>

#include "app_error.h"
#include "app.h"
#include "memfault/components.h"
#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#include "nrf_log.h"

extern uint32_t __AppScratchFlashStart[];
extern uint32_t __AppScratchFlashEnd[];

#define FLASH_ERASED_WORD 0xFFFFFFFFUL

//! The recommended pattern: every flash write goes through one guard that checks the
//! target is erased. A non-erased target is the double-write bug. Record it with the address
//! and the caller (trace event + logs), or make it fatal to get a full coredump at the call
//! site.
typedef enum {
  kFlashGuard_RecordOnly,
  kFlashGuard_Fatal,
} eFlashGuardMode;

static void prv_fstorage_evt_handler(nrf_fstorage_evt_t *p_evt);

NRF_FSTORAGE_DEF(nrf_fstorage_t s_scratch_fs) = {
  .evt_handler = prv_fstorage_evt_handler,
};

// fstorage writes are asynchronous: source data must stay valid until the event fires
static uint32_t s_record[4];
static bool s_write_in_flight;

static void prv_fstorage_evt_handler(nrf_fstorage_evt_t *p_evt) {
  if (p_evt->id == NRF_FSTORAGE_EVT_WRITE_RESULT) {
    s_write_in_flight = false;
    if (p_evt->result != NRF_SUCCESS) {
      MEMFAULT_TRACE_EVENT_WITH_STATUS(sd_api_error, (int32_t)p_evt->result);
    }
  }
}

static bool prv_flash_region_is_erased(uint32_t addr, size_t len) {
  const uint32_t *p = (const uint32_t *)addr;
  for (size_t i = 0; i < len / sizeof(uint32_t); i++) {
    if (p[i] != FLASH_ERASED_WORD) {
      return false;
    }
  }
  return true;
}

static bool prv_flash_write_guarded(uint32_t addr, const void *data, size_t len,
                                    eFlashGuardMode mode) {
  if (!prv_flash_region_is_erased(addr, len)) {
    const uint32_t old_word = *(const uint32_t *)addr;
    MEMFAULT_METRIC_ADD(flash_double_write_count, 1);

    if (mode == kFlashGuard_Fatal) {
      MEMFAULT_LOG_ERROR("Flash double write at 0x%08lx (old=0x%08lx)", (unsigned long)addr,
                         (unsigned long)old_word);
      // Coredump: backtrace shows exactly which code path wrote twice
      MEMFAULT_ASSERT_RECORD(addr);
    }

    // Non-fatal: trace event (deduplicated by call site in Memfault) + upload of recent logs
    MEMFAULT_TRACE_EVENT_WITH_LOG(flash_double_write, "addr=0x%08lx old=0x%08lx",
                                  (unsigned long)addr, (unsigned long)old_word);
    memfault_log_trigger_collection();
    NRF_LOG_ERROR("Flash double write blocked at 0x%08x", addr);
    return false;
  }

  s_write_in_flight = true;
  const ret_code_t rv = nrf_fstorage_write(&s_scratch_fs, addr, data, len, NULL);
  if (rv != NRF_SUCCESS) {
    s_write_in_flight = false;
    MEMFAULT_TRACE_EVENT_WITH_STATUS(sd_api_error, (int32_t)rv);
    return false;
  }
  return true;
}

static void prv_flash_record_write(eFlashGuardMode mode) {
  if (s_write_in_flight) {
    return;
  }

  // The bug being simulated: a record header is rewritten at the same offset
  // without an erase in between (offset never advances). First write is fine, the second hits
  // the guard.
  const uint32_t addr = (uint32_t)__AppScratchFlashStart + g_app_state.scratch_write_offset;
  s_record[0] = 0x4D464C54;  // "MFLT"
  s_record[1] = g_app_state.sensor_acq_attempts;
  s_record[2] = (uint32_t)memfault_platform_get_time_since_boot_ms();
  s_record[3] = 0;

  if (prv_flash_write_guarded(addr, s_record, sizeof(s_record), mode)) {
    NRF_LOG_INFO("Record header written at 0x%08x", addr);
  }
}

void demo_faults_flash_record_write(void) {
  prv_flash_record_write(kFlashGuard_RecordOnly);
}

void demo_faults_flash_record_write_fatal(void) {
  prv_flash_record_write(kFlashGuard_Fatal);
}

//! Driver-style callback table. A stray write corrupts an entry and the next call
//! jumps into nowhere.
typedef struct {
  void (*on_data_ready)(void);
  void (*on_error)(void);
} sDriverCallbacks;

static void prv_driver_noop(void) { }
static sDriverCallbacks s_driver_callbacks = { prv_driver_noop, prv_driver_noop };

void demo_faults_hardfault(void) {
  NRF_LOG_WARNING("Corrupting a driver callback table, then calling it");
  volatile uintptr_t *slot = (volatile uintptr_t *)&s_driver_callbacks.on_data_ready;
  *slot = 0x0BADCAFE;
  s_driver_callbacks.on_data_ready();
}

//! Stuck waiting on a data-ready flag that never gets set. The RTC2 software watchdog
//! fires at 7 s and captures a coredump of this loop, before the 8 s HW watchdog resets
//! the chip with no information.
static volatile bool s_data_ready;

void demo_faults_hang(void) {
  NRF_LOG_WARNING("Waiting on a data-ready flag with no timeout...");
  while (!s_data_ready) {
  }
}

void demo_faults_forget_bonds(void) {
  NRF_LOG_WARNING("Deleting all bonds on the device. The phone still has its keys.");
  app_delete_bonds();
}

void demo_faults_init(void) {
  s_scratch_fs.start_addr = (uint32_t)__AppScratchFlashStart;
  s_scratch_fs.end_addr = (uint32_t)__AppScratchFlashEnd;
  ret_code_t rv = nrf_fstorage_init(&s_scratch_fs, &nrf_fstorage_sd, NULL);
  APP_ERROR_CHECK(rv);

  // Start each boot with a clean scratch page so the scenario is repeatable
  if (!prv_flash_region_is_erased(s_scratch_fs.start_addr, 4096)) {
    rv = nrf_fstorage_erase(&s_scratch_fs, s_scratch_fs.start_addr, 1, NULL);
    APP_ERROR_CHECK(rv);
  }
}
