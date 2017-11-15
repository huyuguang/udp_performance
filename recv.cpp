#include "public.h"

extern std::atomic<bool> closed;
extern long cpus;

// can also check in /proc/pid/net/udp
//[huyuguang@dtrans1 ~]$ cat / proc / 42120 / net / udp
//sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops
//6486 : 00000000 : 0FA0 00000000 : 0000 07 00000000 : 00000000 00 : 00000000 00000000  3017        0 132370440 2 0000000000000000 95253
//the 95253 is the drop count
bool get_overflow(struct msghdr* hdr, uint32_t* dropped_packets) {
    if (hdr->msg_controllen > 0) {
        struct cmsghdr* cmsg;
        for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != nullptr;
            cmsg = CMSG_NXTHDR(hdr, cmsg)) {
            if (cmsg->cmsg_type == SO_RXQ_OVFL) {
                *dropped_packets =
                    *(reinterpret_cast<uint32_t*>CMSG_DATA(cmsg));
                return true;
            }
        }
    }
    return false;
}

#ifndef SO_ATTACH_REUSEPORT_CBPF
#define SO_ATTACH_REUSEPORT_CBPF 51
#define SO_ATTACH_REUSEPORT_EBPF 52
#endif

void attach_bpf(int fd) {
    struct sock_filter code[] = {
        /* A = raw_smp_processor_id() */
        { BPF_LD | BPF_W | BPF_ABS, 0, 0, (uint32_t)(SKF_AD_OFF + SKF_AD_CPU) },
        /* return A */
        { BPF_RET | BPF_A, 0, 0, 0 },
    };

    struct sock_fprog p = {
        2,
        code,
    };

    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &p, sizeof(p)))
        error(1, errno, "failed to set SO_ATTACH_REUSEPORT_CBPF");
}

void build_rcv_group(std::vector<int>& sockets, bool bpf) {
    struct sockaddr_in  addr;
    int opt;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kRecvPort);

    for (auto& s : sockets) {
        s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0)
            error(1, errno, "failed to create receive socket");

        opt = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
            error(1, errno, "failed to set SO_REUSEPORT");

        opt = 128 * 1024 * 1024;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt)))
            error(1, errno, "failed to set SO_RCVBUF");

        if (setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &opt, sizeof(opt)))
            error(0, errno, "failed to set SO_RCVBUFFORCE");

        //if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)))
        //    error(1, errno, "failed to set SO_SNDBUF");

        //if (setsockopt(s, SOL_SOCKET, SO_SNDBUFFORCE, &opt, sizeof(opt)))
        //    error(0, errno, "failed to set SO_SNDBUFFORCE");

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
            (const char*)&tv, sizeof(tv)) < 0) {
            error(1, errno, "failed to set SO_RCVTIMEO");
        }

        opt = 1;
        if (setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL, &opt, sizeof(opt)))
            error(1, errno, "failed to set SO_RXQ_OVFL");

        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)))
            error(1, errno, "failed to bind receive socket");
    }

    if (bpf) {
        attach_bpf(sockets[0]);
    }
}

int sync_recv(int fd, std::vector<char*>& buffers, uint32_t& drop_count) {
    assert(buffers.size() == kRecvCount);
    struct mmsghdr msgs[kRecvCount];
    struct iovec vecs[kRecvCount];
    struct sockaddr_in sas[kRecvCount];
    char cbuf[CMSG_SPACE(sizeof(int)) + 100];

    memset(msgs, 0, sizeof(msgs[0]) * kRecvCount);
    memset(vecs, 0, sizeof(vecs[0]) * kRecvCount);
    memset(sas, 0, sizeof(sas[0]) * kRecvCount);
    memset(cbuf, 0, sizeof(cbuf));

    for (uint32_t i = 0; i < kRecvCount; ++i) {
        vecs[i].iov_base = buffers[i];
        vecs[i].iov_len = kMaxPacketLen;
        msgs[i].msg_hdr.msg_iov = &vecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &sas[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
    }

    struct cmsghdr* cmsg = (struct cmsghdr*)cbuf;
    cmsg->cmsg_len = sizeof(cbuf);
    msgs[0].msg_hdr.msg_control = cmsg;
    msgs[0].msg_hdr.msg_controllen = cmsg->cmsg_len;

    fd_set readfds;
    struct timeval t;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    t.tv_sec = 0;
    t.tv_usec = 100 * 1000;
    if (select(fd + 1, &readfds, NULL, NULL, &t) <= 0)
        return 0;

    if (!FD_ISSET(fd, &readfds))
        return 0;

    int recv_count = recvmmsg(fd, msgs, kRecvCount, MSG_DONTWAIT,
        nullptr);

    if (recv_count < 0) {
        if (errno != EAGAIN) {
            error(1, errno, "recvmmsg");
        }
        return 0;
    }

    get_overflow(&msgs[0].msg_hdr, &drop_count);
    
    return recv_count;
}

int recv_main(bool bpf, bool bindcpu) {
    printf("recv, bpf: %s, bindcpu: %s\n",
        (bpf ? "true" : "false"),
        (bindcpu ? "true" : "false"));

    std::vector<int> sockets(cpus);
    std::vector<std::thread> threads(cpus);
    std::vector<uint64_t> recv_counts(cpus);
    std::vector<uint32_t> drop_counts(cpus);
    std::atomic<uint64_t> total_recv_count{ 0 };
    std::atomic<uint64_t> total_drop_count{ 0 };

    build_rcv_group(sockets, bpf);

    for (uint32_t i = 0; i < threads.size(); ++i) {
        threads[i] = std::thread([i, &sockets, &recv_counts, &drop_counts,
            &total_recv_count, &total_drop_count, bindcpu]() mutable {
            if (bindcpu) {
                bind_cpu(i);
            }
            auto fd = sockets[i];
            auto& recv_count = recv_counts[i];
            auto& drop_count = drop_counts[i];
            std::string name = "RCV_" + std::to_string(i);
            pthread_setname_np(pthread_self(), name.c_str());
            std::vector<char*> buffers(kRecvCount);
            for (auto& buffer : buffers) {
                buffer = new char[kMaxPacketLen];
            }
            while (!closed) {
                uint32_t save_count = drop_count;
                auto new_recv_count = sync_recv(fd, buffers, drop_count);
                recv_count += new_recv_count;
                total_recv_count += new_recv_count;
                if (drop_count) {
                    if (drop_count < save_count) {
                        error(1, errno, "oops: %d < %d\n", drop_count,
                            save_count);
                    }

                    auto new_drop_count = drop_count - save_count;
                    total_drop_count += new_drop_count;
                }
            }
            for (auto buffer : buffers) {
                delete[] buffer;
            }
        });
    }
    
    uint64_t period = 3;
    while (!closed) {
        printf("total_recv_count: %" PRIu64 ", total_drop_count: %" PRIu64 "\n",
            total_recv_count.load()/ period, total_drop_count.load()/ period);
        total_recv_count = 0;
        total_drop_count = 0;
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
        auto recv_count = recv_counts[i];
        auto drop_count = drop_counts[i];
        printf("thread %ld, recv_count: %" PRIu64 ", drop_count: %u\n",
            i, recv_count, drop_count);
    }
    printf("total_recv_count: %" PRIu64 ", total_drop_count: %" PRIu64 "\n",
        total_recv_count.load(), total_drop_count.load());
    return 0;
}