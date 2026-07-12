from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum


ABI_VERSION = 0x0000_0002
BLOCK_SIZE = 4096
COMMAND_SIZE = 64
COMPLETION_SIZE = 64
FLAG_FUA = 1 << 0


class Opcode(IntEnum):
    READ = 0x01
    WRITE = 0x02
    FLUSH = 0x03
    DISCARD = 0x04
    IDENTIFY = 0x80


class Status(IntEnum):
    SUCCESS = 0
    INVALID_OPCODE = 1
    INVALID_FIELD = 2
    LBA_RANGE = 3
    MEDIA_ERROR = 4
    ECC_UNCORRECTABLE = 5
    ABORTED = 6
    INTERNAL = 7


_COMMAND = struct.Struct("<BBHHHQIIQQQQQ")
_COMPLETION = struct.Struct("<QQHHHHQQQQQ")
assert _COMMAND.size == COMMAND_SIZE
assert _COMPLETION.size == COMPLETION_SIZE


@dataclass(frozen=True, slots=True)
class Command:
    opcode: int
    flags: int = 0
    qid: int = 0
    cid: int = 0
    lba: int = 0
    nblocks: int = 0
    control: int = 0
    data_addr: int = 0
    metadata_addr: int = 0
    user_data: int = 0

    def pack(self) -> bytes:
        return _COMMAND.pack(
            self.opcode,
            self.flags,
            self.qid,
            self.cid,
            0,
            self.lba,
            self.nblocks,
            self.control,
            self.data_addr,
            self.metadata_addr,
            self.user_data,
            0,
            0,
        )

    @classmethod
    def unpack(cls, value: bytes) -> Command:
        if len(value) != COMMAND_SIZE:
            raise ValueError(f"command must be {COMMAND_SIZE} bytes")
        fields = _COMMAND.unpack(value)
        return cls(
            opcode=fields[0],
            flags=fields[1],
            qid=fields[2],
            cid=fields[3],
            lba=fields[5],
            nblocks=fields[6],
            control=fields[7],
            data_addr=fields[8],
            metadata_addr=fields[9],
            user_data=fields[10],
        )


@dataclass(frozen=True, slots=True)
class Completion:
    result: int
    user_data: int
    sq_head: int
    cid: int
    status: Status
    flags: int = 0

    def pack(self) -> bytes:
        return _COMPLETION.pack(
            self.result,
            self.user_data,
            self.sq_head,
            self.cid,
            int(self.status),
            self.flags,
            0,
            0,
            0,
            0,
            0,
        )

    @classmethod
    def unpack(cls, value: bytes) -> Completion:
        if len(value) != COMPLETION_SIZE:
            raise ValueError(f"completion must be {COMPLETION_SIZE} bytes")
        fields = _COMPLETION.unpack(value)
        return cls(
            result=fields[0],
            user_data=fields[1],
            sq_head=fields[2],
            cid=fields[3],
            status=Status(fields[4]),
            flags=fields[5],
        )
