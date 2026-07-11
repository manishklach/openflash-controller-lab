from openflash.abi import COMMAND_SIZE, Command, Opcode


def test_command_binary_layout_round_trip() -> None:
    command = Command(
        opcode=Opcode.WRITE,
        flags=1,
        qid=2,
        cid=3,
        lba=99,
        nblocks=4,
        data_addr=0x12340000,
        user_data=0xCAFE,
    )
    packed = command.pack()
    assert len(packed) == COMMAND_SIZE
    assert Command.unpack(packed) == command

