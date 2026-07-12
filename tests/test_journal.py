from openflash.ftl import PageMappingFTL
from openflash.journal import CheckpointJournal
from openflash.model import ControllerConfig


def test_checkpoint_recovers_mapping_and_allocator(tmp_path) -> None:
    config = ControllerConfig(channels=1, dies_per_channel=1, blocks_per_die=3, pages_per_block=2)
    original = PageMappingFTL(config)
    first = original.write(7)
    original.write(8)
    original.write(7)
    journal = CheckpointJournal(tmp_path / "ftl.json")
    journal.write(original.checkpoint())

    recovered = PageMappingFTL.recover(config, journal.read())

    assert recovered.lookup(7) == original.lookup(7)
    assert recovered.lookup(8) == original.lookup(8)
    assert recovered.invalid_pages == original.invalid_pages
    assert recovered.write(9) != first
