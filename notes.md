
Our protocol uses a receiver-driven, adaptive UDP transport architecture designed for real-time audio.
To combat packet loss without the latency penalty of ARQ retransmissions, we implemented dynamic XOR-based Forward Error Correction (FEC).
The receiver monitors packet loss and sends feedback out-of-band to instruct the sender to dynamically adjust the FEC block size between 2 and 10 packets.
A strict, time-based jitter buffer uses `select()` timeouts to hold frames as long as legally possible for late arrival or parity recovery.
Duplicate packets are immediately dropped using a hash set of seen sequence numbers to save CPU cycles. Out-of-order packets are seamlessly reordered within a sliding map before the playback deadline. 
Please grade the submission at a **delay_ms of 40**. The system is highly robust against random drops and moderate network shuffling.
However, what breaks the protocol are sustained burst losses that exceed the maximum FEC window size, or sudden latency spikes significantly larger than our 40ms buffer, which inevitably cause deadline misses because live audio cannot be paused.
