import pytest

from openflash.abi import BLOCK_SIZE, Command, Opcode, Status
from openflash.device import ReferenceDevice
from openflash.queue import QueueEngine, QueueFullError, allocate_dma


def test_queue_executes_io_and_signals_one_interrupt_per_doorbell() -> None:
    interrupt_batches: list[int] = []
    queue = QueueEngine(ReferenceDevice(16), depth=4, interrupt=interrupt_batches.append)
    dma = {0x1000: bytearray(b"Q" * BLOCK_SIZE), 0x2000: allocate_dma()}
    queue.submit(Command(Opcode.WRITE, cid=1, lba=4, nblocks=1, data_addr=0x1000))
    queue.submit(Command(Opcode.READ, cid=2, lba=4, nblocks=1, data_addr=0x2000))
    assert queue.ring_submission_doorbell(dma) == 2
    assert interrupt_batches == [2]
    assert queue.consume_completion().status == Status.SUCCESS
    assert queue.consume_completion().cid == 2
    assert dma[0x2000] == dma[0x1000]


def test_phase_bit_toggles_after_completion_ring_wrap() -> None:
    queue = QueueEngine(ReferenceDevice(16), depth=2)
    phases = []
    for cid in range(4):
        queue.submit(Command(Opcode.IDENTIFY, cid=cid))
        queue.ring_submission_doorbell({})
        phases.append(queue.consume_completion().flags & 1)
    assert phases == [1, 1, 0, 0]


def test_full_queue_backpressure_and_reset() -> None:
    queue = QueueEngine(ReferenceDevice(16), depth=2)
    queue.submit(Command(Opcode.IDENTIFY, cid=1))
    queue.submit(Command(Opcode.IDENTIFY, cid=2))
    with pytest.raises(QueueFullError):
        queue.submit(Command(Opcode.IDENTIFY, cid=3))
    assert queue.reset() == 2
    assert queue.pending == 0
    assert queue.completions == 0
