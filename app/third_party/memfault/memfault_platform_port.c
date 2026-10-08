//! @file
//!
//! Memfault platform port for this example (nRF52840, nRF5 SDK 17.1.0, S140 7.3.0).
//!
//! The generic nRF5 pieces come from memfault-firmware-sdk/ports/nrf5_sdk:
//!   - nrf5_coredump_storage.c      coredumps in internal flash (MEMFAULT_CORES in the .ld)
//!   - resetreas_reboot_tracking.c  RESETREAS -> Memfault reboot reasons
//!   - nrf5_coredump_regions.c      RAM address sanitization
//! plus, in this directory: nrf5_software_watchdog_sd.c (SoftDevice-safe IRQ priority),
//! app_memfault_timer.c (heartbeat on an existing app wakeup, replacing the stock
//! memfault_platform_metrics.c) and nrf_log_backend_memfault.c (NRF_LOG capture).
//!
//! This file adds what is product specific: device info, which RAM goes into a coredump,
//! SoftDevice fault routing, and the battery state of charge.

#include <stdio.h>
#include <string.h>

#include "app_error.h"
#include "app_util.h"
#include "app_util_platform.h"
#include "app.h"
#include "memfault/components.h"
#include "memfault/metrics/platform/battery.h"
#include "memfault/ports/reboot_reason.h"
#include "nrf.h"
#include "nrf_log_ctrl.h"
#include "nrf_sdm.h"

// Event storage holds heartbeats, trace events and reboot events in RAM until the next sync.
// Size it for the worst-case time between syncs, or enable NV event storage: events still in
// RAM are lost on a reset.
#ifndef APP_MEMFAULT_EVENT_STORAGE_SIZE
  #define APP_MEMFAULT_EVENT_STORAGE_SIZE 2048
#endif

// Log buffer: captured in coredumps and uploaded on memfault_log_trigger_collection()
#ifndef APP_MEMFAULT_LOG_BUF_SIZE
  #define APP_MEMFAULT_LOG_BUF_SIZE 2048
#endif

// Cap on how much of the active stack goes into a coredump
#ifndef APP_COREDUMP_STACK_CAPTURE_SIZE
  #define APP_COREDUMP_STACK_CAPTURE_SIZE 4096
#endif

static void prv_get_device_serial(char *buf, size_t buf_len) {
  // 64-bit FICR DEVICEID. A product would likely use its own serial number here so that
  // Memfault device pages line up with support/RMA records.
  snprintf(buf, buf_len, "%08lX%08lX", (unsigned long)NRF_FICR->DEVICEID[1],
           (unsigned long)NRF_FICR->DEVICEID[0]);
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info) {
  static char s_device_serial[17];
  if (s_device_serial[0] == '\0') {
    prv_get_device_serial(s_device_serial, sizeof(s_device_serial));
  }

  *info = (sMemfaultDeviceInfo){
    .device_serial = s_device_serial,
    .software_type = "nrf5-memfault-demo",
    .software_version = APP_FW_VERSION,
    .hardware_version = "nrf52840dk",
  };
}

void memfault_platform_reboot(void) {
  NVIC_SystemReset();
  MEMFAULT_UNREACHABLE;
}

bool memfault_platform_time_get_current(sMemfaultCurrentTime *time) {
  // With an RTC (e.g. set by the phone app on sync), returning it here would timestamp
  // events on the device. The DK has no RTC, so Memfault timestamps on arrival.
  return false;
}

//
// Locking
//
// No RTOS, but Memfault is called from several contexts: main loop, app_timer (RTC1 IRQ),
// SoftDevice event dispatch (SWI2 IRQ). The SDK's weak memfault_lock() is a no-op, so
// protect it with the SoftDevice-aware critical region (masks application IRQs only, never
// the SoftDevice's own).
//

static uint8_t s_lock_nested;
static uint32_t s_lock_depth;

void memfault_lock(void) {
  uint8_t nested;
  app_util_critical_region_enter(&nested);
  if (s_lock_depth++ == 0) {
    s_lock_nested = nested;
  }
}

void memfault_unlock(void) {
  if (--s_lock_depth == 0) {
    app_util_critical_region_exit(s_lock_nested);
  }
}

//
// Coredump contents
//

const sMfltCoredumpRegion *memfault_platform_coredump_get_regions(
  const sCoredumpCrashInfo *crash_info, size_t *num_regions) {
  static sMfltCoredumpRegion s_coredump_regions[2];
  size_t idx = 0;

  // 1. The active stack, capped. In a bare-metal event-driven app that's the main stack,
  //    which also holds the interrupted context when the fault happened in an ISR.
  const size_t stack_size = memfault_platform_sanitize_address_range(
    crash_info->stack_address, APP_COREDUMP_STACK_CAPTURE_SIZE);
  s_coredump_regions[idx++] =
    MEMFAULT_COREDUMP_MEMORY_REGION_INIT(crash_info->stack_address, stack_size);

  // 2. Application state worth seeing in every crash (BLE/sync state, sensor state)
  s_coredump_regions[idx++] =
    MEMFAULT_COREDUMP_MEMORY_REGION_INIT(&g_app_state, sizeof(g_app_state));

  // The Memfault log buffer is added automatically (MEMFAULT_COREDUMP_COLLECT_LOG_REGIONS)

  *num_regions = idx;
  return s_coredump_regions;
}

//! Runs first in Memfault's fault handler, before the coredump is written. NRF_LOG is deferred,
//! so the last lines before a crash are usually still queued in nrf_log's ring buffer.
//! Flush them now (panic mode: synchronous) so they land in the Memfault log buffer,
//! and so in the coredump.
void memfault_platform_fault_handler(const sMfltRegState *regs, eMemfaultRebootReason reason) {
  NRF_LOG_FINAL_FLUSH();
}

//
// SoftDevice and SDK fault routing
//
// The SoftDevice reports its own asserts and memory access violations through the fault handler
// passed to sd_softdevice_enable(). In the nRF5 SDK that's app_error_fault_handler(), which is
// __WEAK in app_error_weak.c and only logs and resets. Overriding it gives a coredump and a
// distinct reboot reason for SoftDevice faults.
//

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  switch (id) {
    case NRF_FAULT_ID_SD_ASSERT:
      // pc = address inside the SoftDevice that asserted (see S140 SDS, "Fault handler")
      MEMFAULT_ASSERT_EXTRA_AND_REASON(pc, MEMFAULT_REBOOT_REASON_KEY(SoftDeviceAssert));
      break;
    case NRF_FAULT_ID_APP_MEMACC:
      // info = address of the illegal access into SoftDevice-protected memory/peripherals
      MEMFAULT_ASSERT_EXTRA_AND_REASON(info, MEMFAULT_REBOOT_REASON_KEY(SoftDeviceMemAccess));
      break;
    case NRF_FAULT_ID_SDK_ASSERT: {
      // nrf_assert.h ASSERT() (enabled with DEBUG_NRF)
      const assert_info_t *assert_info = (const assert_info_t *)info;
      MEMFAULT_ASSERT_RECORD(assert_info ? assert_info->line_num : 0);
      break;
    }
    case NRF_FAULT_ID_SDK_ERROR: {
      // app_error_handler() callers that bypass the APP_ERROR_CHECK override
      const error_info_t *error_info = (const error_info_t *)info;
      MEMFAULT_ASSERT_RECORD(error_info ? error_info->err_code : 0);
      break;
    }
    default:
      MEMFAULT_ASSERT_RECORD(id);
      break;
  }
  MEMFAULT_UNREACHABLE;
}

//
// Battery
//

int memfault_platform_get_stateofcharge(sMfltPlatformBatterySoc *soc) {
  *soc = (sMfltPlatformBatterySoc){
    .soc = app_metrics_battery_soc_pct(),
    .discharging = !app_metrics_battery_is_charging(),
  };
  return 0;
}

//
// Boot
//

int memfault_platform_boot(void) {
  static uint8_t s_log_buf_storage[APP_MEMFAULT_LOG_BUF_SIZE];
  memfault_log_boot(s_log_buf_storage, sizeof(s_log_buf_storage));

  memfault_build_info_dump();
  memfault_device_info_dump();
  memfault_platform_reboot_tracking_boot();

  static uint8_t s_event_storage[APP_MEMFAULT_EVENT_STORAGE_SIZE];
  const sMemfaultEventStorageImpl *evt_storage =
    memfault_events_storage_boot(s_event_storage, sizeof(s_event_storage));
  memfault_trace_event_boot(evt_storage);

  memfault_reboot_tracking_collect_reset_info(evt_storage);

  sMemfaultMetricBootInfo boot_info = {
    .unexpected_reboot_count = memfault_reboot_tracking_get_crash_count(),
  };
  memfault_metrics_boot(evt_storage, &boot_info);

  MEMFAULT_LOG_INFO("Memfault initialized, fw %s", APP_FW_VERSION);
  return 0;
}
