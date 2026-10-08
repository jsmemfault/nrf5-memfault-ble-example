//! @file
//!
//! Copyright (c) Memfault, Inc.
//! See LICENSE for details
//!
//! @brief
//! An example implementation of overriding the Memfault logging macros by
//! placing definitions in memfault_platform_log_config.h and adding
//! -DMEMFAULT_PLATFORM_HAS_LOG_CONFIG=1 to the compiler flags

#pragma once

#include <stdio.h>

#include "memfault/core/compiler.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "sdk_config.h"

//! Note: NRF_LOG_FLUSH() needs to be called if NRF_LOG_DEFERRED=1 in order
//! for string formatters to print
// MEMFAULT_LOG_* goes to NRF_LOG only. nrf_log_backend_memfault.c copies every NRF_LOG line
// (ours, the SDK's, Memfault's) into the Memfault log buffer, so saving here too would
// store each line twice.
//
// Deferred NRF_LOG (NRF_LOG_DEFERRED 1) stores *pointers* for "%s" arguments and formats
// later, in NRF_LOG_PROCESS(). The Memfault SDK logs some strings from stack buffers (e.g.
// the build ID), which are gone by then, so the output is garbage. So format once here and
// hand NRF_LOG a copy (NRF_LOG_PUSH copies the string into the log buffer).
#define MEMFAULT_PORT_LOG_LINE_MAX 128
#define _MEMFAULT_PORT_LOG_IMPL(_level, mflt_level, fmt, ...)           \
  do {                                                                  \
    char _mflt_line[MEMFAULT_PORT_LOG_LINE_MAX];                        \
    snprintf(_mflt_line, sizeof(_mflt_line), fmt, ##__VA_ARGS__);       \
    NRF_LOG_##_level("MFLT: %s", NRF_LOG_PUSH(_mflt_line));             \
  } while (0)

#define MEMFAULT_LOG_DEBUG(fmt, ...) \
  _MEMFAULT_PORT_LOG_IMPL(DEBUG, kMemfaultPlatformLogLevel_Debug, fmt, ##__VA_ARGS__)
#define MEMFAULT_LOG_INFO(fmt, ...) \
  _MEMFAULT_PORT_LOG_IMPL(INFO, kMemfaultPlatformLogLevel_Info, fmt, ##__VA_ARGS__)
#define MEMFAULT_LOG_WARN(fmt, ...) \
  _MEMFAULT_PORT_LOG_IMPL(WARNING, kMemfaultPlatformLogLevel_Warning, fmt, ##__VA_ARGS__)
#define MEMFAULT_LOG_ERROR(fmt, ...) \
  _MEMFAULT_PORT_LOG_IMPL(ERROR, kMemfaultPlatformLogLevel_Error, fmt, ##__VA_ARGS__)

//! Note: nrf_delay_ms() gives the console backend time to drain between raw lines.
#define MEMFAULT_LOG_RAW(fmt, ...)                      \
  do {                                                  \
    NRF_LOG_INTERNAL_RAW_INFO(fmt "\n", ##__VA_ARGS__); \
    NRF_LOG_FLUSH();                                    \
    nrf_delay_ms(1);                                    \
  } while (0)
