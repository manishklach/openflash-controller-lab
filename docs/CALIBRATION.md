# Simulator Calibration

The default simulator configuration remains a policy-testing baseline, not a NAND part model.
Use named profiles whenever reporting results and record any geometry, ECC, and distribution
overrides alongside the workload seed.

## Micron SLC timing profile

`openflash.profiles.micron_slc_timing_profile()` grounds the array timing fields in Micron's
2025 aerospace and defense NAND flyer:

| Field | Profile value | Datasheet statement |
| --- | ---: | --- |
| page read | 35 us | maximum |
| page program | 350 us | typical |
| block erase | 1.5 ms | typical |

Source: [Micron NAND flyer](https://assets.micron.com/adobe/assets/urn%3Aaaid%3Aaem%3Ad8f12ea4-b77a-4d42-ba97-fe17f00dfb14/renditions/original/as/aerospace-and-defense-extreme-conditions-nand-flyer.pdf).

The profile deliberately fixes `latency_sigma` at zero because this source publishes
single timing values, not a latency distribution. Geometry, raw-bit-error behavior, ECC
strength, interface transfer rate, and aging behavior remain assumptions until a selected
part's complete datasheet and measurements are recorded.
