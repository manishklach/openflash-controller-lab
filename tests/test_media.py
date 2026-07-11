from openflash.media import MediaModel
from openflash.model import ControllerConfig, Operation


def test_seeded_media_model_is_reproducible() -> None:
    config = ControllerConfig(random_seed=99)
    first = MediaModel(config).execute(Operation.READ, pages=1, wear_fraction=0.5)
    second = MediaModel(config).execute(Operation.READ, pages=1, wear_fraction=0.5)
    assert first == second


def test_ecc_retry_can_recover_error_burst() -> None:
    config = ControllerConfig(
        random_seed=1, base_raw_bit_errors=1000, ecc_strength_bits=80, read_retry_limit=8
    )
    result = MediaModel(config).execute(Operation.READ, pages=1, wear_fraction=1.0)
    assert result.retries > 0
    assert result.latency_us > config.read_us
