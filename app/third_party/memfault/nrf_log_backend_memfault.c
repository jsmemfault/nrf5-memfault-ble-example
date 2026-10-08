//! @file
//!
//! NRF_LOG backend that feeds the Memfault log buffer.
//!
//! With this, existing NRF_LOG_INFO/WARNING/ERROR calls (app and SDK) end up in:
//!   - every coredump (the log buffer is a coredump region)
//!   - log uploads triggered by memfault_log_trigger_collection()
//! with no changes at the call sites. That matters for a mature nRF5 codebase, where
//! years of NRF_LOG instrumentation already exist.
//!
//! Notes:
//!   - Lines are stored as text (memfault_log_save_preformatted), not as compact logs. Size
//!     the Memfault log buffer for that.
//!   - With NRF_LOG_DEFERRED, lines reach backends from NRF_LOG_PROCESS(). On a crash,
//!     memfault_platform_fault_handler() (memfault_platform_port.c) runs NRF_LOG_FINAL_FLUSH()
//!     so lines still queued in the nrf_log ring buffer make it into the coredump.
//!   - Debug-level lines are filtered out (NRF_LOG_BACKEND_MEMFAULT_SEVERITY).

#include <string.h>

#include "memfault/components.h"
#include "nrf_log_backend_interface.h"
#include "nrf_log_backend_memfault.h"
#include "nrf_log_backend_serial.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_internal.h"
#include "nrf_memobj.h"

#ifndef NRF_LOG_BACKEND_MEMFAULT_SEVERITY
  #define NRF_LOG_BACKEND_MEMFAULT_SEVERITY NRF_LOG_SEVERITY_INFO
#endif

#define LINE_MAX_LEN 128

static char s_line[LINE_MAX_LEN];
static size_t s_line_len;
static eMemfaultPlatformLogLevel s_line_level;
static uint8_t s_fmt_buf[64];
static bool s_in_panic;

static eMemfaultPlatformLogLevel prv_level(nrf_log_severity_t severity) {
  switch (severity) {
    case NRF_LOG_SEVERITY_ERROR:
      return kMemfaultPlatformLogLevel_Error;
    case NRF_LOG_SEVERITY_WARNING:
      return kMemfaultPlatformLogLevel_Warning;
    case NRF_LOG_SEVERITY_INFO:
      return kMemfaultPlatformLogLevel_Info;
    default:
      return kMemfaultPlatformLogLevel_Debug;
  }
}

static void prv_line_commit(void) {
  // Strip the "<info> " style prefix: Memfault records the level itself
  const char *start = s_line;
  size_t len = s_line_len;
  if (len > 0 && start[0] == '<') {
    const char *end = memchr(start, '>', len);
    if (end != NULL && (size_t)(end - start) + 2 <= len) {
      len -= (size_t)(end - start) + 2;
      start = end + 2;
    }
  }
  if (len > 0) {
    if (s_in_panic) {
      // Called from the fault handler: memfault_lock() would mask IRQs that are already
      // irrelevant, and we must not recurse into anything that could fault
      memfault_log_save_preformatted_nolock(s_line_level, start, len);
    } else {
      memfault_log_save_preformatted(s_line_level, start, len);
    }
  }
  s_line_len = 0;
}

//! fwrite callback from nrf_log_backend_serial_put(): may be called several times per line
static void prv_tx(void const *p_context, char const *p_buffer, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = p_buffer[i];
    if (c == '\n') {
      prv_line_commit();
    } else if (c != '\r' && s_line_len < LINE_MAX_LEN) {
      s_line[s_line_len++] = c;
    }
  }
}

static void prv_put(nrf_log_backend_t const *p_backend, nrf_log_entry_t *p_msg) {
  nrf_log_header_t header;
  nrf_memobj_read(p_msg, &header, HEADER_SIZE * sizeof(uint32_t), 0);

  nrf_log_severity_t severity = NRF_LOG_SEVERITY_INFO;
  if (header.base.generic.type == HEADER_TYPE_STD) {
    severity = (nrf_log_severity_t)header.base.std.severity;
  } else if (header.base.generic.type == HEADER_TYPE_HEXDUMP) {
    severity = (nrf_log_severity_t)header.base.hexdump.severity;
  }
  s_line_level = prv_level(severity);
  s_line_len = 0;

  // Formats the entry exactly like the RTT/UART backends and releases the memobj
  nrf_log_backend_serial_put(p_backend, p_msg, s_fmt_buf, sizeof(s_fmt_buf), prv_tx);
  if (s_line_len > 0) {
    prv_line_commit();
  }
}

static void prv_panic_set(nrf_log_backend_t const *p_backend) {
  s_in_panic = true;
}

static void prv_flush(nrf_log_backend_t const *p_backend) { }

static const nrf_log_backend_api_t s_memfault_backend_api = {
  .put = prv_put,
  .panic_set = prv_panic_set,
  .flush = prv_flush,
};

NRF_LOG_BACKEND_DEF(s_memfault_log_backend, s_memfault_backend_api, NULL);

void nrf_log_backend_memfault_init(void) {
  const int32_t id =
    nrf_log_backend_add(&s_memfault_log_backend, NRF_LOG_BACKEND_MEMFAULT_SEVERITY);
  if (id >= 0) {
    nrf_log_backend_enable(&s_memfault_log_backend);
  }
}
