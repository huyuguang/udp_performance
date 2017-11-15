#pragma once

#include <cstdio>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <error.h>
#include <string>
#include <memory>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <map>
#include <set>
#include <vector>
#include <array>
#include <atomic>
#include <queue>
#include <chrono>
#include <array>
#include <iterator>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <linux/filter.h>
//#include <linux/in.h>
#include <linux/unistd.h>
#include <sched.h>
#include <string.h>
#include <inttypes.h>

const uint32_t kMaxPacketLen = 1500;
const uint32_t kSendCount = 1000;
const uint32_t kRecvCount = 1000;
const uint16_t kRecvPort = 6666;
const uint16_t kSendPort = 7777;

inline void bind_cpu(int cpu_id) {
    cpu_set_t cpu_set;
    memset(&cpu_set, 0, sizeof(cpu_set));
    CPU_SET(cpu_id, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0)
        error(1, errno, "failed to pin to cpu");
}