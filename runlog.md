# Experiment Run Log

### Experiment 1: Baseline Check
* **Profile:** A (Mild)
* **Delay (ms):** 60
* **Miss %:** 2.10% (INVALID)
* **Overhead:** 1.00x
* **Changes:** Ran the provided naive baseline C code.
* **Why:** To establish a baseline. It fails because every dropped packet directly translates to a deadline miss. Duplicates also waste CPU.

### Experiment 2: Deduplication and Reordering
* **Profile:** A (Mild)
* **Delay (ms):** 60
* **Miss %:** 1.95% (INVALID)
* **Overhead:** 1.00x
* **Changes:** Migrated to C++. Added `std::unordered_set` to drop duplicates and `std::map` to reorder packets. Added strict `select()` timeout jitter buffer.
* **Why:** Reordering and deduplication fixed glitches caused by network shuffling, but the hard packet drops from the relay are still causing misses. I need redundancy.

### Experiment 3: Static XOR FEC
* **Profile:** B (Moderate)
* **Delay (ms):** 60
* **Miss %:** 0.85% (VALID)
* **Overhead:** 1.25x
* **Changes:** Changed wire format. Grouped packets by 4 at the sender, XORed the payloads together, and sent a parity packet. Added recovery logic at the receiver.
* **Why:** To recover from packet loss without waiting for retransmissions. It worked well, but the overhead is slightly high for clean networks, and a group size of 4 might fail against heavy burst loss.

### Experiment 4: Adaptive Redundancy via Feedback
* **Profile:** B (Moderate)
* **Delay (ms):** 40
* **Miss %:** 0.40% (VALID)
* **Overhead:** ~1.15x (Variable)
* **Changes:** Activated the feedback port. Receiver now calculates loss rate every 500ms and sends a target FEC size (2, 4, 6, or 10) back to the sender.
* **Why:** To dynamically balance bandwidth overhead and loss protection. Lowered the delay to 40ms to test system speed. The system easily survived the moderate profile with low overhead.

### Experiment 5: Delay Minimization / Stress Test
* **Profile:** B (Moderate)
* **Delay (ms):** 35
* **Miss %:** 0.70% (VALID)
* **Overhead:** ~1.15x
* **Changes:** Pushed the `delay_ms` parameter lower to find the breaking point of the jitter buffer. 
* **Why:** To maximize the final score. At 30ms delay on Profile B, the misses started creeping over 1.0% due to extreme network jitter. 40ms is incredibly safe, 35ms is optimal.
