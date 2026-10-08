//! @file
//!
//! Diagnostics Control Point (DCP): a minimal custom GATT service that moves Memfault data
//! from the device to a phone/PC gateway.
//!
//! One characteristic, write + notify. The gateway writes a one-byte opcode; the device
//! answers with notifications on the same characteristic. Every data frame carries one opaque
//! Memfault "chunk", which the gateway POSTs to
//!   https://chunks.memfault.com/api/v0/chunks/<device_serial>
//! The gateway never parses Memfault data.
//!
//! Opcodes (gateway -> device)       Responses (device -> gateway, notifications)
//!   0x01 GET_STATUS                   [0x01, pending:u8, event_storage_used_pct:u8]
//!   0x02 START_SYNC                   [0x02, 0x00]                     started
//!                                     [0x10, frame:u24, chunk...]      one per chunk
//!                                     [0x02, 0x02, frames:u24]         complete
//!   0x03 SUSPEND_SYNC                 [0x03, 0x00]
//!   0x04 RESUME_SYNC                  [0x04, 0x00]
//! Errors: [opcode, 0x01] busy (sync in progress), [opcode, 0x80] unknown opcode.
//! GET_STATUS, SUSPEND_SYNC and RESUME_SYNC are accepted during a sync.
//!
//! Notes:
//!   - Chunk size = ATT_MTU - 3 (notification payload) - 4 (frame header): 240 B at MTU 247.
//!   - Memfault produces chunks on demand, so the total count isn't known up front; the end of
//!     a sync is an explicit "complete" notification.
//!   - On a disconnect mid-sync, memfault_packetizer_abort() makes the interrupted message
//!     (e.g. a coredump) re-send from its start on the next sync.

#include <string.h>

#include "app.h"
#include "app_error.h"
#include "ble_srv_common.h"
#include "memfault/components.h"
#include "memfault/metrics/connectivity.h"
#include "nrf_log.h"

// 128-bit base UUID, LSB first (randomly generated for this example). Bytes 12-13 are
// replaced by the 16-bit UUIDs below: service 7a0c0001-e3c5-218a-4f9c-64a7521d8e3b
#define DCP_UUID_BASE                                                                          \
  { 0x3b, 0x8e, 0x1d, 0x52, 0xa7, 0x64, 0x9c, 0x4f, 0x8a, 0x21, 0xc5, 0xe3, 0x00, 0x00, 0x0c, \
    0x7a }
#define DCP_UUID_SERVICE 0x0001
#define DCP_UUID_CP_CHAR 0x0002

#ifndef DIAG_REQUIRE_BOND
  #define DIAG_REQUIRE_BOND 1
#endif

#define DCP_MAX_LEN 244  // NRF_SDH_BLE_GATT_MAX_MTU_SIZE (247) - 3
#define DCP_FRAME_HDR_LEN 4  // opcode + u24 frame counter

#define DCP_OP_GET_STATUS 0x01
#define DCP_OP_START_SYNC 0x02
#define DCP_OP_SUSPEND_SYNC 0x03
#define DCP_OP_RESUME_SYNC 0x04
#define DCP_OP_DATA_FRAME 0x10

#define DCP_STATUS_OK 0x00
#define DCP_STATUS_BUSY 0x01
#define DCP_STATUS_COMPLETE 0x02
#define DCP_STATUS_UNKNOWN_OPCODE 0x80

static uint16_t s_service_handle;
static ble_gatts_char_handles_t s_cp_handles;

// One frame waiting for SoftDevice TX buffer space (NRF_ERROR_RESOURCES)
static uint8_t s_pending_frame[DCP_MAX_LEN];
static uint16_t s_pending_len;
static bool s_complete_pending;

static uint32_t prv_notify(const uint8_t *data, uint16_t len) {
  if (g_app_state.conn_handle == BLE_CONN_HANDLE_INVALID || !g_app_state.diag_notify_enabled) {
    return NRF_ERROR_INVALID_STATE;
  }

  uint16_t hvx_len = len;
  ble_gatts_hvx_params_t hvx = {
    .handle = s_cp_handles.value_handle,
    .type = BLE_GATT_HVX_NOTIFICATION,
    .p_len = &hvx_len,
    .p_data = data,
  };
  return sd_ble_gatts_hvx(g_app_state.conn_handle, &hvx);
}

static void prv_notify_status(uint8_t opcode, uint8_t status) {
  const uint8_t rsp[] = { opcode, status };
  const uint32_t rv = prv_notify(rsp, sizeof(rsp));
  if (rv != NRF_SUCCESS) {
    NRF_LOG_WARNING("DCP: status notify failed: 0x%x", rv);
  }
}

static void prv_put_u24(uint8_t *dst, uint32_t val) {
  dst[0] = val & 0xff;
  dst[1] = (val >> 8) & 0xff;
  dst[2] = (val >> 16) & 0xff;
}

static uint16_t prv_max_chunk_len(void) {
  const uint16_t payload = MIN(g_app_state.att_mtu - 3, DCP_MAX_LEN);
  return payload - DCP_FRAME_HDR_LEN;
}

static void prv_sync_end(bool success) {
  g_app_state.diag_sync_active = false;
  g_app_state.diag_sync_suspended = false;
  s_pending_len = 0;
  s_complete_pending = false;

  MEMFAULT_METRICS_SESSION_END(diag_sync);
  if (success) {
    memfault_metrics_connectivity_record_memfault_sync_success();
    app_metrics_on_sync_complete();
  } else {
    // Re-send any partially transmitted message (e.g. a coredump) from its start next time
    memfault_packetizer_abort();
    memfault_metrics_connectivity_record_memfault_sync_failure();
  }
}

static bool prv_try_send_complete(void) {
  uint8_t rsp[5] = { DCP_OP_START_SYNC, DCP_STATUS_COMPLETE };
  prv_put_u24(&rsp[2], g_app_state.diag_frames_sent);

  const uint32_t rv = prv_notify(rsp, sizeof(rsp));
  if (rv == NRF_ERROR_RESOURCES) {
    s_complete_pending = true;
    return false;
  }
  NRF_LOG_INFO("DCP: sync complete, %u frames", g_app_state.diag_frames_sent);
  prv_sync_end(rv == NRF_SUCCESS);
  return true;
}

//! Moves Memfault chunks into notifications until the SoftDevice TX queue is full or there's
//! no more data. Called again on BLE_GATTS_EVT_HVN_TX_COMPLETE.
static void prv_pump(void) {
  while (g_app_state.diag_sync_active && !g_app_state.diag_sync_suspended) {
    if (s_complete_pending) {
      prv_try_send_complete();
      return;
    }

    if (s_pending_len == 0) {
      size_t chunk_len = prv_max_chunk_len();
      if (!memfault_packetizer_get_chunk(&s_pending_frame[DCP_FRAME_HDR_LEN], &chunk_len)) {
        prv_try_send_complete();
        return;
      }
      s_pending_frame[0] = DCP_OP_DATA_FRAME;
      prv_put_u24(&s_pending_frame[1], g_app_state.diag_frames_sent + 1);
      s_pending_len = chunk_len + DCP_FRAME_HDR_LEN;
    }

    const uint32_t rv = prv_notify(s_pending_frame, s_pending_len);
    if (rv == NRF_ERROR_RESOURCES) {
      return;  // SoftDevice queue full, resume on HVN_TX_COMPLETE
    }
    if (rv != NRF_SUCCESS) {
      NRF_LOG_WARNING("DCP: notify failed 0x%x, aborting sync", rv);
      MEMFAULT_TRACE_EVENT_WITH_STATUS(sd_api_error, (int32_t)rv);
      prv_sync_end(false);
      return;
    }

    g_app_state.diag_frames_sent++;
    MEMFAULT_METRIC_ADD(diag_tx_bytes, s_pending_len);
    MEMFAULT_METRIC_SESSION_ADD(diag_sync_chunks, diag_sync, 1);
    MEMFAULT_METRIC_SESSION_ADD(diag_sync_bytes, diag_sync, s_pending_len);
    s_pending_len = 0;
  }
}

static void prv_start_sync(void) {
  if (g_app_state.diag_sync_active) {
    prv_notify_status(DCP_OP_START_SYNC, DCP_STATUS_BUSY);
    return;
  }

  g_app_state.diag_sync_active = true;
  g_app_state.diag_sync_suspended = false;
  g_app_state.diag_frames_sent = 0;
  MEMFAULT_METRIC_ADD(diag_sync_count, 1);
  MEMFAULT_METRICS_SESSION_START(diag_sync);

  NRF_LOG_INFO("DCP: sync started (max chunk %u B)", prv_max_chunk_len());
  prv_notify_status(DCP_OP_START_SYNC, DCP_STATUS_OK);
  prv_pump();
}

static void prv_on_cp_write(const uint8_t *data, uint16_t len) {
  if (len < 1) {
    return;
  }
  const uint8_t opcode = data[0];

  // Only status, suspend and resume are accepted while a sync is in progress
  if (g_app_state.diag_sync_active && opcode != DCP_OP_GET_STATUS &&
      opcode != DCP_OP_SUSPEND_SYNC && opcode != DCP_OP_RESUME_SYNC) {
    prv_notify_status(opcode, DCP_STATUS_BUSY);
    return;
  }

  switch (opcode) {
    case DCP_OP_GET_STATUS: {
      const size_t used = memfault_event_storage_bytes_used();
      const size_t total = used + memfault_event_storage_bytes_free();
      const uint8_t rsp[3] = { opcode, memfault_packetizer_data_available() ? 1 : 0,
                               (uint8_t)(total ? (used * 100) / total : 0) };
      prv_notify(rsp, sizeof(rsp));
      break;
    }
    case DCP_OP_START_SYNC:
      prv_start_sync();
      break;
    case DCP_OP_SUSPEND_SYNC:
      g_app_state.diag_sync_suspended = true;
      prv_notify_status(opcode, DCP_STATUS_OK);
      break;
    case DCP_OP_RESUME_SYNC:
      g_app_state.diag_sync_suspended = false;
      prv_notify_status(opcode, DCP_STATUS_OK);
      prv_pump();
      break;
    default:
      prv_notify_status(opcode, DCP_STATUS_UNKNOWN_OPCODE);
      break;
  }
}

void diag_service_on_ble_evt(ble_evt_t const *p_ble_evt) {
  switch (p_ble_evt->header.evt_id) {
    case BLE_GATTS_EVT_WRITE: {
      const ble_gatts_evt_write_t *w = &p_ble_evt->evt.gatts_evt.params.write;
      if (w->handle == s_cp_handles.cccd_handle && w->len == 2) {
        g_app_state.diag_notify_enabled = ble_srv_is_notification_enabled(w->data);
        NRF_LOG_INFO("DCP: notifications %s",
                     g_app_state.diag_notify_enabled ? "enabled" : "disabled");
      } else if (w->handle == s_cp_handles.value_handle) {
        prv_on_cp_write(w->data, w->len);
      }
      break;
    }

    case BLE_GATTS_EVT_HVN_TX_COMPLETE:
      prv_pump();
      break;

    case BLE_GAP_EVT_DISCONNECTED:
      if (g_app_state.diag_sync_active) {
        NRF_LOG_WARNING("DCP: disconnected mid-sync after %u frames",
                        g_app_state.diag_frames_sent);
        prv_sync_end(false);
      }
      g_app_state.diag_notify_enabled = false;
      break;

    default:
      break;
  }
}

void diag_service_start_sync(void) {
  if (g_app_state.conn_handle == BLE_CONN_HANDLE_INVALID || !g_app_state.diag_notify_enabled) {
    NRF_LOG_WARNING("DCP: no gateway subscribed, not syncing");
    return;
  }
  prv_start_sync();
}

void diag_service_init(void) {
  ble_uuid128_t base_uuid = { DCP_UUID_BASE };
  ble_uuid_t service_uuid = { .uuid = DCP_UUID_SERVICE };

  uint32_t err_code = sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type);
  APP_ERROR_CHECK(err_code);

  err_code =
    sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service_uuid, &s_service_handle);
  APP_ERROR_CHECK(err_code);

  // By default writes and notifications need an encrypted link (Just Works pairing; Peer
  // Manager also bonds). Build with DIAG_REQUIRE_BOND=0 for gateways that can't complete
  // pairing unattended.
#if DIAG_REQUIRE_BOND
  const security_req_t access = SEC_JUST_WORKS;
#else
  const security_req_t access = SEC_OPEN;
#endif
  ble_add_char_params_t params = {
    .uuid = DCP_UUID_CP_CHAR,
    .uuid_type = service_uuid.type,
    .max_len = DCP_MAX_LEN,
    .init_len = 0,
    .is_var_len = true,
    .char_props = { .write = 1, .notify = 1 },
    .read_access = SEC_NO_ACCESS,
    .write_access = access,
    .cccd_write_access = access,
  };
  err_code = characteristic_add(s_service_handle, &params, &s_cp_handles);
  APP_ERROR_CHECK(err_code);
}
