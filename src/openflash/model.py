from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class Operation(str, Enum):
    READ = "read"
    WRITE = "write"
    ERASE = "erase"


@dataclass(frozen=True, slots=True)
class ControllerConfig:
    channels: int = 8
    dies_per_channel: int = 4
    pages_per_block: int = 256
    blocks_per_die: int = 1024
    page_size: int = 16 * 1024
    read_us: float = 65.0
    program_us: float = 550.0
    erase_us: float = 3500.0
    channel_transfer_gbps: float = 1.2
    queue_depth: int = 128
    read_burst_limit: int = 16
    latency_sigma: float = 0.18
    gc_low_watermark_blocks: int = 4
    max_erase_cycles: int = 3_000
    ecc_strength_bits: int = 80
    base_raw_bit_errors: float = 2.0
    read_retry_limit: int = 3
    random_seed: int = 7

    def __post_init__(self) -> None:
        positive = (
            self.channels,
            self.dies_per_channel,
            self.pages_per_block,
            self.blocks_per_die,
            self.page_size,
            self.queue_depth,
        )
        if any(value <= 0 for value in positive):
            raise ValueError("geometry and queue values must be positive")
        if min(self.read_us, self.program_us, self.erase_us, self.channel_transfer_gbps) <= 0:
            raise ValueError("timing values must be positive")
        if self.latency_sigma < 0 or self.gc_low_watermark_blocks < 0:
            raise ValueError("latency sigma and GC watermark cannot be negative")
        if min(self.max_erase_cycles, self.ecc_strength_bits) <= 0 or self.read_retry_limit < 0:
            raise ValueError("endurance and ECC values are invalid")

    @property
    def dies(self) -> int:
        return self.channels * self.dies_per_channel


@dataclass(frozen=True, slots=True)
class IORequest:
    request_id: int
    operation: Operation
    lpn: int
    pages: int = 1
    arrival_us: float = 0.0

    def __post_init__(self) -> None:
        if self.request_id < 0 or self.lpn < 0 or self.pages <= 0 or self.arrival_us < 0:
            raise ValueError("request fields cannot be negative and pages must be positive")


@dataclass(frozen=True, slots=True)
class PhysicalPage:
    channel: int
    die: int
    block: int
    page: int
