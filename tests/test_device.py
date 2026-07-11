from openflash.abi import BLOCK_SIZE, FLAG_FUA, Command, Opcode, Status
from openflash.device import ReferenceDevice


def test_write_read_and_flush_survive_power_loss() -> None:
    device = ReferenceDevice(capacity_blocks=8)
    payload = bytearray(b"A" * BLOCK_SIZE)
    dma = {0x1000: payload, 0x2000: bytearray(BLOCK_SIZE)}
    write = Command(Opcode.WRITE, cid=1, lba=2, nblocks=1, data_addr=0x1000)
    assert device.execute(write, dma).status == Status.SUCCESS
    device.execute(Command(Opcode.FLUSH, cid=2), dma)
    device.power_loss()
    read = Command(Opcode.READ, cid=3, lba=2, nblocks=1, data_addr=0x2000)
    assert device.execute(read, dma).status == Status.SUCCESS
    assert dma[0x2000] == payload


def test_unflushed_write_is_lost_but_fua_survives() -> None:
    device = ReferenceDevice(capacity_blocks=8)
    dma = {0x1000: bytearray(b"B" * BLOCK_SIZE), 0x2000: bytearray(BLOCK_SIZE)}
    device.execute(Command(Opcode.WRITE, lba=1, nblocks=1, data_addr=0x1000), dma)
    device.power_loss()
    device.execute(Command(Opcode.READ, lba=1, nblocks=1, data_addr=0x2000), dma)
    assert dma[0x2000] == bytes(BLOCK_SIZE)
    device.execute(
        Command(Opcode.WRITE, flags=FLAG_FUA, lba=1, nblocks=1, data_addr=0x1000), dma
    )
    device.power_loss()
    device.execute(Command(Opcode.READ, lba=1, nblocks=1, data_addr=0x2000), dma)
    assert dma[0x2000] == dma[0x1000]


def test_fault_injection_and_range_validation() -> None:
    device = ReferenceDevice(capacity_blocks=4)
    device.inject_next_fault(Status.MEDIA_ERROR)
    assert device.execute(Command(Opcode.IDENTIFY), {}).status == Status.MEDIA_ERROR
    command = Command(Opcode.READ, lba=4, nblocks=1, data_addr=0x1000)
    assert device.execute(command, {0x1000: bytearray(BLOCK_SIZE)}).status == Status.LBA_RANGE
