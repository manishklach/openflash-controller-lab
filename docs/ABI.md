# OpenFlash Host/Controller ABI v0.2

The ABI is intentionally small and versioned before the QEMU device and `blk-mq` path are
implemented. The normative C layout is `driver/openflash/openflash_abi.h`.

## Transport

- BAR0 contains capability, lifecycle, admin queue, and doorbell registers.
- Submission and completion rings use DMA-coherent host memory.
- Commands and completions are 64 bytes and little-endian. ABI v0.2 adds an optional
  scatter-gather list (SGL) with fixed 16-byte address/length entries.
- The host writes descriptors, issues a DMA write barrier, then updates the SQ doorbell.
- The controller writes a completion, issues its visibility barrier, then interrupts.
- Completion phase toggles on every ring wrap, preventing stale-entry consumption.

## Lifecycle

1. Host enables PCI memory and bus mastering, maps BAR0, and validates ABI major/minor.
2. Host allocates the admin SQ/CQ, writes addresses and depth, then sets `CTRL_ENABLE`.
3. Device sets `STATUS_READY`; host identifies capacity, limits, and feature bits.
4. Admin commands create I/O queue pairs and associate each with an MSI-X vector.
5. On timeout, the host fails ambiguous commands and resets the device. A future ABI
   revision may add command abort when completion ownership can be proved.
6. Reset clears queue ownership. The host must not replay writes unless durability status
   is known; upper layers receive an error when exactly-once execution cannot be proven.

## Durability

Normal write completion means data is accepted under the controller's advertised cache
policy. FUA completion means the data and required FTL metadata are power-loss durable.
Flush orders all earlier writes and makes them durable before completion. Implementations
without protected volatile cache must either persist synchronously or not advertise it.

## Data descriptors

Read and write commands without `CMD_F_SGL` use `data_addr` as one contiguous DMA buffer.
With `CMD_F_SGL`, `data_addr` points to an SGL and `control[15:0]` is the number of entries.
Each entry contains a 64-bit DMA address and 32-bit byte length. The controller rejects zero
length entries, more than 16 entries, and lists whose aggregate length is not exactly
`nblocks * 4096`. This leaves command and completion layouts unchanged while enabling
multi-page Linux block requests.

## Evolution rules

- Major-version mismatch is fatal; minor versions add backwards-compatible features.
- Reserved fields are written as zero and ignored when read.
- Descriptor sizes never change within this ABI major version.
- Feature-dependent opcodes return `INVALID_OPCODE` when not negotiated.
- Every DMA address and length is validated by the controller before use.
