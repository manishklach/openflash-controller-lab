# Contributing to OpenFlash

OpenFlash welcomes changes that improve correctness, measurement quality, or reproducibility.
This project is experimental storage software: do not claim durability, silicon performance, or
hardware compatibility that the tests cannot demonstrate.

## Before opening a change

1. Keep the host/controller ABI synchronized across `driver/openflash/openflash_abi.h`, QEMU,
   and `src/openflash/abi.py`.
2. Add focused tests for every queue, DMA, reset, persistence, or timing-model behavior change.
3. Run `python -m ruff check src tests` and `python -m pytest -q`.
4. For Linux driver changes, build the module against a configured kernel tree and run
   `scripts/checkpatch.pl --no-tree --file` on changed driver files.
5. For QEMU changes, run `bash scripts/build-qemu-device.sh` or rely on the pinned CI job.

## ABI rules

- Preserve 64-byte command and completion layouts within an ABI major version.
- Use a minor version increment for backwards-compatible additions.
- Write reserved fields as zero and validate all DMA address/length combinations.
- Add cross-implementation tests whenever the ABI changes.

## Measurement rules

Report the configuration, seed, workload, queue depth, geometry, timing profile, and whether
data verification, FUA, flush, fault injection, or recovery behavior were enabled. A throughput
gain that violates durability or tail-latency gates is a regression.

## Submission scope

Keep changes narrowly reviewable. Update the relevant documentation and roadmap gate with code,
and do not include generated kernel objects or downloaded QEMU source trees.
