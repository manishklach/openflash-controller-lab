from __future__ import annotations

from collections.abc import Callable, MutableMapping
from dataclasses import replace

from .abi import BLOCK_SIZE, Command, Completion
from .device import ReferenceDevice


class QueueFullError(RuntimeError):
    pass


class QueueEngine:
    """Reference SQ/CQ transport with phase-tagged completion wraparound."""

    def __init__(
        self,
        device: ReferenceDevice,
        depth: int = 128,
        interrupt: Callable[[int], None] | None = None,
    ) -> None:
        if depth < 2 or depth & (depth - 1):
            raise ValueError("queue depth must be a power of two and at least two")
        self.device = device
        self.depth = depth
        self.interrupt = interrupt
        self._sq: list[Command | None] = [None] * depth
        self._cq: list[Completion | None] = [None] * depth
        self._sq_tail = 0
        self._sq_head = 0
        self._cq_tail = 0
        self._cq_head = 0
        self._sq_count = 0
        self._cq_count = 0
        self._device_phase = 1
        self._host_phase = 1
        self.interrupts = 0

    @property
    def pending(self) -> int:
        return self._sq_count

    @property
    def completions(self) -> int:
        return self._cq_count

    def submit(self, command: Command) -> int:
        if self._sq_count == self.depth:
            raise QueueFullError("submission queue is full")
        slot = self._sq_tail
        self._sq[slot] = command
        self._sq_tail = (self._sq_tail + 1) & (self.depth - 1)
        self._sq_count += 1
        return slot

    def ring_submission_doorbell(
        self, dma: MutableMapping[int, bytearray], budget: int | None = None
    ) -> int:
        limit = self.depth if budget is None else max(0, budget)
        processed = 0
        while self._sq_count and self._cq_count < self.depth and processed < limit:
            command = self._sq[self._sq_head]
            if command is None:
                raise RuntimeError("submission ring ownership corruption")
            completion = self.device.execute(command, dma)
            completion = replace(
                completion,
                sq_head=(self._sq_head + 1) & (self.depth - 1),
                flags=(completion.flags & ~1) | self._device_phase,
            )
            self._cq[self._cq_tail] = completion
            self._sq[self._sq_head] = None
            self._sq_head = (self._sq_head + 1) & (self.depth - 1)
            self._sq_count -= 1
            self._cq_tail = (self._cq_tail + 1) & (self.depth - 1)
            self._cq_count += 1
            processed += 1
            if self._cq_tail == 0:
                self._device_phase ^= 1
        if processed:
            self.interrupts += 1
            if self.interrupt is not None:
                self.interrupt(processed)
        return processed

    def consume_completion(self) -> Completion | None:
        if not self._cq_count:
            return None
        completion = self._cq[self._cq_head]
        if completion is None or (completion.flags & 1) != self._host_phase:
            return None
        self._cq[self._cq_head] = None
        self._cq_head = (self._cq_head + 1) & (self.depth - 1)
        self._cq_count -= 1
        if self._cq_head == 0:
            self._host_phase ^= 1
        return completion

    def reset(self) -> int:
        aborted = self._sq_count
        self._sq = [None] * self.depth
        self._cq = [None] * self.depth
        self._sq_tail = self._sq_head = 0
        self._cq_tail = self._cq_head = 0
        self._sq_count = self._cq_count = 0
        self._device_phase = self._host_phase = 1
        return aborted


def allocate_dma(blocks: int = 1) -> bytearray:
    if blocks <= 0:
        raise ValueError("DMA allocation must contain at least one block")
    return bytearray(blocks * BLOCK_SIZE)
