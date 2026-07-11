from __future__ import annotations

import math
import random
from dataclasses import dataclass

from .model import ControllerConfig, Operation


@dataclass(frozen=True, slots=True)
class MediaOperation:
    latency_us: float
    corrected_bits: int = 0
    retries: int = 0
    uncorrectable: bool = False


class MediaModel:
    """Seeded latency and ECC model suitable for repeatable policy experiments."""

    def __init__(self, config: ControllerConfig) -> None:
        self.config = config
        self.random = random.Random(config.random_seed)

    def execute(self, operation: Operation, pages: int, wear_fraction: float) -> MediaOperation:
        nominal = {
            Operation.READ: self.config.read_us,
            Operation.WRITE: self.config.program_us,
            Operation.ERASE: self.config.erase_us,
        }[operation] * pages
        latency = self._sample_lognormal(nominal)
        if operation != Operation.READ:
            return MediaOperation(latency)

        error_mean = self.config.base_raw_bit_errors * pages * (1.0 + 12.0 * wear_fraction)
        corrected_bits = int(self.random.expovariate(1.0 / max(error_mean, 0.001)))
        retries = 0
        while corrected_bits > self.config.ecc_strength_bits and retries < self.config.read_retry_limit:
            retries += 1
            latency += self._sample_lognormal(nominal)
            corrected_bits = int(corrected_bits * 0.55)
        return MediaOperation(
            latency_us=latency,
            corrected_bits=corrected_bits,
            retries=retries,
            uncorrectable=corrected_bits > self.config.ecc_strength_bits,
        )

    def _sample_lognormal(self, mean: float) -> float:
        sigma = self.config.latency_sigma
        if sigma == 0:
            return mean
        mu = math.log(mean) - sigma * sigma / 2
        return self.random.lognormvariate(mu, sigma)

