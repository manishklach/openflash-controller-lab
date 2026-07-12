# Implementation Roadmap

## 1. Controller and FTL model

**Goal:** prove scheduling, placement, and resource sizing before committing to firmware
or RTL.

Deliverables:

- [x] deterministic channel/die timing simulator;
- [x] page mapping, overwrite invalidation, and capacity checks;
- [x] FIFO and starvation-bounded read-priority policies;
- [x] reproducible workloads and machine-readable metrics;
- [ ] latency distributions from a real NAND part;
- [x] live-page GC, erase wear, latency variation, ECC retries, and telemetry;
- [x] JSONL trace replay;
- [ ] over-provisioning, multi-plane/cache-read eligibility, and blktrace conversion.

Acceptance gates:

- Results reproduce exactly for the same config and seed.
- Every resource constraint has a focused unit test.
- Scheduler never loses a request and its starvation bound is testable.
- Model predictions are within 15% of an emulator/FPGA baseline for steady workloads.

## 2. Linux and emulated hardware

**Goal:** establish a correct asynchronous host/controller ABI and remove software-side
serialization.

Deliverables:

- [x] PCI lifecycle, coherent admin rings, BAR programming, MSI-X, and ready handshake;
- [x] versioned register/descriptor specification with endian and alignment rules;
- [x] userspace ABI reference device with read/write/flush/discard and fault injection;
- [x] QEMU 11.0.2 pinned build with BAR0, one DMA queue, MSI-X, and RAM I/O;
- [x] QTests for discovery, ABI/capabilities, enable/reset, and queue register validation;
- [x] synchronous Linux identify command with phase validation and capacity discovery;
- [x] reusable serialized admin submission with CID checks and ring/phase wrap handling;
- [x] interrupt-driven admin CQ draining with timeout/IRQ synchronization;
- [ ] multiple QEMU I/O queues, QTests, persistent backing, and fault injection;
- [x] first admin-created I/O queue with queue-local doorbells and MSI-X vector;
- [x] one-queue `blk-mq` request path with up to 16 DMA scatter-gather segments;
- [ ] scale `blk-mq` queues per CPU/MSI-X vector group;
- [ ] timeout, abort, reset, hot-unplug, suspend/resume, and telemetry paths;
- [ ] discard, flush/FUA, and durability semantics;
- [ ] KUnit tests and a QEMU integration test image.

Acceptance gates:

- No sleeping or global mutex in request submission/completion.
- `fio` verify passes for sequential, random, mixed, discard, flush, and reset tests.
- Queue-depth scaling improves until media/channel saturation, not a driver lock.
- Kernel lockdep, KASAN, DMA API debug, and fault injection are clean.

## 3. Firmware, RTL, and subsystem optimization

**Goal:** move validated policies onto an FPGA/controller and maintain speed under GC,
aging, thermal limits, and power faults.

Deliverables:

- [ ] firmware command processor and scheduler with bounded execution time;
- [ ] ONFI channel engine, timing calibration, multi-plane/cache operations;
- [ ] ECC pipeline and retry policy characterized across retention/endurance corners;
- [ ] persistent FTL journal, checkpoint, recovery, GC, wear leveling, bad-block policy;
- [ ] SRAM/DRAM sizing from measured working sets;
- [ ] power-loss and metadata corruption test rig;
- [ ] production telemetry, secure boot/update, and rollback.

Acceptance gates:

- Sustained sequential throughput reaches at least 85% of the lower of PCIe or aggregate
  channel payload bandwidth.
- Random-read p99 at the agreed queue depth remains within 2x idle-media p99 during
  controlled background GC.
- Write amplification stays below the workload-specific budget at steady state.
- Recovery reconstructs a consistent mapping after every injected power-cut point.
- Uncorrectable-error and retirement behavior matches the product reliability model.

## First 12 weeks

| Weeks | Outcome |
| --- | --- |
| 1-2 | Calibrate NAND timing distributions; add trace and GC models. |
| 3-4 | Freeze ABI v0.1 and build the minimal QEMU PCI device. |
| 5-6 | Complete `blk-mq` queues, DMA, MSI-X, and fio smoke tests. |
| 7-8 | Add timeout/reset/fault injection and durability tests. |
| 9-10 | Profile locks, CPU/request, queue scaling, and scheduler variants. |
| 11-12 | Freeze FPGA requirements and implement the first channel-engine testbench. |
