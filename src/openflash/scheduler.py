from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from typing import Protocol

from .model import IORequest, Operation, PhysicalPage


@dataclass(frozen=True, slots=True)
class ScheduledRequest:
    request: IORequest
    physical: PhysicalPage


class Scheduler(Protocol):
    def order(self, requests: list[ScheduledRequest]) -> list[ScheduledRequest]: ...


class FIFOScheduler:
    def order(self, requests: list[ScheduledRequest]) -> list[ScheduledRequest]:
        return sorted(requests, key=lambda item: (item.request.arrival_us, item.request.request_id))


class ReadPriorityScheduler:
    """Prioritize reads while bounding write starvation with a read burst limit."""

    def __init__(self, read_burst_limit: int = 16) -> None:
        self.read_burst_limit = read_burst_limit

    def order(self, requests: list[ScheduledRequest]) -> list[ScheduledRequest]:
        reads = deque(
            sorted(
                (item for item in requests if item.request.operation == Operation.READ),
                key=lambda item: (item.request.arrival_us, item.request.request_id),
            )
        )
        writes = deque(
            sorted(
                (item for item in requests if item.request.operation != Operation.READ),
                key=lambda item: (item.request.arrival_us, item.request.request_id),
            )
        )
        ordered: list[ScheduledRequest] = []
        read_burst = 0
        while reads or writes:
            if reads and (not writes or read_burst < self.read_burst_limit):
                ordered.append(reads.popleft())
                read_burst += 1
            else:
                ordered.append(writes.popleft())
                read_burst = 0
        return ordered


def create_scheduler(name: str, read_burst_limit: int = 16) -> Scheduler:
    if name == "fifo":
        return FIFOScheduler()
    if name == "read-priority":
        return ReadPriorityScheduler(read_burst_limit)
    raise ValueError(f"unknown scheduler policy: {name}")

