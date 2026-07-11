#!/usr/bin/env bash
set -euo pipefail

readonly QEMU_VERSION="11.0.2"
readonly QEMU_SHA256="3745f6ea88e2e87fe0dc838b2b1d4e0a770bf48e01a1d5a186842a1fff76ccf5"
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly WORK_ROOT="${OPENFLASH_QEMU_WORKDIR:-/tmp/openflash-qemu-build}"
readonly ARCHIVE="${WORK_ROOT}/qemu-${QEMU_VERSION}.tar.xz"
readonly SOURCE="${WORK_ROOT}/qemu-${QEMU_VERSION}"

mkdir -p "${WORK_ROOT}"
if [[ ! -f "${ARCHIVE}" ]]; then
    curl --fail --location --retry 3 \
        "https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz" \
        --output "${ARCHIVE}"
fi
echo "${QEMU_SHA256}  ${ARCHIVE}" | sha256sum --check --status

rm -rf "${SOURCE}"
tar -xf "${ARCHIVE}" -C "${WORK_ROOT}"
cp "${REPO_ROOT}/qemu/hw/block/openflash.c" "${SOURCE}/hw/block/openflash.c"
cp "${REPO_ROOT}/qemu/tests/qtest/openflash-test.c" \
    "${SOURCE}/tests/qtest/openflash-test.c"
cat "${REPO_ROOT}/qemu/hw/block/Kconfig.openflash" >> "${SOURCE}/hw/block/Kconfig"
cat "${REPO_ROOT}/qemu/hw/block/meson.build.fragment" >> "${SOURCE}/hw/block/meson.build"
sed -i "/^qtest_executables = {}/i qtests_i386 += ['openflash-test']" \
    "${SOURCE}/tests/qtest/meson.build"

cd "${SOURCE}"
./configure \
    --target-list=x86_64-softmmu \
    --disable-docs \
    --disable-werror
ninja -C build qemu-system-x86_64
build/pyvenv/bin/meson test -C build --print-errorlogs \
    qtest-x86_64/openflash-test
build/qemu-system-x86_64 --version
build/qemu-system-x86_64 -device help | grep -F 'openflash'
