# Memfault on nRF5 SDK: BLE example for nRF52840-DK

A bare-metal BLE peripheral on **nRF5 SDK 17.1.0 + S140 7.3.0** with the Nordic secure BLE
bootloader, integrated with the **Memfault Firmware SDK 1.45.0**. The device has no internet
connection: Memfault data travels over a custom GATT service to a gateway (a Python script
standing in for a phone app), which uploads it to Memfault.

```
nRF52840-DK ──BLE──▶ gateway/gateway.py ──HTTPS──▶ Memfault
  Diagnostics Control Point            POST /api/v0/chunks/<serial>
```

Crashes, reboot reasons, logs, battery voltage and BLE/transfer metrics are real. Sensor, motion, battery
state-of-charge and flash-fill metrics are simulated (see [Metrics](#metrics)), as are the
console-triggered `sec_fail`, `sd_assert` and `brownout_test` events.

## Requirements

- nRF52840-DK (PCA10056)
- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) on
  `PATH` (tested with 14.2.1)
- [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) with the `device`
  and `nrf5sdk-tools` commands, and `mergehex` (nRF Command Line Tools)
- `curl`, `unzip`, `git`; Python 3 with [bleak](https://github.com/hbldh/bleak) for the gateway
- [Memfault CLI](https://pypi.org/project/memfault-cli/) and a Memfault project

## Quick start

```bash
./scripts/setup.sh        # fetch SDKs into deps/ (SHA-256 checked), make a DFU key in keys/
cd app
make -j8
make flash_all            # erase + SoftDevice + bootloader + app + bootloader settings

# Memfault needs each build's ELF to decode its data
export MEMFAULT_ORG_TOKEN=... MEMFAULT_ORG=... MEMFAULT_PROJECT=...
make upload_symbols

cd ../gateway && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
MEMFAULT_PROJECT_KEY=... .venv/bin/python gateway.py --interval 120
```

New custom metrics may need to be enabled as timeseries in the Memfault project's metric
settings before they chart.

| Make variable / target | Purpose |
| --- | --- |
| `APP_FW_VERSION` (`1.0.0`) | Version reported to Memfault and used for DFU |
| `HEARTBEAT_SECS` (`300`) | Heartbeat interval (Memfault's default is 3600) |
| `DIAG_REQUIRE_BOND` (`1`) | Require Just Works pairing for the diagnostics characteristic. Use `0` for hosts that don't complete pairing unattended (with macOS + bleak, pairing timed out) |
| `NRF_SN` | J-Link serial (`nrfutil device list`), needed with more than one board attached |
| `CONSOLE_PORT` | Serial port for `make console`; defaults to the macOS name for `NRF_SN` (Linux: usually `/dev/ttyACM0`) |
| `flash` / `flash_all` / `flash_nobl` | App + bootloader settings / everything / SoftDevice + app without bootloader |
| `upload_symbols`, `dfu_pkg`, `memfault_release` | Upload the ELF / build the signed DFU zip / upload it as a Memfault OTA payload and deploy (needs the Manager role) |

## Diagnostics Control Point

Service `7a0c0001-e3c5-218a-4f9c-64a7521d8e3b`, characteristic
`7a0c0002-e3c5-218a-4f9c-64a7521d8e3b` (write + notify). The gateway reads the serial from the
Device Information Service, writes `0x01` (status) and `0x02` (start sync), and POSTs each data
frame's payload, an opaque Memfault chunk of up to 240 bytes at MTU 247, to
`https://chunks.memfault.com/api/v0/chunks/<serial>`. Full opcode table:
[`app/src/diag_service.c`](app/src/diag_service.c).

If the link drops mid-sync, `memfault_packetizer_abort()` makes the interrupted message re-send
next time; the gateway uploads whatever it received before the failure.

## Metrics

| Metric | Source | Behaviour |
| --- | --- | --- |
| `battery_mv` | Real: VDD via SAADC, every 10 s | ~3000 mV on USB |
| `battery_soc_pct` (Memfault built-in) | Simulated | Falls 1%/min to 20%, rises 5%/min to 100% |
| `motion_idle_time_ms`, `motion_low_time_ms`, `motion_high_time_ms` | Simulated | Level re-picked every 30–80 s: 60% low, 30% high, 10% idle |
| `sensor_acq_attempts`, `sensor_acq_failures`, `sensor_acq_skipped_idle` | Simulated | One acquisition per 10 s; fails 1 in 25 at low motion, 1 in 4 at high; skipped when idle |
| `ext_flash_used_pct` | Simulated | +1% per 10 s, reset to 5% after each sync |
| `ble_connect_count`, `ble_pairing_failures` | Real | Connections; Peer Manager security failures |
| `diag_sync_count`, `diag_tx_bytes` | Real | Syncs started; bytes sent |
| `flash_double_write_count` | Real | Writes blocked by the flash guard |

Sessions: `diag_sync` (chunks and bytes per sync) and Memfault's `bt_conn` (per connection, with
the disconnect reason). Memfault's connectivity, sync-success and uptime metrics are enabled too.

## Failure scenarios

Buttons on the DK, or commands on the console (`make console`, 115200 baud; Tab lists commands,
`<cmd> -h` shows help).

| Trigger | Result in Memfault |
| --- | --- |
| Button 1 / `flash_dw` | First press writes a record; the next one targets non-erased flash: trace `flash_double_write` + log upload |
| `flash_dw fatal` | Same, but the double write asserts: coredump at the flash guard |
| Button 2 / `hardfault` | HardFault coredump (call through a corrupted function pointer) |
| Button 3 / `hang` | Software Watchdog coredump; the backtrace shows the hung loop |
| Button 4 / `forget_bonds` | Deletes bonds; a phone reconnecting with its old keys then fails security (trace `ble_conn_sec_failed`) |
| `sec_fail` | The same trace event, simulated |
| `sd_assert` | Calls the SoftDevice fault handler: reboot `SoftDeviceAssert` + coredump |
| `brownout_test` | Reboot `BrownOutReset` + trace `brownout_suspected` (last mV) |

`export` prints pending chunks as text, which can be pasted into the Memfault project's Chunks
Debug view when BLE isn't available.

## Integration notes

What an nRF5 SDK + SoftDevice project needs beyond the upstream
[`ports/nrf5_sdk`](https://github.com/memfault/memfault-firmware-sdk/tree/master/ports/nrf5_sdk).
(The upstream `examples/nrf5` targets SDK 15.2 and S140 6.x, with the app at `0x26000`, and
doesn't enable the SoftDevice.)

**Build**
- Memfault's `app_error.h` override routes `APP_ERROR_CHECK` to a Memfault assert. It requires
  removing the SDK's `components/libraries/util/app_error.h`, which affects every project sharing
  that SDK tree; `setup.sh` renames it, and the bootloader gets the original via
  `bootloader/sdk_restore/`.
- Remove the SDK's `hardfault_implementation.c`; Memfault provides the fault handlers. Other build
  flags (`-std=gnu11`, GNU build ID, `-Wno-array-bounds` for GCC 12+) are commented in
  `app/Makefile`.

**SoftDevice**
- SoftDevice asserts and memory-access faults reach `app_error_fault_handler()`, which the SDK
  defines as weak, logging and resetting. This example overrides it to capture a coredump.
- Priorities 0, 1 and 4 are reserved for the SoftDevice. The upstream software watchdog uses 0;
  this example's copy uses 2.
- Memfault runs in thread mode and in interrupts, and its default `memfault_lock()` is empty; this
  example implements it with `app_util_critical_region_enter()`.

**Power**
- For heartbeat intervals longer than the RTC counter period (1024 s with the SDK's default
  app_timer settings), the upstream metrics timer wakes every 60 s. `app_memfault_timer.c` drives
  the heartbeat from an existing application wakeup instead.

**Logs**
- `nrf_log_backend_memfault.c` copies `NRF_LOG` lines (INFO and above) into the Memfault log
  buffer, so existing logging appears in coredumps.
- Deferred NRF_LOG stores `%s` arguments as pointers, so `MEMFAULT_LOG_*` is formatted first, and
  the fault handler flushes NRF_LOG before writing the coredump.

**Reset reasons**
- nRF52 `RESETREAS` has no brownout flag: a brownout reads as power-on. The last VDD reading is
  kept in RAM that survives reset, and a power-on after a low reading is reported as
  `BrownOutReset`. This is a heuristic; RAM isn't guaranteed to survive a brownout.
- CPU lockup is reported as `Lockup` (the upstream port reports `SoftwareReset`).

**Memory layout**

| Address | Size | Use |
| --- | --- | --- |
| `0x27000` | | Application (S140 7.x app start) |
| `0xF0000` | 4 KB | Scratch page for the flash double-write demo |
| `0xF1000` | 16 KB | Memfault coredump storage |
| `0xF5000` | 12 KB | FDS bonds (placed below the bootloader; at the top of flash without one) |
| `0xF8000` | 24 KB | Secure bootloader |
| `0x2003BF00` | 256 B | No-init RAM (brownout tracking), below the bootloader's 16 KB stack |

- The bootloader preserves 32 KB below itself across DFU (`NRF_DFU_APP_DATA_AREA_SIZE`; SDK
  default 12 KB, FDS only), so stored coredumps survive updates.
- The bootloader checks the app against its settings page at boot; `make flash` rewrites both.
- A coredump holds up to 4 KB of the active stack, the application state struct and the log
  buffer.

**Footprint:** Memfault SDK + port, excluding the demo console component: about 19 KiB flash and
1 KiB RAM, plus this example's 2 KiB event and 2 KiB log buffers (map file, `-Os`, GCC 14.2.1).
The whole application is about 94 KiB.

## Limitations

- Events wait in RAM until the next sync and are lost if the device resets first; a product that
  syncs rarely should enable Memfault's non-volatile event storage.
- The gateway doesn't fetch OTA releases, and the DFU service is the unbonded buttonless variant.
  The DFU path (phone to bootloader to new app) has not been exercised end to end on hardware.
- No RTC: Memfault timestamps events on arrival.

## Third-party code

Files derived from the nRF5 SDK (`sdk_config.h` files, Makefiles, linker script, `app_error.h`
override) keep Nordic Semiconductor's license header. Files derived from the Memfault Firmware SDK
(`app_reboot_tracking.c`, `nrf5_software_watchdog_sd.c`, `memfault_platform_log_config.h`) are
under Memfault's license, [`app/third_party/memfault/LICENSE`](app/third_party/memfault/LICENSE).
The SDKs themselves are downloaded by `setup.sh`, not redistributed.
