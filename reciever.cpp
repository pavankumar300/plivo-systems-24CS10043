#include <arpa/inet.h>
#include <iostream>
#include <vector>
#include <map>
#include <unordered_set>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

#pragma pack(push, 1)
struct WireHeader {
    uint8_t type; 
    uint32_t seq; 
    uint8_t fec_size;
};
#pragma pack(pop)

struct FECBlock {
    uint8_t size;
    std::vector<uint8_t> parity;
};

// Helper for high-res time
double get_time_s() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
    double t0 = getenv("T0") ? atof(getenv("T0")) : get_time_s();
    double delay_ms = getenv("DELAY_MS") ? atof(getenv("DELAY_MS")) : 60.0;
    
    // 1. Socket Setup
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr));

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player{};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    int fb_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in fb_dest{};
    fb_dest.sin_family = AF_INET;
    fb_dest.sin_port = htons(47003);
    fb_dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 2. State Management Data Structures
    std::map<uint32_t, std::vector<uint8_t>> packet_buffer; // Unplayed packets
    std::map<uint32_t, std::vector<uint8_t>> history;       // Kept briefly for FEC calculation
    std::unordered_set<uint32_t> seen_seqs;                 // Deduplication
    std::map<uint32_t, FECBlock> fec_blocks;                // Stored parity data
    
    uint32_t expected_seq = 0;
    
    // Stats for Feedback Loop
    int stats_received = 0;
    int stats_min_seq = -1, stats_max_seq = -1;
    double last_fb_time = get_time_s();

    // Helper Lambda: Try to recover a missing packet in a specific FEC group
    auto try_fec_recovery = [&](uint32_t base_seq) {
        if (fec_blocks.find(base_seq) == fec_blocks.end()) return;
        FECBlock& block = fec_blocks[base_seq];
        
        int found_count = 0;
        uint32_t missing_seq = 0;
        
        for (uint32_t s = base_seq; s < base_seq + block.size; ++s) {
            if (seen_seqs.count(s)) found_count++;
            else missing_seq = s;
        }

        // If exactly one packet is missing, we can recover it
        if (found_count == block.size - 1 && missing_seq >= expected_seq) {
            std::vector<uint8_t> recovered = block.parity;
            for (uint32_t s = base_seq; s < base_seq + block.size; ++s) {
                if (s != missing_seq) {
                    const auto& data = history.count(s) ? history[s] : packet_buffer[s];
                    if (!data.empty()) {
                        for (size_t i = 0; i < 160; ++i) recovered[i] ^= data[i];
                    }
                }
            }
            packet_buffer[missing_seq] = recovered;
            history[missing_seq] = recovered;
            seen_seqs.insert(missing_seq);
        }
    };

    while (true) {
        double now = get_time_s();
        
        // Adaptive Feedback Mechanism (Every 500ms)
        if (now - last_fb_time > 0.5) {
            if (stats_max_seq > stats_min_seq && stats_min_seq != -1) {
                int expected = stats_max_seq - stats_min_seq + 1;
                double loss_rate = 1.0 - ((double)stats_received / expected);
                
                uint8_t target_fec = 10; // Good network
                if (loss_rate > 0.15) target_fec = 2; // Heavy loss
                else if (loss_rate > 0.08) target_fec = 4; // Moderate
                else if (loss_rate > 0.02) target_fec = 6; // Mild
                
                sendto(fb_fd, &target_fec, 1, 0, (struct sockaddr *)&fb_dest, sizeof(fb_dest));
            }
            stats_received = 0; stats_min_seq = -1; stats_max_seq = -1;
            last_fb_time = now;
        }

        // A. Strict Deadline Jitter Buffer Calculation
        // Play out ~3ms before the harness deadline to account for local processing time.
        double target_playout = t0 + (delay_ms / 1000.0) + (expected_seq * 0.020) - 0.003;
        double wait_time = target_playout - now;

        struct timeval tv{};
        if (wait_time > 0) {
            tv.tv_sec = (int)wait_time;
            tv.tv_usec = (int)((wait_time - tv.tv_sec) * 1000000);
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(in_fd, &readfds);

        int ret = select(in_fd + 1, &readfds, nullptr, nullptr, wait_time > 0 ? &tv : nullptr);

        // B. Process Incoming Packets
        if (ret > 0 && FD_ISSET(in_fd, &readfds)) {
            unsigned char buf[2048];
            ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, nullptr, nullptr);
            if (n >= (ssize_t)sizeof(WireHeader)) {
                WireHeader* header = reinterpret_cast<WireHeader*>(buf);
                uint32_t seq = ntohl(header->seq);
                
                if (header->type == 0 && n == sizeof(WireHeader) + 160) {
                    // It's a Data Packet
                    if (seen_seqs.find(seq) == seen_seqs.end()) { // Deduplication
                        seen_seqs.insert(seq);
                        
                        // Update Feedback Stats
                        stats_received++;
                        if (stats_min_seq == -1 || seq < (uint32_t)stats_min_seq) stats_min_seq = seq;
                        if ((int)seq > stats_max_seq) stats_max_seq = seq;

                        std::vector<uint8_t> payload(buf + sizeof(WireHeader), buf + n);
                        if (seq >= expected_seq) {
                            packet_buffer[seq] = payload;
                        }
                        history[seq] = payload;
                        
                        // Check if this new packet completes an existing FEC block
                        uint32_t suspected_base = seq - (seq % header->fec_size); 
                        // Note: Our sender uses dynamic sizes, so we check recently known bases
                        for (auto it = fec_blocks.rbegin(); it != fec_blocks.rend(); ++it) {
                            if (seq >= it->first && seq < it->first + it->second.size) {
                                try_fec_recovery(it->first);
                                break;
                            }
                        }
                    }
                } 
                else if (header->type == 1 && n == sizeof(WireHeader) + 160) {
                    // It's an FEC Packet
                    std::vector<uint8_t> parity(buf + sizeof(WireHeader), buf + n);
                    fec_blocks[seq] = {header->fec_size, parity};
                    try_fec_recovery(seq); // Immediate recovery check
                }
            }
        }

        // C. Jitter Buffer Playout Flush
        // Release all frames whose absolute playout deadline has been crossed.
        now = get_time_s();
        while (now >= target_playout) {
            if (packet_buffer.count(expected_seq)) {
                std::vector<uint8_t> out_pkt(164);
                uint32_t net_seq = htonl(expected_seq);
                memcpy(out_pkt.data(), &net_seq, 4);
                memcpy(out_pkt.data() + 4, packet_buffer[expected_seq].data(), 160);
                
                sendto(out_fd, out_pkt.data(), 164, 0, (struct sockaddr *)&player, sizeof(player));
                packet_buffer.erase(expected_seq);
            }
            
            // Clean up old history to prevent memory leaks (Sliding Window bounding)
            history.erase(expected_seq - 50);
            fec_blocks.erase(expected_seq - 50);
            
            expected_seq++;
            target_playout = t0 + (delay_ms / 1000.0) + (expected_seq * 0.020) - 0.003;
        }
    }
    return 0;
}
