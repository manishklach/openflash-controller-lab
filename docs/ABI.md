# OpenFlash Host/Controller ABI v0.1

The ABI is intentionally small and versioned before the QEMU device and `blk-mq` path are
implemented. The normative C layout is `driver/openflash/openflash_abi.h`.

## Transport

- BAR0 contains capability, lifecycle, admin queue, and doorbell registers.
- Submission and completion rings use DMA-coherent host memory.
- Commands and completions are 64 bytes and little-endian.
- The host writes descriptors, issues a DMA write barrier, then updates the SQ doorbell.
- The controller writes a completion, issues its visibility barrier, then interrupts.
- Completion phase toggles on every ring wrap, preventing stale-entry consumption.

## Lifecycle

1. Host enables PCI memory and bus mastering, maps BAR0, and validates ABI major/minor.
2. Host allocates the admin SQ/CQ, writes addresses and depth, then sets `CTRL_ENABLE`.
3. Device sets `STATUS_READY`; host identifies capacity, limits, and feature bits.
4. Admin commands create I/O queue pairs and associate each with an MSI-X vector.
5. On timeout, the host first aborts; if progress cannot be proved, it resets the device.
6. Reset clears queue ownership. The host must not replay writes unless durability status
   is known; upper layers receive an error when exactly-once execution cannot be proven.

## Durability

Normal write completion means data is accepted under the controller's advertised cache
policy. FUA completion means the data and required FTL metadata are power-loss durable.
Flush orders all earlier writes and makes them durable before completion. Implementations
without protected volatile cache must either persist synchronously or not advertise it.

## Evolution rules

- Major-version mismatch is fatal; minor versions add backwards-compatible features.
- Reserved fields are written as zero and ignored when read.
- Descriptor sizes never change within this ABI major version.
- Feature-dependent opcodes return `INVALID_OPCODE` when not negotiated.
- Every DMA address and length is validated by the controller before use.
