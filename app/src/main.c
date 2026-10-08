//! @file
//!
//! Example: a bare-metal, event-driven BLE peripheral on nRF5 SDK 17.1.0 + S140 7.3.0, with a
//! phone/PC as its only path to the cloud, integrated with the Memfault Firmware SDK.
//!
//! Structure follows examples/ble_peripheral/ble_app_template. Memfault touchpoints are marked
//! "MEMFAULT:".

#include <stdint.h>
#include <string.h>

#include "app_button.h"
#include "app_error.h"
#include "app_timer.h"
#include "boards.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_dis.h"
#include "ble_hci.h"
#include "fds.h"
#include "app.h"
#include "memfault/components.h"
#include "memfault/metrics/ble_session.h"
#include "memfault/metrics/connectivity.h"
#include "memfault/ports/watchdog.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_drv_clock.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_log_backend_memfault.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrfx_wdt.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"

#define DEVICE_NAME "nRF5-MFLT-Demo"
#define APP_ADV_INTERVAL MSEC_TO_UNITS(100, UNIT_0_625_MS)
#define APP_ADV_DURATION 0  // advertise forever
#define APP_BLE_OBSERVER_PRIO 3
#define APP_BLE_CONN_CFG_TAG 1

// Faster than a battery-powered product would use, so syncs are quick during the demo
#define MIN_CONN_INTERVAL MSEC_TO_UNITS(15, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(30, UNIT_1_25_MS)
#define SLAVE_LATENCY 0
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(30000)
#define MAX_CONN_PARAMS_UPDATE_COUNT 3

// SoftDevice notification queue depth: more in-flight notifications = faster chunk drain
#define HVN_TX_QUEUE_SIZE 8

#define BUTTON_DETECTION_DELAY APP_TIMER_TICKS(50)

#define LED_CONNECTED BSP_BOARD_LED_0
#define LED_ADVERTISING BSP_BOARD_LED_1

NRF_BLE_GATT_DEF(m_gatt);
NRF_BLE_QWR_DEF(m_qwr);
BLE_ADVERTISING_DEF(m_advertising);

sAppState g_app_state = {
  .conn_handle = BLE_CONN_HANDLE_INVALID,
  .att_mtu = BLE_GATT_ATT_MTU_DEFAULT,
};

static bool s_delete_bonds_on_disconnect;

static void advertising_start(void);

//
// MEMFAULT: security failures (bond/key mismatch between phone and device)
//

void app_on_conn_sec_failed(uint16_t conn_handle, uint16_t procedure, uint16_t error) {
  MEMFAULT_METRIC_ADD(ble_pairing_failures, 1);
  // Status = PM_CONN_SEC_ERROR_* / BLE_GAP_SEC_STATUS_*: e.g. 0x1006 = PIN or key missing,
  // which is what a device that lost its bond reports when the phone reconnects with old keys.
  MEMFAULT_TRACE_EVENT_WITH_STATUS(ble_conn_sec_failed, error);
  MEMFAULT_LOG_ERROR("Conn sec failed: conn=%d procedure=%d error=0x%x", conn_handle, procedure,
                     error);
  // Ship the logs leading up to this failure with the next sync
  memfault_log_trigger_collection();
}

static void pm_evt_handler(pm_evt_t const *p_evt) {
  pm_handler_on_pm_evt(p_evt);
  pm_handler_disconnect_on_sec_failure(p_evt);
  pm_handler_flash_clean(p_evt);

  switch (p_evt->evt_id) {
    case PM_EVT_CONN_SEC_SUCCEEDED:
      MEMFAULT_LOG_INFO("Link secured, procedure=%d",
                        p_evt->params.conn_sec_succeeded.procedure);
      break;

    case PM_EVT_CONN_SEC_FAILED:
      app_on_conn_sec_failed(p_evt->conn_handle, p_evt->params.conn_sec_failed.procedure,
                              p_evt->params.conn_sec_failed.error);
      break;

    case PM_EVT_CONN_SEC_CONFIG_REQ: {
      // Let a phone that lost its keys pair again instead of being locked out
      pm_conn_sec_config_t config = { .allow_repairing = true };
      pm_conn_sec_config_reply(p_evt->conn_handle, &config);
      break;
    }

    case PM_EVT_PEERS_DELETE_SUCCEEDED:
      NRF_LOG_INFO("All bonds deleted");
      advertising_start();
      break;

    default:
      break;
  }
}

void app_delete_bonds(void) {
  if (g_app_state.conn_handle != BLE_CONN_HANDLE_INVALID) {
    s_delete_bonds_on_disconnect = true;
    sd_ble_gap_disconnect(g_app_state.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    return;
  }
  (void)sd_ble_gap_adv_stop(m_advertising.adv_handle);
  ret_code_t err_code = pm_peers_delete();
  APP_ERROR_CHECK(err_code);
}

//
// BLE events
//

static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
  ret_code_t err_code;

  switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
      g_app_state.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
      g_app_state.link_encrypted = false;
      err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, g_app_state.conn_handle);
      APP_ERROR_CHECK(err_code);
      bsp_board_led_on(LED_CONNECTED);
      bsp_board_led_off(LED_ADVERTISING);

      // MEMFAULT: connection metrics
      MEMFAULT_METRIC_ADD(ble_connect_count, 1);
      memfault_metrics_ble_session_connected();
      memfault_metrics_connectivity_connected_state_change(
        kMemfaultMetricsConnectivityState_Connected);
      NRF_LOG_INFO("Connected");
      break;

    case BLE_GAP_EVT_DISCONNECTED: {
      const uint8_t reason = p_ble_evt->evt.gap_evt.params.disconnected.reason;
      NRF_LOG_INFO("Disconnected, reason 0x%02x", reason);
      g_app_state.conn_handle = BLE_CONN_HANDLE_INVALID;
      g_app_state.link_encrypted = false;
      g_app_state.att_mtu = BLE_GATT_ATT_MTU_DEFAULT;
      bsp_board_led_off(LED_CONNECTED);

      // MEMFAULT: one bt_conn session per connection, tagged with the HCI disconnect reason
      memfault_metrics_ble_session_disconnected(reason);
      memfault_metrics_connectivity_connected_state_change(
        kMemfaultMetricsConnectivityState_Started);

      if (s_delete_bonds_on_disconnect) {
        s_delete_bonds_on_disconnect = false;
        err_code = pm_peers_delete();
        APP_ERROR_CHECK(err_code);
      }
      break;
    }

    case BLE_GAP_EVT_CONN_SEC_UPDATE:
      g_app_state.link_encrypted =
        p_ble_evt->evt.gap_evt.params.conn_sec_update.conn_sec.sec_mode.lv > 1;
      break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
      ble_gap_phys_t const phys = { .rx_phys = BLE_GAP_PHY_AUTO, .tx_phys = BLE_GAP_PHY_AUTO };
      err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
      APP_ERROR_CHECK(err_code);
      break;
    }

    case BLE_GATTC_EVT_TIMEOUT:
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                       BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GATTS_EVT_TIMEOUT:
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                       BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(err_code);
      break;

    default:
      break;
  }

  diag_service_on_ble_evt(p_ble_evt);
}

static void gatt_evt_handler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const *p_evt) {
  if (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED) {
    g_app_state.att_mtu = p_evt->params.att_mtu_effective;
    NRF_LOG_INFO("ATT MTU %d", g_app_state.att_mtu);
  }
}

static void ble_stack_init(void) {
  ret_code_t err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  ble_cfg_t ble_cfg = { 0 };
  ble_cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
  ble_cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = HVN_TX_QUEUE_SIZE;
  err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &ble_cfg, ram_start);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static void gap_params_init(void) {
  ble_gap_conn_sec_mode_t sec_mode;
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  ret_code_t err_code =
    sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
  APP_ERROR_CHECK(err_code);

  ble_gap_conn_params_t gap_conn_params = {
    .min_conn_interval = MIN_CONN_INTERVAL,
    .max_conn_interval = MAX_CONN_INTERVAL,
    .slave_latency = SLAVE_LATENCY,
    .conn_sup_timeout = CONN_SUP_TIMEOUT,
  };
  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}

static void gatt_init(void) {
  ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
  APP_ERROR_CHECK(err_code);
}

static void nrf_qwr_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

static void services_init(void) {
  nrf_ble_qwr_init_t qwr_init = { .error_handler = nrf_qwr_error_handler };
  ret_code_t err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(err_code);

  diag_service_init();
  dfu_service_init();

  // Device Information Service: the app reads the serial (for the Memfault chunks URL) and
  // firmware revision (what's installed, for release tracking) here
  sMemfaultDeviceInfo info;
  memfault_platform_get_device_info(&info);
  ble_dis_init_t dis_init = { .dis_char_rd_sec = SEC_OPEN };
  ble_srv_ascii_to_utf8(&dis_init.manufact_name_str, "Memfault nRF5 Demo");
  ble_srv_ascii_to_utf8(&dis_init.serial_num_str, (char *)info.device_serial);
  ble_srv_ascii_to_utf8(&dis_init.fw_rev_str, (char *)info.software_version);
  ble_srv_ascii_to_utf8(&dis_init.hw_rev_str, (char *)info.hardware_version);
  err_code = ble_dis_init(&dis_init);
  APP_ERROR_CHECK(err_code);
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
  if (ble_adv_evt == BLE_ADV_EVT_FAST) {
    bsp_board_led_on(LED_ADVERTISING);
    memfault_metrics_connectivity_connected_state_change(
      kMemfaultMetricsConnectivityState_Started);
  }
}

static void advertising_init(void) {
  ble_advertising_init_t init = { 0 };

  init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
  init.advdata.include_appearance = false;
  init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

  init.config.ble_adv_fast_enabled = true;
  init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
  init.config.ble_adv_fast_timeout = APP_ADV_DURATION;
  init.evt_handler = on_adv_evt;

  ret_code_t err_code = ble_advertising_init(&m_advertising, &init);
  APP_ERROR_CHECK(err_code);
  ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

void app_advertising_on_disconnect_disable(void) {
  ble_adv_modes_config_t config = m_advertising.adv_modes_config;
  config.ble_adv_on_disconnect_disabled = true;
  ble_advertising_modes_config_set(&m_advertising, &config);
}

static void advertising_start(void) {
  ret_code_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
  if (err_code != NRF_ERROR_INVALID_STATE) {  // already advertising
    APP_ERROR_CHECK(err_code);
  }
}

static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

static void conn_params_init(void) {
  ble_conn_params_init_t cp_init = {
    .p_conn_params = NULL,
    .first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY,
    .next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY,
    .max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT,
    .start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID,
    .disconnect_on_fail = false,
    .error_handler = conn_params_error_handler,
  };
  ret_code_t err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
}

static void peer_manager_init(void) {
  ret_code_t err_code = pm_init();
  APP_ERROR_CHECK(err_code);

  // Just Works bonding, as for a device with no display/keyboard
  ble_gap_sec_params_t sec_param = {
    .bond = 1,
    .mitm = 0,
    .lesc = 0,
    .keypress = 0,
    .io_caps = BLE_GAP_IO_CAPS_NONE,
    .oob = 0,
    .min_key_size = 7,
    .max_key_size = 16,
    .kdist_own = { .enc = 1, .id = 1 },
    .kdist_peer = { .enc = 1, .id = 1 },
  };
  err_code = pm_sec_params_set(&sec_param);
  APP_ERROR_CHECK(err_code);

  err_code = pm_register(pm_evt_handler);
  APP_ERROR_CHECK(err_code);
}

//
// Buttons: demo failure scenarios
//

static void button_event_handler(uint8_t pin_no, uint8_t button_action) {
  if (button_action != APP_BUTTON_PUSH) {
    return;
  }
  switch (pin_no) {
    case BUTTON_1:
      demo_faults_flash_record_write();
      break;
    case BUTTON_2:
      demo_faults_hardfault();
      break;
    case BUTTON_3:
      demo_faults_hang();
      break;
    case BUTTON_4:
      demo_faults_forget_bonds();
      break;
    default:
      break;
  }
}

static void buttons_init(void) {
  static const app_button_cfg_t buttons[] = {
    { BUTTON_1, APP_BUTTON_ACTIVE_LOW, BUTTON_PULL, button_event_handler },
    { BUTTON_2, APP_BUTTON_ACTIVE_LOW, BUTTON_PULL, button_event_handler },
    { BUTTON_3, APP_BUTTON_ACTIVE_LOW, BUTTON_PULL, button_event_handler },
    { BUTTON_4, APP_BUTTON_ACTIVE_LOW, BUTTON_PULL, button_event_handler },
  };
  ret_code_t err_code =
    app_button_init(buttons, ARRAY_SIZE(buttons), BUTTON_DETECTION_DELAY);
  APP_ERROR_CHECK(err_code);
  err_code = app_button_enable();
  APP_ERROR_CHECK(err_code);
}

//
// Watchdogs
//
// MEMFAULT: the RTC2 software watchdog (7 s) fires before the hardware WDT (8 s) and
// captures a coredump of whatever is stuck. The HW WDT stays as the backstop.
//

static nrfx_wdt_channel_id s_wdt_channel_id;

static void wdt_event_handler(void) {
  // Only reachable if the software watchdog didn't fire first. ~60 us until reset.
  MEMFAULT_REBOOT_MARK_RESET_IMMINENT(kMfltRebootReason_HardwareWatchdog);
}

static void watchdogs_init(void) {
  nrfx_wdt_config_t config = NRFX_WDT_DEAFULT_CONFIG;
  ret_code_t err_code = nrfx_wdt_init(&config, wdt_event_handler);
  APP_ERROR_CHECK(err_code);
  err_code = nrfx_wdt_channel_alloc(&s_wdt_channel_id);
  APP_ERROR_CHECK(err_code);
  nrfx_wdt_enable();

  memfault_software_watchdog_enable();
}

static void watchdogs_feed(void) {
  nrfx_wdt_channel_feed(s_wdt_channel_id);
  memfault_software_watchdog_feed();
}

//
// Main
//

static void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void timers_init(void) {
  ret_code_t err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);
}

static void power_management_init(void) {
  ret_code_t err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
}

int main(void) {
  log_init();
  timers_init();

  // MEMFAULT: as early as possible, after app_timer (heartbeat timer) is up.
  // Collects the reboot reason of the previous boot and any coredump it left behind.
  memfault_platform_boot();
  // MEMFAULT: capture existing NRF_LOG output in the Memfault log buffer
  nrf_log_backend_memfault_init();

  bsp_board_init(BSP_INIT_LEDS);
  power_management_init();
  dfu_service_svci_init();  // before the SoftDevice is enabled
  ble_stack_init();
  gap_params_init();
  gatt_init();
  services_init();
  advertising_init();
  conn_params_init();
  peer_manager_init();

  demo_faults_init();
  app_metrics_init();
  buttons_init();
  console_init();
  watchdogs_init();

  NRF_LOG_INFO("Memfault nRF5 demo %s started", APP_FW_VERSION);
  advertising_start();

  while (true) {
    watchdogs_feed();
    console_process();
    if (!NRF_LOG_PROCESS()) {
      nrf_pwr_mgmt_run();
    }
  }
}
