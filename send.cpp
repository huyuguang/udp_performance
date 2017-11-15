#include "public.h"

extern std::atomic<bool> closed;
extern long cpus;

const int kSocketFactor = 4;

void build_send_group(std::vector<int>& sockets, bool single_src_port) {
    struct sockaddr_in addr;
    int opt;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);    

    uint16_t port = kSendPort;

    for (auto& s : sockets) {
        s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0)
            error(1, errno, "failed to create receive socket");

        if (single_src_port) {
            opt = 1;
            if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
                error(1, errno, "failed to set SO_REUSEPORT");            
        } else {
            ++port;
        }

        opt = 10 * 1024 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)))
            error(1, errno, "failed to set SO_SNDBUF");

        if (setsockopt(s, SOL_SOCKET, SO_SNDBUFFORCE, &opt, sizeof(opt)))
            error(0, errno, "failed to set SO_SNDBUFFORCE");

        addr.sin_port = htons(port);
        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)))
            error(1, errno, "failed to bind send socket");
    }
}

int sync_send(int fd, uint32_t dst_ip, char const* packet_buf,
    uint16_t packet_len) {
    struct mmsghdr msgs[kSendCount];
    struct iovec vecs[kSendCount];
    struct sockaddr_in sas[kSendCount];

    memset(msgs, 0, sizeof(msgs[0]) * kSendCount);
    memset(vecs, 0, sizeof(vecs[0]) * kSendCount);
    memset(sas, 0, sizeof(sas[0]) * kSendCount);

    for (uint32_t i = 0; i < kSendCount; ++i) {
        mmsghdr& msg = msgs[i];
        iovec& vec = vecs[i];
        sockaddr_in& sa = sas[i];

        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(dst_ip);
        sa.sin_port = htons(kRecvPort);

        msg.msg_hdr.msg_name = &sa;
        msg.msg_hdr.msg_namelen = sizeof(sockaddr_in);
        msg.msg_hdr.msg_iov = &vec;
        msg.msg_hdr.msg_iovlen = 1;

        vec.iov_base = (char*)packet_buf;
        vec.iov_len = packet_len;
    }

    int send_count = sendmmsg(fd, msgs, kSendCount, 0);
    if (send_count < 0) {
        error(1, errno, "sendmmsg failed");
    }
    return send_count;
}

int send_main(uint32_t dst_ip, uint16_t packet_len, bool single_src_port,
    bool bindcpu) {
    printf("send, packet_len: %d, single_src_port: %s, bindcpu: %s\n",
        (int)packet_len, (single_src_port ? "true" : "false"),
        (bindcpu ? "true" : "false"));
    std::vector<int> sockets(cpus * kSocketFactor);
    std::vector<std::thread> threads(cpus);
    std::vector<uint64_t> send_counts(cpus);
    std::atomic<uint64_t> total_send_count{ 0 };

    build_send_group(sockets, single_src_port);

    for (uint32_t i = 0; i < threads.size(); ++i) {
        threads[i] = std::thread([i, &sockets, &send_counts, bindcpu,
            &total_send_count, dst_ip, packet_len]() mutable {
            if (bindcpu) {
                bind_cpu(i);
            }
            auto* fds = &sockets[i*kSocketFactor];
            auto& send_count = send_counts[i];
            std::string name = "SND_" + std::to_string(i);
            pthread_setname_np(pthread_self(), name.c_str());
            char dummy_packet[kMaxPacketLen] = { 0 };
            dummy_packet[0] = (char)i;
            uint64_t fd_index = 0;
            while (!closed) {                
                auto fd = fds[fd_index];
                ++fd_index;
                fd_index %= kSocketFactor;
                auto new_send_count = sync_send(fd, dst_ip, dummy_packet,
                    packet_len);
                send_count += new_send_count;
                total_send_count += new_send_count;
            }
        });
    }

    uint64_t period = 3;
    while (!closed) {
        printf("total_send_count: %" PRIu64 "\n",
            total_send_count.load()/period);
        total_send_count = 0;
        usleep(period * 1000 * 1000);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (auto s : sockets) {
        close(s);
    }

    printf("===\n");
    for (long i = 0; i < cpus; ++i) {
        auto send_count = send_counts[i];
        printf("thread %ld, send_count: %" PRIu64 "\n", i, send_count);
    }
    printf("total_send_count: %" PRIu64 "\n", total_send_count.load());
    return 0;
}