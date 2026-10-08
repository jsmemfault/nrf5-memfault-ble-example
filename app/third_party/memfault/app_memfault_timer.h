#pragma once

//! @file
//!
//! Drives Memfault heartbeats from an existing application wakeup (no dedicated timer).

#ifdef __cplusplus
extern "C" {
#endif

//! Call from an existing periodic wakeup, at least once per RTC counter wrap (1024 s at
//! APP_TIMER_CONFIG_RTC_FREQUENCY 1).
//! Fires the Memfault heartbeat when its interval has elapsed. The heartbeat period's
//! resolution is the caller's tick period.
void app_memfault_timer_tick(void);

#ifdef __cplusplus
}
#endif
