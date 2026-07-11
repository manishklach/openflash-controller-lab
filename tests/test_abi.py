from openflash.abi import COMMAND_SIZE, COMPLETION_SIZE, Command, Completion, Opcode, Status


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


def test_completion_binary_layout_round_trip() -> None:
    completion = Completion(2, 0xCAFE, 7, 3, Status.SUCCESS, flags=1)
    packed = completion.pack()
    assert len(packed) == COMPLETION_SIZE
    assert Completion.unpack(packed) == completion
