import re
from pathlib import Path

from openflash.abi import ABI_VERSION, COMMAND_SIZE, COMPLETION_SIZE


ROOT = Path(__file__).parents[1]


def macro_value(source: str, name: str) -> int:
    match = re.search(rf"^#define\s+{name}\s+(0x[0-9a-fA-F]+|\d+)", source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing ABI macro {name}")
    return int(match.group(1), 0)


def test_python_kernel_and_qemu_abi_versions_match() -> None:
    qemu = (ROOT / "qemu/hw/block/openflash.c").read_text()
    kernel = (ROOT / "driver/openflash/openflash_abi.h").read_text()
    assert macro_value(qemu, "OF_ABI_VERSION") == ABI_VERSION
    assert macro_value(kernel, "OPENFLASH_ABI_VERSION_MINOR") == ABI_VERSION


def test_all_abi_implementations_require_64_byte_descriptors() -> None:
    qemu = (ROOT / "qemu/hw/block/openflash.c").read_text()
    kernel = (ROOT / "driver/openflash/openflash_abi.h").read_text()
    assert f"sizeof(OpenFlashCommand) != {COMMAND_SIZE}" in qemu
    assert f"Exactly {COMMAND_SIZE} bytes" in kernel
    assert f"sizeof(OpenFlashCompletion) != {COMPLETION_SIZE}" in qemu
