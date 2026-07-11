from __future__ import annotations

from collections.abc import MutableMapping

from .abi import BLOCK_SIZE, FLAG_FUA, Command, Completion, Opcode, Status


class ReferenceDevice:
    """Sparse in-memory behavioral oracle for the OpenFlash v0.1 command ABI."""

    def __init__(self, capacity_blocks: int = 1024) -> None:
        if capacity_blocks <= 0:
            raise ValueError("capacity must be positive")
        self.capacity_blocks = capacity_blocks
        self._stable: dict[int, bytes] = {}
        self._volatile: dict[int, bytes | None] = {}
        self._next_fault: Status | None = None
        self.completed = 0

    def inject_next_fault(self, status: Status) -> None:
        if status == Status.SUCCESS:
            raise ValueError("fault status cannot be SUCCESS")
        self._next_fault = status

    def execute(self, command: Command, dma: MutableMapping[int, bytearray]) -> Completion:
        status = self._next_fault
        self._next_fault = None
        result = 0
        if status is None:
            try:
                opcode = Opcode(command.opcode)
            except ValueError:
                status = Status.INVALID_OPCODE
            else:
                status, result = self._dispatch(opcode, command, dma)
        completion = Completion(
            result=result,
            user_data=command.user_data,
            sq_head=self.completed & 0xFFFF,
            cid=command.cid,
            status=status,
        )
        self.completed += 1
        return completion

    def power_loss(self) -> None:
        self._volatile.clear()

    def _dispatch(
        self, opcode: Opcode, command: Command, dma: MutableMapping[int, bytearray]
    ) -> tuple[Status, int]:
        if opcode == Opcode.IDENTIFY:
            return Status.SUCCESS, self.capacity_blocks
        if opcode == Opcode.FLUSH:
            for lba, data in self._volatile.items():
                if data is None:
                    self._stable.pop(lba, None)
                else:
                    self._stable[lba] = data
            self._volatile.clear()
            return Status.SUCCESS, 0
        if command.nblocks <= 0:
            return Status.INVALID_FIELD, 0
        if command.lba + command.nblocks > self.capacity_blocks:
            return Status.LBA_RANGE, 0
        if opcode == Opcode.DISCARD:
            for lba in range(command.lba, command.lba + command.nblocks):
                self._volatile[lba] = None
            return Status.SUCCESS, command.nblocks
        if command.data_addr not in dma:
            return Status.INVALID_FIELD, 0
        size = command.nblocks * BLOCK_SIZE
        buffer = dma[command.data_addr]
        if len(buffer) < size:
            return Status.INVALID_FIELD, 0
        if opcode == Opcode.WRITE:
            for index in range(command.nblocks):
                offset = index * BLOCK_SIZE
                self._volatile[command.lba + index] = bytes(buffer[offset : offset + BLOCK_SIZE])
            if command.flags & FLAG_FUA:
                self._dispatch(Opcode.FLUSH, command, dma)
            return Status.SUCCESS, command.nblocks
        if opcode == Opcode.READ:
            for index in range(command.nblocks):
                lba = command.lba + index
                data = self._volatile.get(lba, self._stable.get(lba, bytes(BLOCK_SIZE)))
                if data is None:
                    data = bytes(BLOCK_SIZE)
                offset = index * BLOCK_SIZE
                buffer[offset : offset + BLOCK_SIZE] = data
            return Status.SUCCESS, command.nblocks
        return Status.INVALID_OPCODE, 0

