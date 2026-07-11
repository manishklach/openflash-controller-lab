from openflash.model import IORequest, Operation, PhysicalPage
from openflash.scheduler import ReadPriorityScheduler, ScheduledRequest


def scheduled(request_id: int, operation: Operation) -> ScheduledRequest:
    return ScheduledRequest(IORequest(request_id, operation, request_id), PhysicalPage(0, 0, 0, 0))


def test_read_priority_bounds_write_starvation() -> None:
    work = [scheduled(0, Operation.WRITE)] + [scheduled(i, Operation.READ) for i in range(1, 7)]
    ordered = ReadPriorityScheduler(read_burst_limit=2).order(work)
    assert [item.request.operation for item in ordered[:3]] == [
        Operation.READ,
        Operation.READ,
        Operation.WRITE,
    ]

