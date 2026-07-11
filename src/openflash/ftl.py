from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass

from .model import ControllerConfig, PhysicalPage


class OutOfSpaceError(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class GarbageCollectionResult:
    victim: tuple[int, int, int]
    pages_moved: int
    pages_reclaimed: int
    erase_count: int


class PageMappingFTL:
    """Page-mapped FTL with striped placement and live-page GC relocation."""

    def __init__(self, config: ControllerConfig) -> None:
        self.config = config
        self.mapping: dict[int, PhysicalPage] = {}
        self.invalid_pages: dict[tuple[int, int, int], int] = defaultdict(int)
        self.valid_lpns: dict[tuple[int, int, int], set[int]] = defaultdict(set)
        self.erase_counts: dict[tuple[int, int, int], int] = defaultdict(int)
        self._next_page = 0
        self._reclaimed: deque[PhysicalPage] = deque()

    @property
    def capacity_pages(self) -> int:
        return self.config.dies * self.config.blocks_per_die * self.config.pages_per_block

    @property
    def free_pages(self) -> int:
        return self.capacity_pages - self._next_page + len(self._reclaimed)

    @property
    def needs_gc(self) -> bool:
        threshold = self.config.gc_low_watermark_blocks * self.config.pages_per_block
        return self.free_pages <= threshold and bool(self.invalid_pages)

    @property
    def max_observed_erase_count(self) -> int:
        return max(self.erase_counts.values(), default=0)

    def lookup(self, lpn: int) -> PhysicalPage | None:
        return self.mapping.get(lpn)

    def write(self, lpn: int) -> PhysicalPage:
        previous = self.mapping.get(lpn)
        if previous is not None:
            block = self._block_key(previous)
            self.valid_lpns[block].discard(lpn)
            self.invalid_pages[block] += 1
        physical = self._allocate()
        self.mapping[lpn] = physical
        self.valid_lpns[self._block_key(physical)].add(lpn)
        return physical

    def map_read(self, lpn: int) -> PhysicalPage:
        physical = self.lookup(lpn)
        if physical is not None:
            return physical
        stripe = lpn % self.config.dies
        return PhysicalPage(
            channel=stripe % self.config.channels,
            die=stripe // self.config.channels,
            block=(lpn // self.config.dies) % self.config.blocks_per_die,
            page=(lpn // (self.config.dies * self.config.blocks_per_die))
            % self.config.pages_per_block,
        )

    def garbage_collect(self) -> GarbageCollectionResult:
        candidates = [block for block, invalid in self.invalid_pages.items() if invalid > 0]
        if not candidates:
            raise OutOfSpaceError("no reclaimable block is available")
        victim = max(
            candidates,
            key=lambda block: (
                self.invalid_pages[block],
                -self.erase_counts[block],
                tuple(-part for part in block),
            ),
        )
        live_lpns = sorted(self.valid_lpns[victim])
        if self.capacity_pages - self._next_page + len(self._reclaimed) < len(live_lpns):
            raise OutOfSpaceError("insufficient workspace to relocate GC victim")

        # Victim pages are not returned to the free list until every live page moved.
        for lpn in live_lpns:
            physical = self._allocate(avoid=victim)
            self.mapping[lpn] = physical
            self.valid_lpns[self._block_key(physical)].add(lpn)

        self.valid_lpns[victim].clear()
        self.invalid_pages[victim] = 0
        self.erase_counts[victim] += 1
        channel, die, block = victim
        for page in range(self.config.pages_per_block):
            self._reclaimed.append(PhysicalPage(channel, die, block, page))
        return GarbageCollectionResult(
            victim=victim,
            pages_moved=len(live_lpns),
            pages_reclaimed=self.config.pages_per_block,
            erase_count=self.erase_counts[victim],
        )

    def wear_fraction(self, physical: PhysicalPage) -> float:
        count = self.erase_counts[self._block_key(physical)]
        return min(1.0, count / self.config.max_erase_cycles)

    def _allocate(self, avoid: tuple[int, int, int] | None = None) -> PhysicalPage:
        for _ in range(len(self._reclaimed)):
            physical = self._reclaimed.popleft()
            if self._block_key(physical) != avoid:
                return physical
            self._reclaimed.append(physical)
        if self._next_page >= self.capacity_pages:
            raise OutOfSpaceError("FTL capacity exhausted; GC/reclamation is required")
        physical = self._physical_from_linear(self._next_page)
        self._next_page += 1
        return physical

    def _physical_from_linear(self, linear: int) -> PhysicalPage:
        stripe = linear % self.config.dies
        per_die = linear // self.config.dies
        return PhysicalPage(
            channel=stripe % self.config.channels,
            die=stripe // self.config.channels,
            block=per_die // self.config.pages_per_block,
            page=per_die % self.config.pages_per_block,
        )

    @staticmethod
    def _block_key(physical: PhysicalPage) -> tuple[int, int, int]:
        return physical.channel, physical.die, physical.block

