# Experiment Run Log

### Experiment 1: First Valid FEC Run
* [cite_start]**Profile:** A (Mild) 
* [cite_start]**Delay (ms):** 60 [cite: 44]
* [cite_start]**Miss %:** 0.87% (VALID) [cite: 47]
* [cite_start]**Overhead:** 1.45x [cite: 48]
* **Changes:** Ran the complete C++ implementation with adaptive XOR FEC, deduplication, and a strict select() jitter buffer.
* [cite_start]**Why:** To verify the core architecture can survive Profile A's packet loss and jitter within the assignment's overhead and deadline caps[cite: 47, 48]. [cite_start]The protocol successfully recovered enough dropped packets to stay under the 1% miss threshold [cite: 47][cite_start], while the adaptive feedback kept overhead well under 2.0x[cite: 48].

### Experiment 2: Pushing the Delay Limit
* [cite_start]**Profile:** A (Mild) 
* [cite_start]**Delay (ms):** 40 [cite: 44]
* [cite_start]**Miss %:** 10.20% (INVALID) [cite: 47]
* [cite_start]**Overhead:** 1.43x [cite: 48]
* [cite_start]**Changes:** Lowered delay to 40ms to test system limits on the same architecture[cite: 44].
* **Why:** To find the breaking point of the jitter buffer. [cite_start]It failed massively because Profile A has a max network delay of 40ms  (leaving no room for processing)[cite_start], and our FEC block size spans too much time[cite: 53, 55]. [cite_start]If a packet early in the block drops, its 40ms playback deadline expires long before the sender even generates the parity packet[cite: 53, 55].

### Experiment 3: Walking Down the Delay Matrix
* [cite_start]**Profile:** A (Mild) 
* **Delay (ms):** 50
* [cite_start]**Miss %:** 1.27% (INVALID) [cite: 47]
* [cite_start]**Overhead:** 1.53x [cite: 48]
* **Changes:** Increased delay to 50ms to find the mathematical floor of the architecture.
* [cite_start]**Why:** 50ms was agonizingly close (missing the 1% cap by just 4 frames) [cite: 47][cite_start], but it proves that 60ms is the absolute lowest safe delay for our block-based XOR FEC implementation on Profile A[cite: 35, 47]. [cite_start]The network jitter plus the time it takes to build a parity block simply requires slightly more than 50ms of padding[cite: 53, 55].

### Experiment 4: High Jitter Stress Test
* [cite_start]**Profile:** B (Moderate) 
* **Delay (ms):** 80
* [cite_start]**Miss %:** 5.73% (INVALID) [cite: 47]
* [cite_start]**Overhead:** 1.55x [cite: 48]
* [cite_start]**Changes:** Switched to the hostile Profile B network parameters to evaluate robustness under heavier impairments.
* [cite_start]**Why:** Profile B introduces up to 80ms of jitter[cite: 35], which completely breaks the block-based XOR parity scheme. [cite_start]Even though our bandwidth budget is healthy at 1.55x [cite: 48][cite_start], the block delay penalty means the receiver cannot reconstruct dropped early frames before their 80ms playback deadlines elapse[cite: 53, 55].
