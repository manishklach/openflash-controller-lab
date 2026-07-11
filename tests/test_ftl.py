import pytest

from openflash.ftl import OutOfSpaceError, PageMappingFTL
from openflash.model import ControllerConfig


def test_allocation_stripes_across_channels_and_dies() -> None:
    config = ControllerConfig(channels=2, dies_per_channel=2, blocks_per_die=1, pages_per_block=2)
    ftl = PageMappingFTL(config)
    pages = [ftl.write(lpn) for lpn in range(4)]
    assert [(page.channel, page.die) for page in pages] == [(0, 0), (1, 0), (0, 1), (1, 1)]


def test_overwrite_invalidates_old_physical_page() -> None:
    config = ControllerConfig(channels=1, dies_per_channel=1, blocks_per_die=1, pages_per_block=2)
    ftl = PageMappingFTL(config)
    first = ftl.write(5)
    second = ftl.write(5)
    assert first != second
    assert ftl.invalid_pages[(0, 0, 0)] == 1


def test_capacity_exhaustion_is_explicit() -> None:
    config = ControllerConfig(channels=1, dies_per_channel=1, blocks_per_die=1, pages_per_block=1)
    ftl = PageMappingFTL(config)
    ftl.write(0)
    with pytest.raises(OutOfSpaceError):
        ftl.write(1)

