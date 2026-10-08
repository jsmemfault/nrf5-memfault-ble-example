#pragma once

//! @file
//!
//! Memfault SDK configuration for this example (nRF52840, nRF5 SDK 17.1.0, S140 7.3.0).
//! Every option here overrides a default in memfault/default_config.h.

#ifdef __cplusplus
extern "C" {
#endif

// Tie each binary to its symbol file via the GNU build ID (see app.ld)
#define MEMFAULT_USE_GNU_BUILD_ID 1

// Route MEMFAULT_LOG_* into NRF_LOG (see memfault_platform_log_config.h)
#define MEMFAULT_PLATFORM_HAS_LOG_CONFIG 1

// Compact logs: format strings stay in the .elf, the device only stores arguments
#define MEMFAULT_COMPACT_LOG_ENABLE 1

// Heartbeat interval. Production would use the default (3600 s). Shortened for the demo
// (Makefile HEARTBEAT_SECS, default 300).
#ifndef MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS
  #define MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS 300
#endif

// Built-in metrics that drive Memfault's standard fleet dashboards
#define MEMFAULT_METRICS_BATTERY_ENABLE 1
#define MEMFAULT_METRICS_SYNC_SUCCESS 1
#define MEMFAULT_METRICS_MEMFAULT_SYNC_SUCCESS 1
#define MEMFAULT_METRICS_CONNECTIVITY_CONNECTED_TIME 1
#define MEMFAULT_METRICS_BLE_SESSION 1

// We choose exactly which RAM goes into a coredump (see memfault_platform_port.c) so it
// fits a small internal-flash region and stays cheap to move over BLE.
#define MEMFAULT_PLATFORM_COREDUMP_CUSTOM_REGIONS 1
#define MEMFAULT_COREDUMP_COLLECT_LOG_REGIONS 1
#define MEMFAULT_NVIC_INTERRUPTS_TO_COLLECT 48

// Custom reboot reasons (memfault_reboot_reason_user_config.def), e.g. SoftDevice asserts
#define MEMFAULT_REBOOT_REASON_CUSTOM_ENABLE 1

// Software watchdog fires 1 s before the 8 s hardware watchdog (NRFX_WDT_CONFIG_RELOAD_VALUE)
#define MEMFAULT_NRF5_WATCHDOG_SW_AUTOCONFIG 0
#define MEMFAULT_WATCHDOG_SW_TIMEOUT_SECS 7

#ifdef __cplusplus
}
#endif
