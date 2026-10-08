#pragma once

//! @file
//!
//! Shared declarations for the example application.

#include <stdbool.h>
#include <stdint.h>

#include "ble.h"

#ifdef __cplusplus
extern "C" {
#endif

//! Application state, captured in every coredump (see memfault_platform_port.c)
typedef struct {
  uint16_t conn_handle;
  uint16_t att_mtu;
  bool link_encrypted;
  bool diag_notify_enabled;
  bool diag_sync_active;
  bool diag_sync_suspended;
  uint32_t diag_frames_sent;
  uint32_t sensor_acq_attempts;
  uint32_t sensor_acq_failures;
  uint8_t ext_flash_used_pct;
  uint8_t battery_soc_pct;
  uint32_t scratch_write_offset;
  uint8_t motion_state;  // 0 idle, 1 low, 2 high (simulated accelerometer)
} sAppState;

extern sAppState g_app_state;

//
// diag_service.c: Diagnostics Control Point, Memfault chunks over BLE
//
void diag_service_init(void);
void diag_service_on_ble_evt(ble_evt_t const *p_ble_evt);
//! Starts a sync without a gateway request (console "sync" command)
void diag_service_start_sync(void);

//
// app_metrics.c: device health metrics (real VDD; simulated sensors) + brownout tracking
//
void app_metrics_init(void);
uint32_t app_metrics_battery_soc_pct(void);
bool app_metrics_battery_is_charging(void);
//! Called after a successful diagnostics sync
void app_metrics_on_sync_complete(void);
const char *app_metrics_motion_state_name(void);
//! Brownout classification at boot (called from app_reboot_tracking.c). May rewrite
//! *reset_reason_reg (brownout_test). Returns true if this power-on looks like a brownout.
bool app_power_check_reset(uint32_t *reset_reason_reg);
//! Console: store a low last-VDD reading and reset as if from power-on
void app_power_brownout_test(void);

//
// demo_faults.c: failure scenarios
//
void demo_faults_init(void);
//! Writes a record to the scratch flash page through the double-write guard
void demo_faults_flash_record_write(void);
//! Same, but treat a double write as fatal (assert + coredump)
void demo_faults_flash_record_write_fatal(void);
void demo_faults_hardfault(void);
void demo_faults_hang(void);
void demo_faults_forget_bonds(void);

//
// main.c
//
//! Called from the Peer Manager event handler when a security procedure fails
void app_on_conn_sec_failed(uint16_t conn_handle, uint16_t procedure, uint16_t error);
void app_delete_bonds(void);
//! Stops advertising from restarting after the DFU-triggered disconnect
void app_advertising_on_disconnect_disable(void);

//
// dfu_service.c: buttonless secure DFU
//
void dfu_service_svci_init(void);
void dfu_service_init(void);

//
// console.c: UART console (demo only)
//
void console_init(void);
void console_process(void);

#ifdef __cplusplus
}
#endif
