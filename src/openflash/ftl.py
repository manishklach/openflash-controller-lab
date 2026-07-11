from __future__ import annotations

from collections import defaultdict

from .model import ControllerConfig, PhysicalPage


class OutOfSpaceError(RuntimeError):
    pass


class PageMappingFTL:
    """Deterministic page-mapped FTL with channel and die striping.

    This first milestone models placement and invalidation. Garbage collection is
    accounted for separately by the simulator so scheduler policy can be studied
    without hiding work inside allocation.
    """

    def __init__(self, config: ControllerConfig) -> None:
        self.config = config
        self.mapping: dict[int, PhysicalPage] = {}
        self.invalid_pages: dict[tuple[int, int, int], int] = defaultdict(int)
        self._next_page = 0

    @property
    def capacity_pages(self) -> int:
        return self.config.dies * self.config.blocks_per_die * self.config.pages_per_block

    def lookup(self, lpn: int) -> PhysicalPage | None:
        return self.mapping.get(lpn)

    def write(self, lpn: int) -> PhysicalPage:
        previous = self.mapping.get(lpn)
        if previous is not None:
            self.invalid_pages[(previous.channel, previous.die, previous.block)] += 1
        physical = self._allocate()
        self.mapping[lpn] = physical
        return physical

    def map_read(self, lpn: int) -> PhysicalPage:
        physical = self.lookup(lpn)
        if physical is None:
            # A read of unwritten space is routed deterministically for timing studies.
            stripe = lpn % self.config.dies
            return PhysicalPage(
                channel=stripe % self.config.channels,
                die=stripe // self.config.channels,
                block=(lpn // self.config.dies) % self.config.blocks_per_die,
                page=(lpn // (self.config.dies * self.config.blocks_per_die))
                % self.config.pages_per_block,
            )
        return physical

    def _allocate(self) -> PhysicalPage:
        if self._next_page >= self.capacity_pages:
            raise OutOfSpaceError("FTL capacity exhausted; GC/reclamation is required")
        linear = self._next_page
        self._next_page += 1
        stripe = linear % self.config.dies
        per_die = linear // self.config.dies
        return PhysicalPage(
            channel=stripe % self.config.channels,
            die=stripe // self.config.channels,
            block=per_die // self.config.pages_per_block,
            page=per_die % self.config.pages_per_block,
        )

