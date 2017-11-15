#include "public.h"

std::atomic<bool> closed{ false };
long cpus = 0;

int usage() {
    printf("recv bpf(1|0) bindcpu(1|0)\n");
    printf("send dst_ip packet_len single_src_port(1|0) bindcpu(1|0)\n");
    return 1;
}

int recv_main(bool bpf, bool bindcpu);

int send_main(uint32_t dst_ip, uint16_t packet_len, bool single_src_port,
    bool bindcpu);

void sigexit_handler(int /*sig*/) {
    closed = true;
}

void init_signal() {
    /*
    * ignore SIGPIPE signals; we can use errno==EPIPE if we
    * need that information
    */
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1
        || sigaction(SIGPIPE, &sa, 0) == -1
        || sigaction(SIGHUP, &sa, 0) == -1
        ) {
        perror("failed to ignore SIGPIPE; sigaction");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigexit_handler;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

int main(int argc, char** argv) {
    if (argc < 2)
        return usage();

    init_signal();

    cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus <= 0) {
        error(1, errno, "failed get cpus");
    }
    printf("cpus: %ld\n", cpus);

    if (!strcmp(argv[1], "recv") && argc == 4) {
        auto bpf = atoi(argv[2]) ? true : false;
        auto bindcpu = atoi(argv[3]) ? true : false;
        return recv_main(bpf, bindcpu);
    }

    if (!strcmp(argv[1], "send") && argc == 6) {
        uint32_t dst_ip = ntohl(inet_addr(argv[2]));
        uint16_t packet_len = (uint16_t)atoi(argv[3]);
        if (!packet_len || packet_len > kMaxPacketLen)
            error(1, 0, "invalid packet_len: %d", packet_len);
        bool single = atoi(argv[4]) ? true : false;
        bool bindcpu = atoi(argv[5]) ? true : false;
        return send_main(dst_ip, packet_len, single, bindcpu);
    }

    return usage();
}