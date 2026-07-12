from __future__ import annotations

from .model import ControllerConfig


MICRON_SLC_SOURCE = (
    "Micron aerospace and defense NAND flyer (2025): 35 us page read max, "
    "350 us page program typical, 1.5 ms block erase typical."
)
MICRON_SLC_SOURCE_URL = (
    "https://assets.micron.com/adobe/assets/urn%3Aaaid%3Aaem%3Ad8f12ea4-b77a-4d42-ba97-fe17f00dfb14/"
    "renditions/original/as/aerospace-and-defense-extreme-conditions-nand-flyer.pdf"
)


def micron_slc_timing_profile(**overrides: object) -> ControllerConfig:
    """Return a timing-calibrated SLC profile; geometry and ECC remain explicit inputs."""

    values: dict[str, object] = {
        "read_us": 35.0,
        "program_us": 350.0,
        "erase_us": 1500.0,
        "latency_sigma": 0.0,
    }
    values.update(overrides)
    return ControllerConfig(**values)
