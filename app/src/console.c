//! @file
//!
//! Demo console on the DK's USB serial port (J-Link VCOM0, 115200 8N1), using the nRF5 SDK
//! CLI (nrf_cli) over UART. The CLI is also an NRF_LOG backend, so logs and commands share the
//! terminal. Demo only.
//!
//!   make console      (screen on the DK's VCOM0 port)
//!
//! Fallback when BLE isn't available: `export` prints Memfault chunks as
//! base64 "MC:" lines that the Memfault CLI or the Chunks Debug view can upload.

#include <string.h>

#include "app_error.h"
#include "boards.h"
#include "app.h"
#include "memfault/components.h"
#include "nrf_cli.h"
#include "nrf_cli_uart.h"
#include "nrf_drv_uart.h"
#include "nrf_log.h"
#include "nrf_sdm.h"

#define CONSOLE_LOG_QUEUE_SIZE 8

NRF_CLI_UART_DEF(s_cli_uart_transport, 0, 64, 16);
NRF_CLI_DEF(s_cli_uart, "demo:~$ ", &s_cli_uart_transport.transport, '\r', CONSOLE_LOG_QUEUE_SIZE);

//! Registers a top-level command that calls an (argc, argv) handler (our commands and the
//! Memfault demo component's memfault_demo_cli_cmd_* functions share this signature)
#define APP_CMD(_name, _fn, _help)                                               \
  static void prv_cmd_##_name(nrf_cli_t const *p_cli, size_t argc, char **argv) { \
    (void)_fn((int)argc, argv);                                                   \
  }                                                                               \
  NRF_CLI_CMD_REGISTER(_name, NULL, _help, prv_cmd_##_name)

static int prv_flash_dw(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "fatal") == 0) {
    demo_faults_flash_record_write_fatal();
  } else {
    demo_faults_flash_record_write();
  }
  return 0;
}

static int prv_hardfault(int argc, char *argv[]) {
  demo_faults_hardfault();
  return 0;
}

static int prv_hang(int argc, char *argv[]) {
  demo_faults_hang();
  return 0;
}

static int prv_forget_bonds(int argc, char *argv[]) {
  demo_faults_forget_bonds();
  return 0;
}

static int prv_sec_fail(int argc, char *argv[]) {
  // Same path as a real PM_EVT_CONN_SEC_FAILED with PM_CONN_SEC_ERROR_PIN_OR_KEY_MISSING,
  // for when the phone/OS won't reproduce a stale-bond reconnect on cue
  app_on_conn_sec_failed(g_app_state.conn_handle, 0 /* PM_CONN_SEC_PROCEDURE_ENCRYPTION */,
                          0x1006);
  return 0;
}

static int prv_sd_assert(int argc, char *argv[]) {
  // Exercises the app_error_fault_handler() override with NRF_FAULT_ID_SD_ASSERT, the path
  // the SoftDevice takes for its own internal asserts
  app_error_fault_handler(NRF_FAULT_ID_SD_ASSERT, 0x00012345, 0);
  return 0;
}

static int prv_brownout_test(int argc, char *argv[]) {
  app_power_brownout_test();
  return 0;
}

static int prv_sync(int argc, char *argv[]) {
  diag_service_start_sync();
  return 0;
}

static int prv_state(int argc, char *argv[]) {
  NRF_LOG_INFO("conn=0x%04x mtu=%d enc=%d notify=%d sync=%d frames=%d",
               g_app_state.conn_handle, g_app_state.att_mtu, g_app_state.link_encrypted,
               g_app_state.diag_notify_enabled, g_app_state.diag_sync_active,
               g_app_state.diag_frames_sent);
  NRF_LOG_INFO("sensor %d/%d failed, ext flash %d%%, battery %d%%, motion %s",
               g_app_state.sensor_acq_failures, g_app_state.sensor_acq_attempts,
               g_app_state.ext_flash_used_pct, g_app_state.battery_soc_pct,
               app_metrics_motion_state_name());
  return 0;
}

static int prv_heartbeat_dump(int argc, char *argv[]) {
  memfault_metrics_heartbeat_debug_print();
  return 0;
}

APP_CMD(state, prv_state, "Print app state");
APP_CMD(flash_dw, prv_flash_dw, "Flash record write (2nd = double write). 'flash_dw fatal' asserts");
APP_CMD(hardfault, prv_hardfault, "Corrupt a driver callback table -> HardFault");
APP_CMD(hang, prv_hang, "Hang -> software watchdog coredump");
APP_CMD(sd_assert, prv_sd_assert, "Simulate a SoftDevice assert (app_error_fault_handler)");
APP_CMD(brownout_test, prv_brownout_test, "Simulate a brownout: low VDD, then a POR-style reset");
APP_CMD(forget_bonds, prv_forget_bonds, "Delete bonds on the device");
APP_CMD(sec_fail, prv_sec_fail, "Record a simulated bond/key-missing failure");
APP_CMD(sync, prv_sync, "Push Memfault chunks to the connected gateway now");
// Memfault demo component commands
APP_CMD(export, memfault_demo_cli_cmd_export, "Dump Memfault chunks as base64 (MC:...)");
APP_CMD(heartbeat, memfault_demo_cli_cmd_heartbeat, "Trigger a heartbeat now");
APP_CMD(heartbeat_dump, prv_heartbeat_dump, "Print current metric values");
APP_CMD(get_core, memfault_demo_cli_cmd_get_core, "Is a coredump stored?");
APP_CMD(clear_core, memfault_demo_cli_cmd_clear_core, "Erase stored coredump");
APP_CMD(info, memfault_demo_cli_cmd_get_device_info, "Memfault device info");
APP_CMD(reboot, memfault_demo_cli_cmd_system_reboot, "Reboot (expected, user reset)");

//! Overrides the Memfault SDK's weak default, which logs each chunk with MEMFAULT_LOG_INFO. An
//! export line (~112 chars) is longer than the port's log line buffer, and deferred NRF_LOG
//! would also copy it into the Memfault log buffer. Print it directly to the console instead.
void memfault_data_export_base64_encoded_chunk(const char *base64_chunk) {
  nrf_cli_fprintf(&s_cli_uart, NRF_CLI_NORMAL, "%s\n", base64_chunk);
}

void console_init(void) {
  nrf_drv_uart_config_t uart_config = NRF_DRV_UART_DEFAULT_CONFIG;
  uart_config.pseltxd = TX_PIN_NUMBER;
  uart_config.pselrxd = RX_PIN_NUMBER;
  uart_config.hwfc = NRF_UART_HWFC_DISABLED;
  uart_config.baudrate = NRF_UART_BAUDRATE_115200;

  // true, true: colors (disabled in sdk_config), and act as an NRF_LOG backend
  ret_code_t err_code =
    nrf_cli_init(&s_cli_uart, &uart_config, true, true, NRF_LOG_SEVERITY_INFO);
  APP_ERROR_CHECK(err_code);
  err_code = nrf_cli_start(&s_cli_uart);
  APP_ERROR_CHECK(err_code);
}

void console_process(void) {
  nrf_cli_process(&s_cli_uart);
}
