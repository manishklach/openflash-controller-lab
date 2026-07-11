# QEMU OpenFlash Device

`hw/block/openflash.c` is a QEMU PCI device implementing ABI v0.1 with BAR0 registers,
DMA-backed submission/completion rings, MSI-X notification, and a sparse-purpose 64 MiB
RAM backing store. It currently uses the admin queue for identify and data-path smoke
testing; multiple I/O queue creation and persistent image backing are the next increment.

## Integrate into a QEMU checkout

1. Copy `openflash.c` to `hw/block/openflash.c`.
2. Append `Kconfig.openflash` to `hw/block/Kconfig` without its filename wrapper, or add
   `source hw/block/Kconfig.openflash` from the parent Kconfig as appropriate for the QEMU
   revision.
3. Append the line in `meson.build.fragment` to `hw/block/meson.build`.
4. Configure and build QEMU for an x86_64 softmmu target.

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

## Required before persistent-data testing

- QTest register, DMA, malformed-command, queue-wrap, and reset cases.
- Separate admin and per-CPU I/O queue creation/deletion.
- File-backed storage with flush/FUA and migration semantics.
- DMA error handling, interrupt masking/coalescing, and explicit fault injection.
- QEMU coding-style and sanitizer runs against the pinned upstream revision.
