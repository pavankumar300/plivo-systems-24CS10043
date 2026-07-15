#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

#pragma pack(push, 1)
struct WireHeader {
    uint8_t type;      // 0 = Data, 1 = FEC Parity
    uint32_t seq;      // Sequence number or Base Sequence for FEC
    uint8_t fec_size;  // How many packets this FEC block covers
};
#pragma pack(pop)

int main() {
    // 1. Setup Incoming Socket from Harness (47010)
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr));

    // 2. Setup Outgoing Socket to Relay (47001)
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay{};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 3. Setup Feedback Socket from Relay (47004)
    int fb_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in fb_addr{};
    fb_addr.sin_family = AF_INET;
    fb_addr.sin_port = htons(47004);
    fb_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(fb_fd, (struct sockaddr *)&fb_addr, sizeof(fb_addr));
    fcntl(fb_fd, F_SETFL, O_NONBLOCK); // Non-blocking reads for feedback

    uint8_t current_fec_size = 6; // Default starting redundancy (moderate)
    std::vector<std::vector<uint8_t>> fec_buffer;
    uint32_t current_base_seq = 0;

    unsigned char buf[2048];

    while (true) {
        // A. Check for receiver feedback (Adaptive Redundancy)
        uint8_t fb_data;
        if (recvfrom(fb_fd, &fb_data, 1, 0, nullptr, nullptr) > 0) {
            current_fec_size = fb_data;
            if (current_fec_size < 2) current_fec_size = 2; // Max protection (50% overhead)
            if (current_fec_size > 12) current_fec_size = 12; // Min protection
        }

        // B. Wait for next audio frame from source
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 4) continue;

        uint32_t seq;
        memcpy(&seq, buf, 4);
        seq = ntohl(seq);

        if (fec_buffer.empty()) {
            current_base_seq = seq;
        }

        // C. Construct and send the Data packet over the wire format
        std::vector<uint8_t> wire_pkt(sizeof(WireHeader) + n - 4);
        WireHeader* header = reinterpret_cast<WireHeader*>(wire_pkt.data());
        header->type = 0;
        header->seq = htonl(seq);
        header->fec_size = current_fec_size;
        memcpy(wire_pkt.data() + sizeof(WireHeader), buf + 4, n - 4);

        sendto(out_fd, wire_pkt.data(), wire_pkt.size(), 0, (struct sockaddr *)&relay, sizeof(relay));

        // D. Build FEC Parity Block
        std::vector<uint8_t> payload(buf + 4, buf + n);
        fec_buffer.push_back(payload);

        if (fec_buffer.size() >= current_fec_size) {
            std::vector<uint8_t> fec_parity(160, 0);
            for (const auto& p : fec_buffer) {
                for (size_t i = 0; i < 160; ++i) {
                    fec_parity[i] ^= p[i]; // XOR parity
                }
            }

            std::vector<uint8_t> fec_pkt(sizeof(WireHeader) + 160);
            WireHeader* fec_hdr = reinterpret_cast<WireHeader*>(fec_pkt.data());
            fec_hdr->type = 1;
            fec_hdr->seq = htonl(current_base_seq);
            fec_hdr->fec_size = current_fec_size;
            memcpy(fec_pkt.data() + sizeof(WireHeader), fec_parity.data(), 160);

            sendto(out_fd, fec_pkt.data(), fec_pkt.size(), 0, (struct sockaddr *)&relay, sizeof(relay));
            fec_buffer.clear();
        }
    }
    return 0;
}
