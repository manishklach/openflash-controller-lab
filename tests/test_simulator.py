from openflash.cli import generate_workload
from openflash.model import ControllerConfig, IORequest, Operation
from openflash.simulator import Simulator


def test_parallel_channels_reduce_elapsed_time() -> None:
    requests = [IORequest(i, Operation.WRITE, i) for i in range(8)]
    serial = Simulator(ControllerConfig(channels=1, dies_per_channel=1), "fifo").run(requests)
    parallel = Simulator(ControllerConfig(channels=4, dies_per_channel=2), "fifo").run(requests)
    assert parallel.elapsed_us < serial.elapsed_us
    assert parallel.throughput_mib_s > serial.throughput_mib_s


def test_generated_workload_is_reproducible() -> None:
    first = generate_workload(20, 0.7, 100, 42, 0.5)
    second = generate_workload(20, 0.7, 100, 42, 0.5)
    assert first == second


def test_empty_workload_is_rejected() -> None:
    try:
        Simulator(ControllerConfig()).run([])
    except ValueError as error:
        assert "at least one" in str(error)
    else:
        raise AssertionError("expected an empty workload to be rejected")

