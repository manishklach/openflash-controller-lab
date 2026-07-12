# Controller Architecture

## Performance thesis

NAND media latency cannot be removed, but it can be hidden by keeping independent dies
busy and preventing host/software serialization. The controller therefore needs enough
outstanding work, cheap placement decisions, separate channel and die resource tracking,
and scheduling that protects latency-sensitive reads without starving writes.

## Data path

1. The Linux driver translates block requests into fixed-size queue descriptors.
2. Firmware validates each descriptor and resolves logical pages through the FTL.
3. The scheduler selects ready work using operation class, age, channel, and die state.
4. Channel engines perform command/address/data transfer while dies execute internally.
5. ECC processes read data and reports corrected-bit counts or uncorrectable errors.
6. Completion entries return status, latency, ECC, and media-health metadata.

## Initial geometry and targets

| Dimension | Baseline | Design target |
| --- | ---: | ---: |
| Channels | 8 | Configurable 4-16 |
| Dies/channel | 4 | Configurable 1-8 |
| Outstanding commands | 128 | 256+ without global locks |
| Page size | 16 KiB | ONFI-discovered |
| Read latency | 65 us | Model from measured NAND |
| Program latency | 550 us | Model distribution, not only mean |
| Erase latency | 3.5 ms | Background and preemptible policy |

The default constants are hypotheses. The repository now includes a named Micron SLC timing
profile using published 35 us read, 350 us program, and 1.5 ms erase values; see
[`CALIBRATION.md`](CALIBRATION.md). Geometry, error distributions, and workload-dependent
latency still require a selected-part datasheet and measurements before RTL sizing.

## Scheduling contract

The reference scheduler supports FIFO and read-priority. Read-priority emits at most a
configured read burst while writes are waiting. The production policy should add:

- per-channel ready queues so one busy die does not block unrelated dies;
- request aging and deadlines for hard starvation bounds;
- multi-plane and cache-operation eligibility checks;
- GC throttling tied to free-block watermarks;
- QoS classes and latency histograms exported to the host.

## FTL contract

The simulator now supports an atomic checkpoint journal and recovery of mapping/allocation
state. The production controller FTL still needs a persistent mapping journal with replay,
over-provisioning, victim selection,
wear leveling, hot/cold separation, TRIM, bad-block retirement, and atomic metadata
updates. No host completion may be reported before the relevant durability contract is
satisfied.

## Hardware partition

- **PCIe front end:** admin queue, I/O queues, MSI-X, DMA protection, reset state.
- **Command processor:** descriptor validation, dispatch, timeout/error state machine.
- **FTL engine:** mapping cache, allocator, metadata journal, GC and wear statistics.
- **NAND scheduler:** operation-aware queues and channel/die dependency tracking.
- **Channel engines:** ONFI command sequencing, timing modes, calibration, and retries.
- **ECC engine:** LDPC/BCH pipeline, soft-decision retry, corrected-bit telemetry.
- **Memory:** SRAM for hot rings/metadata; DRAM for mapping and write coalescing.

## Measurement rules

Every optimization is compared against a pinned configuration and seed. Report bandwidth,
IOPS, p50/p95/p99/p99.9 latency, write amplification, GC duty cycle, channel utilization,
ECC retries, CPU/request, and recovery time. A throughput win that violates the selected
tail-latency or durability gate is a regression.
