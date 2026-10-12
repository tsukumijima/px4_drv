# BonDriver Copy Output Bound

The Windows BonDriver exposes two GetTsStream overloads. The pointer overload
returns driver-owned storage. The copy overload writes to caller-owned storage
but has no destination-capacity parameter: pdwSize is an output, not a capacity.

IoQueue::Read instead takes its size argument as an input capacity and returns
the copied length through that argument. Passing an uninitialized local size
from the BonDriver copy overload could request an arbitrary amount of data.

The copy adapter initializes the request to 188 * 256 bytes (48,128 bytes),
initializes the remaining-buffer count, and retains unread data in IoQueue.
NumberOfPacketsPerBuffer still configures the internal queue, including 1,024
packet buffers; it does not enlarge the copy request. The pointer overload and
the host wire protocol are unchanged.

This bound is a compatibility choice for the tested clients, not a universal
capacity guarantee from IBonDriver. Copy callers need at least 48,128 writable
bytes. The reviewed recisdb 1.3.0 Windows reader allocates 81,920 bytes. Smaller
unknown callers require a separate compatibility review because this interface
cannot communicate their capacity.

## Verification

Build tests/bon_ts_copy_test.vcxproj for Release-static, x64 and Win32 using v143.
The test checks request initialization, canaries, split order, a short final
packet and empty reads. It also links the actual io_queue.cpp and checks both
overloads against deterministic ordered bytes under a four-buffer queue, with
provider fragment sizes 4,093, 153,408, 65,536 and 770,048 bytes. These are offline
tests; they do not exercise USB, named pipes, capture restarts or RF reception.

In a bounded PX-MLT8PE / J.83B 256-QAM 5.6 Msps trial, both BonRecTest's pointer
reader and recisdb 1.3.0's copy reader obtained the expected transport identities.
However, both native-client clean-carrier samples had continuity errors while
the direct protocol sample did not. Those errors remain unresolved; this change
is not a claim of lossless reception or a fix for demodulation failures.

A later diagnostic-host trial freshly opened the direct receiver per frequency.
The same control was TEI/CC-free with all three clients, while a damaged carrier
remained damaged in all three. The earlier native-client CC issue did not
reproduce, but lifetime/build/time differences prevent attributing its absence
to this copy fix. No ring-write rejection was observed in the later trial.

The separate [stream transfer diagnostics](stream_transfer_diagnostics.md)
test also exercises actual Windows named pipes. Its scope is still offline:
USB, the loaded BonDriver DLL, RF and the complete capture pipeline remain live
verification requirements.

Primary client source:
https://github.com/kazuki0824/recisdb-rs/blob/1.3.0/recisdb-rs/src/tuner/windows/IBonDriver.rs
https://github.com/kazuki0824/recisdb-rs/blob/1.3.0/recisdb-rs/src/tuner/windows/mod.rs
