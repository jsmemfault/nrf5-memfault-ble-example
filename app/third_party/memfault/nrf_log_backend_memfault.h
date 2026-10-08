#pragma once

//! @file
//!
//! NRF_LOG backend that copies log lines into the Memfault log buffer.

#ifdef __cplusplus
extern "C" {
#endif

//! Call after NRF_LOG_INIT() and memfault_platform_boot() (which boots the log buffer)
void nrf_log_backend_memfault_init(void);

#ifdef __cplusplus
}
#endif
