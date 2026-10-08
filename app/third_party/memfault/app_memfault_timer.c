//! @file
//!
//! Memfault metrics timer + uptime for this example. Replaces
//! memfault-firmware-sdk/ports/nrf5_sdk/memfault_platform_metrics.c.
//!
//! Why: the stock port creates its own repeating app_timer. When the heartbeat interval is
//! longer than one app_timer counter wrap (24-bit RTC: 1024 s at APP_TIMER_CONFIG_RTC_FREQUENCY
//! 1, 512 s at 0), it falls back to a **1-minute** timer and counts minutes. A 1-hour
//! heartbeat then costs 60 extra wakeups an hour, on a device whose power budget is set by
//! its wake pattern.
//!
//! Here Memfault gets no timer of its own. The application calls
//! app_memfault_timer_tick() from a wakeup it already has (in a product: e.g. the periodic
//! acquisition tick; in this example: the 10 s application tick in app_metrics.c).
//! Elapsed time comes from the RTC counter, so the heartbeat stays accurate whatever the tick
//! period is.
//!
//! The one requirement: call the tick (or anything that reads uptime) at least once per
//! RTC counter wrap (1024 s here), so counter overflow is tracked. Any periodic wake shorter
//! than that works.

#include "app_timer.h"
#include "app_util_platform.h"
#include "app_memfault_timer.h"
#include "memfault/core/debug_log.h"
#include "memfault/core/platform/core.h"
#include "memfault/metrics/platform/timer.h"

static MemfaultPlatformTimerCallback *s_heartbeat_cb;
static uint64_t s_heartbeat_period_ms;
static uint64_t s_next_heartbeat_ms;

static uint32_t s_last_tick_count;
static uint64_t s_ticks_since_boot;

static uint64_t prv_ticks_to_ms(uint64_t ticks) {
  const uint32_t ticks_per_sec = APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY + 1);
  return (ticks * 1000) / ticks_per_sec;
}

uint64_t memfault_platform_get_time_since_boot_ms(void) {
  uint64_t ticks;
  CRITICAL_REGION_ENTER();
  const uint32_t now = app_timer_cnt_get();
  s_ticks_since_boot += app_timer_cnt_diff_compute(now, s_last_tick_count);
  s_last_tick_count = now;
  ticks = s_ticks_since_boot;
  CRITICAL_REGION_EXIT();
  return prv_ticks_to_ms(ticks);
}

bool memfault_platform_metrics_timer_boot(uint32_t period_sec,
                                          MemfaultPlatformTimerCallback callback) {
  if (s_heartbeat_cb != NULL) {
    MEMFAULT_LOG_ERROR("%s should only be called once", __func__);
    return false;
  }
  s_last_tick_count = app_timer_cnt_get();
  s_heartbeat_period_ms = (uint64_t)period_sec * 1000;
  s_next_heartbeat_ms = memfault_platform_get_time_since_boot_ms() + s_heartbeat_period_ms;
  s_heartbeat_cb = callback;
  return true;
}

void app_memfault_timer_tick(void) {
  const uint64_t now_ms = memfault_platform_get_time_since_boot_ms();
  if (s_heartbeat_cb == NULL || now_ms < s_next_heartbeat_ms) {
    return;
  }
  // Schedule from the nominal time so heartbeats don't drift by the tick granularity
  s_next_heartbeat_ms += s_heartbeat_period_ms;
  if (s_next_heartbeat_ms <= now_ms) {
    s_next_heartbeat_ms = now_ms + s_heartbeat_period_ms;  // missed several, e.g. long sleep
  }
  s_heartbeat_cb();
}
