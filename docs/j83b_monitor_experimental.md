# J.83B experimental raw monitor

`DeviceDefinitionN.Config.ExperimentalJ83BMonitor=true` enables an additional
diagnostic sample when CNR is requested for a J.83B receiver. The setting defaults
to false. It does not change tuning initialization or the host protocol.

The diagnostic log includes input index, requested frequency in kHz, raw QAM
status, 13-bit C/N estimator and 12-bit IF AGC. These are uncalibrated raw values,
not dB, dBm, RF power, or percentages. CNR still returns unsupported rather than
a fabricated numerical quality value. Reads require an active J.83B demodulator.

Reference: [koreapyj/cxd28xx cxd2878.c](https://github.com/koreapyj/cxd28xx/blob/c5fa27eecbe80633cbb365cc44656daea1c0e2e3/cxd2878.c),
Annex A/B/C monitoring at SLV-T bank 0x40, QAM 0x19 and estimator 0x4c-0x4d.
IF AGC 0x49-0x4a reuses this driver's existing cable diagnostic location.
Applicability and calibration on CXD2856ER remain subject to hardware measurement.
The reference's conversion and fixed adjustment are not a calibrated CXD2856ER
measurement, so numerical CNR conversion is outside this experimental change.

Register freezing follows the existing C/N sample pattern. Every operation after
successful freeze attempts unfreeze, preserving the original I/O error. Failure
to unfreeze is returned as an error; the output sample remains zero on any error.
Mock tests cover inactive state, bit masks, and each I/O failure boundary.
Mock results do not prove analog calibration or absence of TS performance impact.

## Acquisition and FEC snapshots

The same opt-in setting also emits a `J83B stages` line associated with the
preceding raw-monitor line by the device log identity, input index and frequency.
No host command structure or BonDriver ABI changes are involved. It contains:

- SLV-T 0x40:0x88/0x10 acquisition and TS status, 0x80:0x50 VQ status,
  and 0x80:0x53-0x55 interleave validity, mode and spectrum sense.
- Signed 14-bit carrier offset at 0x40:0x15-0x16, symbol-rate trial value at
  0x40:0x1a-0x1b, and timing integral/mode at 0x40:0x9b-0x9f.
- Pre-RS error count/validity at 0x40:0x62-0x64 and its period at 0x40:0x60;
  packet error count/validity/period at 0x40:0x5c-0x5f; RS reject count/validity
  at 0x40:0xea-0xec. These are sampled windows, not cumulative recording totals.

Register facts and validity conditions were cross-checked against the
[public CXD2856 monitor source](https://github.com/zhe2lucifer/linux-kernel-zh/blob/8fb05a60b37d48a9f2f5b95d9452fd3f081e82a2/drivers/alidrivers/modules/alinim/dem_cxd2856/j83b/sony_demod_j83b_monitor.c)
and its corresponding header, carrying Sony's 2016 copyright notice. This is
not an assertion of an official Sony distribution or a license grant for that
repository. The monitor implementation extends this driver's existing I/O and
cleanup flow rather than importing the reference library.

Offset and symbol-rate registers are read only with acquisition lock. Other raw
registers remain available while unlocked, but QAM/SNR need acquisition lock,
spectrum sense needs VQ lock, and error counters need their own valid bits and
consistent count/period bounds. Invalid samples must not be interpreted as zero
error rates or proof of RF absence. Spectrum inversion configuration is needed
before a demodulator offset can become an RF-frequency correction.

The snapshot uses 19 I/O requests when acquisition-locked and 16 otherwise,
including bank selection and freeze cleanup, compared with six in the earlier
raw-only implementation. Cleanup always attempts bank restoration and unfreeze
after successful freeze, preserving the first error and publishing no partial
sample. Mock tests cover both paths, signed extrema and cleanup failure.
Short Windows WinUSB / PX-MLT8PE five-tuner-side comparisons observed zero
sampled FEC counters and TS errors on clean control carriers, but valid nonzero
PER/RS rejection counts with TS errors on a problematic carrier. Another center
reached interleave lock without TS lock and reported a high valid PER. These
results support the diagnostic distinction, not calibration or applicability to
every receiver. Monitor-on/off and long-duration shared-load effects remain
unverified; short clean controls do not establish that polling is harmless.
The setting remains disabled by default; CNR requests still return unsupported.

For hardware evaluation, compare a known clean cable carrier and the problematic
carrier on the same explicitly reserved receiver, recording raw samples and TS
TEI/continuity counts. Recheck terrestrial and satellite reception afterwards.
Polling occupies the shared control bus; high-rate polling is inappropriate.
Existing production installations need no configuration changes.
