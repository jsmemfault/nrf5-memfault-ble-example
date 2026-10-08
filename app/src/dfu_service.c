//! @file
//!
//! Buttonless Secure DFU, the way a phone app (e.g. nRF Connect, nRF Device Manager) sends
//! the device into the Nordic secure bootloader. Follows
//! examples/ble_peripheral/ble_app_buttonless_dfu (nRF5 SDK 17.1).
//!
//! MEMFAULT: the one hook that matters for OTA visibility is marking the reset as
//! kMfltRebootReason_FirmwareUpdate before jumping to the bootloader. Memfault then sees:
//!   - a FirmwareUpdate reboot event on the device timeline
//!   - the software version change on the next heartbeat or event (from device info)
//!   - adoption per version, and crash-free rate per version, with no change to the DFU flow
//! A DFU that fails and leaves the old app in place shows up as FirmwareUpdate with no version
//! change, which is easy to build a fleet chart or alert on.
//!
//! Note: this uses the *unbonded* buttonless variant. A product that requires bonds
//! would use the bonded variant (NRF_DFU_BLE_BUTTONLESS_SUPPORTS_BONDS + a bootloader built
//! with NRF_DFU_BLE_REQUIRES_BONDS). The Memfault hook is the same either way.

#include "app_error.h"
#include "ble_advertising.h"
#include "ble_conn_state.h"
#include "ble_dfu.h"
#include "ble_hci.h"
#include "app.h"
#include "memfault/components.h"
#include "nrf_bootloader_info.h"
#include "nrf_log.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"

static void prv_disconnect(uint16_t conn_handle, void *p_context) {
  const ret_code_t err_code =
    sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  if (err_code != NRF_SUCCESS) {
    NRF_LOG_WARNING("Failed to disconnect 0x%x: 0x%x", conn_handle, err_code);
  }
}

static void prv_dfu_evt_handler(ble_dfu_buttonless_evt_type_t event) {
  switch (event) {
    case BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE:
      NRF_LOG_INFO("DFU: preparing to enter bootloader");
      app_advertising_on_disconnect_disable();
      ble_conn_state_for_each_connected(prv_disconnect, NULL);
      break;

    case BLE_DFU_EVT_BOOTLOADER_ENTER:
      NRF_LOG_INFO("DFU: entering bootloader");
      break;

    case BLE_DFU_EVT_BOOTLOADER_ENTER_FAILED:
      // Visible in Memfault: a DFU that never started
      MEMFAULT_TRACE_EVENT_WITH_STATUS(dfu_enter_failed, 0);
      NRF_LOG_ERROR("DFU: entering bootloader failed");
      break;

    case BLE_DFU_EVT_RESPONSE_SEND_ERROR:
      MEMFAULT_TRACE_EVENT_WITH_STATUS(dfu_enter_failed, 1);
      NRF_LOG_ERROR("DFU: response send error");
      break;

    default:
      break;
  }
}

//! Called by nrf_pwr_mgmt before resetting into the bootloader
static bool prv_shutdown_handler(nrf_pwr_mgmt_evt_t event) {
  if (event == NRF_PWR_MGMT_EVT_PREPARE_DFU) {
    // MEMFAULT: record why we're resetting. The reboot-tracking block (bottom of the app stack)
    // sits below the bootloader's RAM use (see app.ld), so this survives
    // app -> bootloader -> new app.
    MEMFAULT_REBOOT_MARK_RESET_IMMINENT(kMfltRebootReason_FirmwareUpdate);
    NRF_LOG_INFO("DFU: reset to bootloader allowed");
  }
  return true;
}

NRF_PWR_MGMT_HANDLER_REGISTER(prv_shutdown_handler, 0);

static void prv_sdh_state_observer(nrf_sdh_state_evt_t state, void *p_context) {
  if (state == NRF_SDH_EVT_STATE_DISABLED) {
    // SoftDevice disabled on the way into DFU: tell the bootloader to skip the CRC check
    nrf_power_gpregret2_set(BOOTLOADER_DFU_SKIP_CRC);
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
  }
}

NRF_SDH_STATE_OBSERVER(m_buttonless_dfu_state_obs, 0) = {
  .handler = prv_sdh_state_observer,
};

void dfu_service_svci_init(void) {
  // Must run before the SoftDevice is enabled
  const ret_code_t err_code = ble_dfu_buttonless_async_svci_init();
  APP_ERROR_CHECK(err_code);
}

void dfu_service_init(void) {
  ble_dfu_buttonless_init_t dfus_init = { .evt_handler = prv_dfu_evt_handler };
  const ret_code_t err_code = ble_dfu_buttonless_init(&dfus_init);
  APP_ERROR_CHECK(err_code);
}
