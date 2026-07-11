from __future__ import annotations

import math
from dataclasses import dataclass
from statistics import mean

from .ftl import PageMappingFTL
from .model import ControllerConfig, IORequest, Operation
from .scheduler import ScheduledRequest, create_scheduler


@dataclass(frozen=True, slots=True)
class Completion:
    request_id: int
    operation: Operation
    start_us: float
    finish_us: float
    latency_us: float


@dataclass(frozen=True, slots=True)
class SimulationResult:
    policy: str
    requests: int
    elapsed_us: float
    throughput_mib_s: float
    iops: float
    mean_latency_us: float
    p50_latency_us: float
    p95_latency_us: float
    p99_latency_us: float
    read_p99_latency_us: float
    write_p99_latency_us: float
    channel_utilization: float

    def as_dict(self) -> dict[str, float | int | str]:
        return {field: getattr(self, field) for field in self.__dataclass_fields__}


class Simulator:
    """Queue-level NAND timing model with independent die and shared-channel resources."""

    def __init__(self, config: ControllerConfig, policy: str = "read-priority") -> None:
        self.config = config
        self.policy = policy
        self.ftl = PageMappingFTL(config)

    def run(self, requests: list[IORequest]) -> SimulationResult:
        if not requests:
            raise ValueError("at least one request is required")
        mapped: list[ScheduledRequest] = []
        for request in requests:
            physical = (
                self.ftl.map_read(request.lpn)
                if request.operation == Operation.READ
                else self.ftl.write(request.lpn)
            )
            mapped.append(ScheduledRequest(request, physical))

        scheduler = create_scheduler(self.policy, self.config.read_burst_limit)
        ordered = scheduler.order(mapped)
        die_ready = [[0.0] * self.config.dies_per_channel for _ in range(self.config.channels)]
        channel_ready = [0.0] * self.config.channels
        channel_busy = [0.0] * self.config.channels
        completions: list[Completion] = []

        for item in ordered:
            request, physical = item.request, item.physical
            channel = physical.channel
            die = physical.die
            start = max(request.arrival_us, die_ready[channel][die], channel_ready[channel])
            transfer = self._transfer_time_us(request.pages)
            media = self._media_time_us(request.operation, request.pages)

            # Command/data transfer occupies the channel; media work then proceeds on the die.
            channel_ready[channel] = start + transfer
            channel_busy[channel] += transfer
            finish = start + transfer + media
            die_ready[channel][die] = finish
            completions.append(
                Completion(request.request_id, request.operation, start, finish, finish - request.arrival_us)
            )

        elapsed = max(item.finish_us for item in completions)
        total_bytes = sum(item.pages for item in requests) * self.config.page_size
        latencies = [item.latency_us for item in completions]
        reads = [item.latency_us for item in completions if item.operation == Operation.READ]
        writes = [item.latency_us for item in completions if item.operation == Operation.WRITE]
        return SimulationResult(
            policy=self.policy,
            requests=len(requests),
            elapsed_us=elapsed,
            throughput_mib_s=(total_bytes / (1024 * 1024)) / (elapsed / 1_000_000),
            iops=len(requests) / (elapsed / 1_000_000),
            mean_latency_us=mean(latencies),
            p50_latency_us=_percentile(latencies, 50),
            p95_latency_us=_percentile(latencies, 95),
            p99_latency_us=_percentile(latencies, 99),
            read_p99_latency_us=_percentile(reads, 99) if reads else 0.0,
            write_p99_latency_us=_percentile(writes, 99) if writes else 0.0,
            channel_utilization=sum(channel_busy) / (elapsed * self.config.channels),
        )

    def _transfer_time_us(self, pages: int) -> float:
        bytes_per_us = self.config.channel_transfer_gbps * 1_000_000_000 / 8 / 1_000_000
        return pages * self.config.page_size / bytes_per_us

    def _media_time_us(self, operation: Operation, pages: int) -> float:
        latency = {
            Operation.READ: self.config.read_us,
            Operation.WRITE: self.config.program_us,
            Operation.ERASE: self.config.erase_us,
        }[operation]
        return latency * pages


def _percentile(values: list[float], percentile: int) -> float:
    if not values:
        raise ValueError("cannot calculate a percentile for an empty list")
    ordered = sorted(values)
    index = max(0, math.ceil(percentile / 100 * len(ordered)) - 1)
    return ordered[index]

