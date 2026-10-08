//! @file
//!
//! Device health metrics for the example.
//!
//! - battery_mv: real measurement of the DK's VDD rail through the SAADC (a battery-powered
//!   product would read its battery divider instead).
//! - battery_soc_pct: simulated discharge/charge cycle feeding Memfault's built-in battery
//!   metrics (MEMFAULT_METRICS_BATTERY_ENABLE).
//! - motion_*_time_ms: simulated accelerometer motion level (idle / low / high),
//!   as time-in-state timer metrics. The state is also in g_app_state, so it's in every
//!   coredump.
//! - sensor_acq_*: simulated sensor acquisition every 10 s. The failure rate depends on the
//!   motion level, and acquisitions are skipped when idle, to show correlated metrics in
//!   Memfault.
//! - ext_flash_used_pct: simulated external SPI flash fill level, reset after each sync.
//!
//! Brownout tracking: see app_power_* below.
//!
//! Power: the only periodic work here is the 10 s application tick, which stands in for
//! a product's existing acquisition wakeups. The Memfault heartbeat rides on the same wakeup
//! (app_memfault_timer.c). Metric updates are RAM writes. Nothing here adds a radio event.

#include <stdlib.h>

#include "app_error.h"
#include "app_timer.h"
#include "app.h"
#include "app_memfault_timer.h"
#include "memfault/components.h"
#include "nrf.h"
#include "nrf_log.h"

#define APP_TICK_PERIOD_MS 10000

// Sensor failure odds by motion level
#define SENSOR_FAILURE_ONE_IN_LOW 25
#define SENSOR_FAILURE_ONE_IN_HIGH 4

APP_TIMER_DEF(s_app_tick_timer);

static uint32_t s_soc_pct = 100;
static bool s_charging;
static uint32_t s_last_vdd_mv;

//
// Motion state (simulated accelerometer)
//

typedef enum {
  kMotion_Idle = 0,
  kMotion_Low,
  kMotion_High,
  kMotion_NumStates,
} eMotionState;

static const char *const s_motion_names[kMotion_NumStates] = { "idle", "low", "high" };
static uint32_t s_motion_ticks_left;

static void prv_motion_timer_start(eMotionState state) {
  switch (state) {
    case kMotion_Idle:
      MEMFAULT_METRIC_TIMER_START(motion_idle_time_ms);
      break;
    case kMotion_Low:
      MEMFAULT_METRIC_TIMER_START(motion_low_time_ms);
      break;
    default:
      MEMFAULT_METRIC_TIMER_START(motion_high_time_ms);
      break;
  }
}

static void prv_motion_timer_stop(eMotionState state) {
  switch (state) {
    case kMotion_Idle:
      MEMFAULT_METRIC_TIMER_STOP(motion_idle_time_ms);
      break;
    case kMotion_Low:
      MEMFAULT_METRIC_TIMER_STOP(motion_low_time_ms);
      break;
    default:
      MEMFAULT_METRIC_TIMER_STOP(motion_high_time_ms);
      break;
  }
}

static void prv_motion_update(void) {
  if (s_motion_ticks_left > 0) {
    s_motion_ticks_left--;
    return;
  }
  // 60% low, 30% high, 10% idle
  const int r = rand() % 10;
  const eMotionState next = (r < 6) ? kMotion_Low : (r < 9) ? kMotion_High : kMotion_Idle;
  const eMotionState prev = (eMotionState)g_app_state.motion_state;
  if (next != prev) {
    prv_motion_timer_stop(prev);
    prv_motion_timer_start(next);
    g_app_state.motion_state = (uint8_t)next;
    NRF_LOG_INFO("Motion: %s -> %s", s_motion_names[prev], s_motion_names[next]);
  }
  s_motion_ticks_left = 2 + (rand() % 6);  // hold the state for 30-80 s
}

const char *app_metrics_motion_state_name(void) {
  return s_motion_names[g_app_state.motion_state % kMotion_NumStates];
}

//
// Power / brownout tracking
//
// nRF52 RESETREAS has no brownout bit: a brownout reads exactly like a power-on reset. So
// keep the last VDD reading in RAM that the startup code and the bootloader don't touch
// (.app_noinit, see app.ld). After a "power-on reset", a still-valid block with a
// low last reading means RAM survived a dip, i.e. a probable brownout rather than a cold power
// up (a real power-off loses RAM, so the magic is gone).
// Classified in app_reboot_tracking.c as Memfault's built-in kMfltRebootReason_BrownOutReset.
//

#define APP_POWER_NOINIT_MAGIC 0x504F5752UL  // "POWR"
#ifndef APP_BROWNOUT_VDD_THRESHOLD_MV
  // DK: VDD sits at ~3.0 V on USB. A product would set this from its battery/regulator dropout.
  #define APP_BROWNOUT_VDD_THRESHOLD_MV 2000
#endif

typedef struct {
  uint32_t magic;
  uint32_t last_vdd_mv;
  uint32_t simulate_por;  // brownout_test: treat the next reset as a POR
  uint32_t checksum;
} sAppPowerNoinit;

static sAppPowerNoinit s_power_noinit __attribute__((section(".app_noinit")));
static bool s_last_boot_brownout;
static uint32_t s_last_boot_vdd_mv;

static uint32_t prv_power_checksum(const sAppPowerNoinit *p) {
  return p->magic ^ p->last_vdd_mv ^ p->simulate_por ^ 0xA5A5A5A5UL;
}

static void prv_power_store(uint32_t vdd_mv, bool simulate_por) {
  s_power_noinit.magic = APP_POWER_NOINIT_MAGIC;
  s_power_noinit.last_vdd_mv = vdd_mv;
  s_power_noinit.simulate_por = simulate_por ? 1 : 0;
  s_power_noinit.checksum = prv_power_checksum(&s_power_noinit);
}

bool app_power_check_reset(uint32_t *reset_reason_reg) {
  const bool valid = (s_power_noinit.magic == APP_POWER_NOINIT_MAGIC) &&
                     (s_power_noinit.checksum == prv_power_checksum(&s_power_noinit));
  if (valid && s_power_noinit.simulate_por) {
    *reset_reason_reg = 0;  // brownout_test: pretend RESETREAS read as power-on
  }
  const bool brownout = valid && (*reset_reason_reg == 0) &&
                        (s_power_noinit.last_vdd_mv < APP_BROWNOUT_VDD_THRESHOLD_MV);
  s_last_boot_brownout = brownout;
  s_last_boot_vdd_mv = valid ? s_power_noinit.last_vdd_mv : 0;
  // Re-arm; the next application tick refreshes the reading
  prv_power_store(valid ? s_power_noinit.last_vdd_mv : 0, false);
  return brownout;
}

void app_power_brownout_test(void) {
  NRF_LOG_WARNING("brownout_test: last VDD 1750 mV, then a POR-style reset");
  NRF_LOG_FINAL_FLUSH();
  prv_power_store(1750, true);
  NVIC_SystemReset();
}

//! Single blocking SAADC conversion of VDD. Gain 1/6, 0.6 V internal reference, 10-bit:
//! full scale = 3.6 V. Takes ~tens of microseconds. SAADC isn't a SoftDevice-restricted
//! peripheral.
static uint32_t prv_read_vdd_mv(void) {
  static volatile int16_t s_result;

  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_10bit;
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD;
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
  NRF_SAADC->CH[0].CONFIG = (SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) |
                            (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
                            (SAADC_CH_CONFIG_TACQ_10us << SAADC_CH_CONFIG_TACQ_Pos);
  NRF_SAADC->RESULT.PTR = (uint32_t)&s_result;
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;

  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED) {
  }
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END) {
  }

  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED) {
  }
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled;

  const int32_t raw = (s_result < 0) ? 0 : s_result;
  return (uint32_t)((raw * 3600) / 1024);
}

static void prv_sensor_acquire(void) {
  if (g_app_state.motion_state == kMotion_Idle) {
    MEMFAULT_METRIC_ADD(sensor_acq_skipped_idle, 1);
    return;
  }

  g_app_state.sensor_acq_attempts++;
  MEMFAULT_METRIC_ADD(sensor_acq_attempts, 1);

  const int one_in = (g_app_state.motion_state == kMotion_High) ? SENSOR_FAILURE_ONE_IN_HIGH
                                                                   : SENSOR_FAILURE_ONE_IN_LOW;
  if ((rand() % one_in) == 0) {
    g_app_state.sensor_acq_failures++;
    MEMFAULT_METRIC_ADD(sensor_acq_failures, 1);
    // Status = motion level * 100 + simulated sensor error code 1-3, e.g. 201 = high + error 1
    const int32_t status = (int32_t)g_app_state.motion_state * 100 + 1 + (rand() % 3);
    MEMFAULT_TRACE_EVENT_WITH_STATUS(sensor_acq_failed, status);
    NRF_LOG_WARNING("Sensor acquisition failed (motion %s)", app_metrics_motion_state_name());
  }
}

static void prv_app_tick_cb(void *p_context) {
  // MEMFAULT: heartbeats ride on this existing wakeup (no Memfault timer of its own)
  app_memfault_timer_tick();

  // Battery reading every tick, kept in noinit RAM for brownout classification
  s_last_vdd_mv = prv_read_vdd_mv();
  prv_power_store(s_last_vdd_mv, false);

  prv_motion_update();
  prv_sensor_acquire();

  // Simulated external flash buffering: each tick adds data until the next sync
  if (g_app_state.ext_flash_used_pct < 100) {
    g_app_state.ext_flash_used_pct++;
  }

  // Simulated battery: discharge 1% per minute (6 ticks), charge back up from 20% at 5%/min.
  // About a 100-minute cycle, so an overnight run shows several charge/discharge cycles.
  static uint32_t s_battery_ticks;
  if (++s_battery_ticks >= 6) {
    s_battery_ticks = 0;
    if (s_charging) {
      s_soc_pct = MIN(100, s_soc_pct + 5);
      s_charging = (s_soc_pct < 100);
    } else if (s_soc_pct > 20) {
      s_soc_pct--;
    } else {
      s_charging = true;
    }
  }
  g_app_state.battery_soc_pct = (uint8_t)s_soc_pct;
}

//! Called by the Memfault SDK just before each heartbeat is serialized
void memfault_metrics_heartbeat_collect_data(void) {
  MEMFAULT_METRIC_SET_UNSIGNED(battery_mv, s_last_vdd_mv ? s_last_vdd_mv : prv_read_vdd_mv());
  MEMFAULT_METRIC_SET_UNSIGNED(ext_flash_used_pct, g_app_state.ext_flash_used_pct);
}

uint32_t app_metrics_battery_soc_pct(void) {
  return s_soc_pct;
}

bool app_metrics_battery_is_charging(void) {
  return s_charging;
}

void app_metrics_on_sync_complete(void) {
  g_app_state.ext_flash_used_pct = 5;
  memfault_metrics_connectivity_record_sync_success();
}

void app_metrics_init(void) {
  srand(NRF_FICR->DEVICEID[0]);

  if (s_last_boot_brownout) {
    // Alongside the BrownOutReset reboot event: the last VDD seen before the reset
    MEMFAULT_TRACE_EVENT_WITH_STATUS(brownout_suspected, (int32_t)s_last_boot_vdd_mv);
    NRF_LOG_WARNING("Previous reset classified as brownout (last VDD %d mV)", s_last_boot_vdd_mv);
  }

  g_app_state.motion_state = kMotion_Low;
  prv_motion_timer_start(kMotion_Low);

  ret_code_t err_code =
    app_timer_create(&s_app_tick_timer, APP_TIMER_MODE_REPEATED, prv_app_tick_cb);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_start(s_app_tick_timer, APP_TIMER_TICKS(APP_TICK_PERIOD_MS), NULL);
  APP_ERROR_CHECK(err_code);
}
