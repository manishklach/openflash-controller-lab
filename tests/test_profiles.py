from openflash.profiles import micron_slc_timing_profile


def test_micron_slc_profile_uses_published_array_timing() -> None:
    profile = micron_slc_timing_profile()
    assert (profile.read_us, profile.program_us, profile.erase_us) == (35.0, 350.0, 1500.0)
    assert profile.latency_sigma == 0.0
