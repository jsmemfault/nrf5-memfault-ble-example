#!/usr/bin/env bash
#
# One-time setup: fetches the dependencies into deps/ and generates a DFU signing key in keys/.
# Nothing here is committed (see .gitignore).
#
# Requires: arm-none-eabi-gcc on PATH, curl, unzip, git, python3, and nRF Util (`nrfutil`)
# with the `nrf5sdk-tools` command installed (`nrfutil install nrf5sdk-tools`).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
KEYS="$ROOT/keys"
mkdir -p "$DEPS" "$KEYS"

NRF5_SDK_URL="https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/sdks/nrf5/binaries/nrf5_sdk_17.1.0_ddde560.zip"
NRF5_SDK_SHA256="5bfe38e744c39fd7f30e10077ba12df306ef91f368894795d6a3e7a62dc68061"
NRF5_SDK_DIR="$DEPS/nRF5_SDK_17.1.0_ddde560"

S140_URL="https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/softdevices/s140/s140_nrf52_7.3.0.zip"
S140_SHA256="5a8e57717add4ba02ef45af23536bf1ea2b2786d35b34d0d2b3993a3afa9a857"
S140_DIR="$DEPS/s140_nrf52_7.3.0"

MEMFAULT_SDK_TAG="1.45.0"
MEMFAULT_SDK_DIR="$DEPS/memfault-firmware-sdk"

fetch() {  # url sha256 output
  if [ ! -f "$3" ]; then
    echo "Downloading $(basename "$3")"
    curl -sSfL -o "$3.part" "$1" && mv "$3.part" "$3"
  fi
  echo "$2  $3" | shasum -a 256 -c - >/dev/null || { echo "Checksum mismatch: $3"; exit 1; }
}

# nRF5 SDK 17.1.0
if [ ! -d "$NRF5_SDK_DIR" ]; then
  fetch "$NRF5_SDK_URL" "$NRF5_SDK_SHA256" "$DEPS/nrf5_sdk_17.1.0.zip"
  unzip -q "$DEPS/nrf5_sdk_17.1.0.zip" -d "$DEPS"
fi

# The Memfault override of app_error.h (app/third_party/memfault/sdk_overrides) requires the
# SDK's own copy out of the way. Keep the original for the bootloader, which isn't
# Memfault-integrated.
APP_ERROR_H="$NRF5_SDK_DIR/components/libraries/util/app_error.h"
if [ -f "$APP_ERROR_H" ]; then
  mv "$APP_ERROR_H" "$APP_ERROR_H.orig"
fi
mkdir -p "$ROOT/bootloader/sdk_restore"
cp "$APP_ERROR_H.orig" "$ROOT/bootloader/sdk_restore/app_error.h"

# Point the SDK's GCC makefiles at the arm-none-eabi-gcc on PATH
GCC="$(command -v arm-none-eabi-gcc)" || { echo "arm-none-eabi-gcc not found on PATH"; exit 1; }
cat > "$NRF5_SDK_DIR/components/toolchain/gcc/Makefile.posix" <<EOF
GNU_INSTALL_ROOT ?= $(dirname "$GCC")/
GNU_VERSION ?= $(arm-none-eabi-gcc -dumpversion)
GNU_PREFIX ?= arm-none-eabi
EOF

# S140 7.3.0 (the SDK bundles 7.2.0)
if [ ! -d "$S140_DIR" ]; then
  fetch "$S140_URL" "$S140_SHA256" "$DEPS/s140_nrf52_7.3.0.zip"
  unzip -q "$DEPS/s140_nrf52_7.3.0.zip" -d "$S140_DIR"
fi

# Memfault Firmware SDK
if [ ! -d "$MEMFAULT_SDK_DIR" ]; then
  git clone -q --depth 1 --branch "$MEMFAULT_SDK_TAG" \
    https://github.com/memfault/memfault-firmware-sdk.git "$MEMFAULT_SDK_DIR"
fi

# DFU signing key (local only; never commit it)
if [ ! -f "$KEYS/dfu_private_key.pem" ]; then
  nrfutil nrf5sdk-tools keys generate "$KEYS/dfu_private_key.pem" >/dev/null
fi
nrfutil nrf5sdk-tools keys display --key pk --format code "$KEYS/dfu_private_key.pem" \
  --out_file "$KEYS/dfu_public_key.c" >/dev/null

echo "Setup complete. Next: cd app && make -j8"
