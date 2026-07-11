# Linux Driver Scaffold

This directory defines the lifecycle and data structures for a future managed NAND PCIe
controller. It intentionally does not register a block disk yet: doing so before the ABI,
DMA rings, interrupts, timeout, and reset behavior exist would expose an unsafe fake data
path.

## Intended implementation order

1. Extend the frozen ABI v0.1 in `openflash_abi.h` only through its documented versioning
   rules; add compile-time layout checks when wiring it into a Linux build.
2. Allocate coherent submission/completion rings, program DMA addresses, and negotiate
   queue count/depth through the admin queue.
3. Allocate MSI-X vectors and map each I/O queue to a completion handler/NAPI-like poll.
4. Add a `blk_mq_tag_set`; translate requests without allocation or sleeping in `queue_rq`.
5. Implement flush/FUA/discard, timeout/abort, controller reset, and in-flight replay rules.
6. Add debugfs/sysfs telemetry only after stable counters are part of the ABI.

Build it inside a configured Linux tree by adding the directory to the relevant Kconfig
and Makefile. The scaffold compiles only after the provisional register contract is
replaced and its TODO paths are implemented.

## Locking rules

- One submission lock per queue until lockless producer ownership is proven.
- Completion processing owns its queue and never takes a controller-wide mutex.
- Admin/reset serialization is outside the I/O fast path.
- Queue teardown first prevents new submissions, then drains or fails in-flight work.

## Correctness gates

Use sparse, smatch, Coccinelle, lockdep, KASAN, DMA API debug, and fault injection. Test
invalid descriptors, stale completions, queue wrap, interrupt loss, DMA errors, surprise
remove, reset races, and power-state transitions before exposing persistent data.
