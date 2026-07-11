# QEMU OpenFlash Device

`hw/block/openflash.c` is a QEMU PCI device implementing ABI v0.1 with BAR0 registers,
DMA-backed submission/completion rings, MSI-X notification, and a sparse-purpose 64 MiB
RAM backing store. It currently uses the admin queue for identify and data-path smoke
testing; multiple I/O queue creation and persistent image backing are the next increment.

The reproducible build is pinned to QEMU 11.0.2 and verifies the official tarball SHA-256
before integration. GitHub Actions runs this build for every change.

```bash
scripts/build-qemu-device.sh
```

Set `OPENFLASH_QEMU_WORKDIR` to move the downloaded source/build cache from its default
location under `/tmp`.

## Integrate into a QEMU checkout

1. Use QEMU 11.0.2, or expect to adapt internal APIs for another revision.
2. Copy `openflash.c` to `hw/block/openflash.c`.
3. Append `Kconfig.openflash` to `hw/block/Kconfig` without its filename wrapper, or add
   `source hw/block/Kconfig.openflash` from the parent Kconfig as appropriate for the QEMU
   revision.
4. Append the line in `meson.build.fragment` to `hw/block/meson.build`.
5. Configure and build QEMU for an x86_64 softmmu target.

```bash
mkdir build && cd build
../configure --target-list=x86_64-softmmu --enable-debug
ninja qemu-system-x86_64
./qemu-system-x86_64 -device openflash,capacity=256M -machine q35 ...
```

QEMU internal APIs evolve. This source is an in-tree integration artifact rather than an
out-of-tree plugin ABI; compile it against the chosen QEMU revision and keep API updates
in an isolated compatibility commit.

## Implemented behavior

- ABI, capability, control, status, queue address, queue size, and doorbell registers.
- 64-byte command fetch and completion DMA with phase-bit wraparound.
- Identify, read, write, flush, and discard commands.
- LBA and command-field validation.
- One MSI-X vector and resettable queue state.
- QTests for PCI discovery, ABI/capabilities, lifecycle/reset, queue registers, and invalid
  queue depth handling.

## Required before persistent-data testing

- QTest DMA command execution, malformed-command, queue-wrap, and MSI-X delivery cases.
- Separate admin and per-CPU I/O queue creation/deletion.
- File-backed storage with flush/FUA and migration semantics.
- DMA error handling, interrupt masking/coalescing, and explicit fault injection.
- QEMU coding-style and sanitizer runs against the pinned upstream revision.
