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
- [ ] GC, over-provisioning, wear, multi-plane, cache-read, and ECC-retry models;
- [ ] trace replay for fio/blktrace workloads.

Acceptance gates:

- Results reproduce exactly for the same config and seed.
- Every resource constraint has a focused unit test.
- Scheduler never loses a request and its starvation bound is testable.
- Model predictions are within 15% of an emulator/FPGA baseline for steady workloads.

## 2. Linux and emulated hardware

**Goal:** establish a correct asynchronous host/controller ABI and remove software-side
serialization.

Deliverables:

- [x] PCI lifecycle and queue data structures scaffold;
- [ ] versioned register/descriptor specification with endian and alignment rules;
- [ ] QEMU PCI model with admin queue, I/O queues, MSI-X, DMA, and fault injection;
- [ ] `blk-mq` request path with one queue per CPU/MSI-X vector group;
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

