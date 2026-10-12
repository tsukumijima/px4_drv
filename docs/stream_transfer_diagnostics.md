# Windows Stream Transfer Diagnostics

## Scope

The experimental Windows host records rejected writes at the StreamBuffer to
RingBuffer boundary. The counters observe existing behavior; they do not change
buffer sizes, retry policy, capture lifetime, demodulator settings or the pipe
protocol. No command structure or BonDriver ABI is added.

The four counters reset at StreamBuffer::Start and accumulate until the next
start. GetWriteDiagnostics is an atomic, relaxed snapshot, not a transaction
across all fields. PXMLT capture-stop logs include the snapshot only when
experimental ISDB-C is enabled and the last system is ISDB-C.

| Field | Meaning |
| --- | --- |
| rejected_bytes | Requested bytes not accepted by a failed ring write; includes full or inactive output. |
| rejected_writes | Calls with at least one rejected byte and a false return. |
| purge_write_discarded_bytes | Bytes not written when RingBuffer reports success with a shortened length during purge. |
| purge_requests | Calls to StreamBuffer::Purge, including unsuccessful calls. |

Null/header filtering and TSMF slot selection occur before this boundary and
are not counted as rejected output. Bytes already buffered and then cleared by
Purge are not included. The counters do not observe upstream USB loss,
downstream pipe loss, app output failures, or corruption originating at the
demodulator. A nonzero total does not attribute steady-capture CC errors: tuning,
startup, purge and shutdown are also within the capture lifetime. Zero totals
do not prove the entire reception chain lossless.

A receiver held open across several frequencies has one aggregate capture
interval; the frequency printed at stop is the last frequency, not the frequency
of each rejection. Fresh-per-frequency sessions make attribution narrower but
still include their startup and tuning intervals.

## Offline Verification

Build tests/stream_transfer_test.vcxproj for Release-static, x64 and Win32,
using v143. The test links the actual receiver_base, ringbuffer, common Pipe,
IoQueue and message implementations.

The buffer test checks full/inactive rejection, null-filter exclusion, purge
request counts and start/reset. The transfer test creates process-unique
px4_stream_fixture named pipes, never the production host pipe. A message-mode
server and byte-mode client feed deterministic bytes through Pipe and IoQueue
to both the bounded BonDriver copy adapter and pointer reader. Two provider
fragment sizes (4,093 and 153,408 bytes), four queue buffers, destination
canaries and a one-packet final tail are checked for exact length and order.
Cancellation and deadlines bound fixture failures.

This is not a test of the loaded BonDriver DLL, USB, actual RF, the complete
StreamBuffer-to-pipe pipeline, simultaneous purge, or capture restarts under
hardware load. Live verification must compare transport identity, TEI, CC,
sync errors and output diagnostics with matched receiver lifetimes. Any monitor
traffic or different purging behavior remains a comparison confound.

Related: [BonDriver copy output bound](bon_ts_copy.md).

## Bounded Live Observation

A PX-MLT8PE on a FLET'S TV coax feed was tested with the experimental Windows
host, J.83B, 256-QAM, 5.6 Msps and zero frequency correction. Five-tuner input 4
was freshly opened for each direct/BonRecTest/recisdb frequency trial. Three-tuner
input 2 was held open across eleven frequencies. Each steady sample was six
seconds; the coax connectors' physical mapping was not established.

All ten capture-lifetime diagnostics had zero rejected bytes/writes and zero
purge-write discarded bytes. The 567.750 MHz control had zero TEI/CC across all
three clients and both USB groups. The 320.750 MHz transport had substantial TEI
and CC errors in every client and both USB groups. Eight previously missing
centers also produced no TS on the three-tuner group.

These observations do not attribute damaged reception to ring-buffer overflow
in this run, nor prove the entire pipeline lossless. The final three-tuner
diagnostic represents all eleven frequencies together, not just the printed
last frequency. Earlier native-client control CC errors were not reproduced;
changed lifetime/build/time conditions prevent treating this as a verified fix.
