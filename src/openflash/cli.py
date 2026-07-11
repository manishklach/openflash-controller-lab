from __future__ import annotations

import argparse
import json
import random
from dataclasses import asdict

from .model import ControllerConfig, IORequest, Operation
from .simulator import SimulationResult, Simulator


def generate_workload(
    count: int, read_ratio: float, working_set_pages: int, seed: int, arrival_rate: float
) -> list[IORequest]:
    if count <= 0 or working_set_pages <= 0 or not 0 <= read_ratio <= 1 or arrival_rate <= 0:
        raise ValueError("invalid workload parameters")
    randomizer = random.Random(seed)
    requests: list[IORequest] = []
    arrival = 0.0
    for request_id in range(count):
        arrival += randomizer.expovariate(arrival_rate)
        operation = Operation.READ if randomizer.random() < read_ratio else Operation.WRITE
        requests.append(
            IORequest(request_id, operation, randomizer.randrange(working_set_pages), arrival_us=arrival)
        )
    return requests


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="OpenFlash NAND controller simulator")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("run", "compare"):
        command = subparsers.add_parser(name)
        command.add_argument("--requests", type=int, default=10_000)
        command.add_argument("--read-ratio", type=float, default=0.7)
        command.add_argument("--working-set-pages", type=int, default=65_536)
        command.add_argument("--arrival-rate", type=float, default=0.5, help="requests per microsecond")
        command.add_argument("--seed", type=int, default=7)
        command.add_argument("--channels", type=int, default=8)
        command.add_argument("--dies-per-channel", type=int, default=4)
        command.add_argument("--json", action="store_true")
        if name == "run":
            command.add_argument("--policy", choices=("fifo", "read-priority"), default="read-priority")
    return parser


def _print_result(result: SimulationResult, as_json: bool) -> None:
    if as_json:
        print(json.dumps(result.as_dict(), indent=2))
        return
    print(
        f"{result.policy:14} {result.throughput_mib_s:9.1f} MiB/s  "
        f"{result.iops:10.0f} IOPS  p99={result.p99_latency_us:9.1f} us  "
        f"read-p99={result.read_p99_latency_us:9.1f} us"
    )


def main() -> None:
    args = _parser().parse_args()
    config = ControllerConfig(channels=args.channels, dies_per_channel=args.dies_per_channel)
    requests = generate_workload(
        args.requests, args.read_ratio, args.working_set_pages, args.seed, args.arrival_rate
    )
    policies = (args.policy,) if args.command == "run" else ("fifo", "read-priority")
    results = [Simulator(config, policy).run(requests) for policy in policies]
    if args.json and len(results) > 1:
        print(json.dumps([asdict(result) for result in results], indent=2))
    else:
        for result in results:
            _print_result(result, args.json)


if __name__ == "__main__":
    main()

