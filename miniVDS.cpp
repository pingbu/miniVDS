#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <map>
#include <net/if.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#define MTU   5000

static std::string format(const char *fmt, ...) {
    char t[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(t, sizeof(t), fmt, args);
    va_end(args);
    return t;
}

class Tap {
    int fd;
    std::string name;

public:
    Tap(const char* if_name = nullptr) {
        fd = ::open("/dev/net/tun", O_RDWR);
        if (fd < 0)
            throw std::runtime_error("Open TUN failed.");

        ifreq ifr = {0};
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        if (if_name)
            ::strcpy(ifr.ifr_name, if_name);
        int r = ::ioctl(fd, TUNSETIFF, &ifr);
        if (r < 0)
            throw std::runtime_error(format("Create TUN device failed, errno=%d", errno));
        name = ifr.ifr_name;

        int s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0)
            throw std::runtime_error("Error create socket");

        ::bzero(&ifr, sizeof(ifr));
        ::strcpy(ifr.ifr_name, name.c_str());
        r = ::ioctl(s, SIOCGIFFLAGS, &ifr);
        if (r < 0)
            throw std::runtime_error(format("Error get flags, errno=%d", errno));

        ifr.ifr_ifru.ifru_flags |= IFF_UP;
        r = ::ioctl(s, SIOCSIFFLAGS, &ifr);
        if (r < 0)
            throw std::runtime_error(format("Error set flags, errno=%d", errno));
    }

    ~Tap() {
        ::close(fd);
    }

    size_t read(void *buf, size_t size) {
        int r = ::read(fd, buf, size);
        if (r < 0)
            throw std::runtime_error(format("Error read, errno=%d", errno));
        return (size_t) r;
    }

    void write(const void *buf, size_t size) {
        ::write(fd, buf, size);
    }
};

class Udp {
    int fd;

public:
    Udp(int port) {
        fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0)
            throw std::runtime_error("Error create socket");

        int v = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *) &v, sizeof(v));

        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        int r = ::bind(fd, (const sockaddr *) &addr, sizeof(addr));
        if (r != 0)
            throw std::runtime_error("Bind UDP socket failed.");
    }

    ~Udp() {
        ::close(fd);
    }

    int recv(void *buf, size_t size, sockaddr_in *peer) {
        socklen_t n = sizeof(sockaddr_in);
        int r = ::recvfrom(fd, buf, size, 0, (sockaddr *) peer, &n);
        if (r < 0)
            throw std::runtime_error(format("Error recv, errno=%d", errno));
        return (size_t) r;
    }

    void send(const void *buf, size_t size, const sockaddr_in *peer) {
        ::sendto(fd, buf, size, 0, (const sockaddr *) peer, sizeof(sockaddr_in));
    }
};

static uint64_t upTime() {
    timespec t = {0};
    ::clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

struct Peer {
    sockaddr_in addr;
    uint64_t expires = UINT64_MAX;

    Peer() = default;

    Peer(const sockaddr_in &addr) {
        this->addr = addr;
    }

    bool expired() {
        if (upTime() >= expires)
            return true;
        if (expires == UINT64_MAX)
            expires = upTime() + 5000; // expires after 5 seconds since first calling
        return false;
    }
};

class miniVDS {
    Tap tap;
    Udp udp;
    sockaddr_in broadcast = {0};
    std::map <uint64_t, Peer> peers;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    void transformIn() {
        for (uint8_t buf[MTU];;) {
            sockaddr_in from = {0};
            int r = udp.recv(buf, MTU, &from);
            pthread_mutex_lock(&mutex);
            printf("[%d bytes] %02x:%02x:%02x:%02x:%02x:%02x <-- %02x:%02x:%02x:%02x:%02x:%02x (%s)\n", r,
                   buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
                   inet_ntoa(from.sin_addr));
            if ((buf[6] & 1) == 0) {
                uint64_t addr = 0;
                memcpy(&addr, buf + 6, 6);
                peers[addr] = Peer(from);
            }
            pthread_mutex_unlock(&mutex);
            tap.write(buf, r);
        }
    }

    void transformOut() {
        for (uint8_t buf[MTU];;) {
            int r = tap.read(buf, MTU);
            pthread_mutex_lock(&mutex);
            if ((buf[0] & 1) == 0) {
                uint64_t addr = 0;
                memcpy(&addr, buf, 6);
                auto peer = peers.find(addr);
                if (peer != peers.end() && !peer->second.expired()) {
                    printf("[%d bytes] %02x:%02x:%02x:%02x:%02x:%02x --> %02x:%02x:%02x:%02x:%02x:%02x (%s)\n", r,
                           buf[6], buf[7], buf[8], buf[9], buf[10], buf[11], buf[0], buf[1], buf[2], buf[3], buf[4],
                           buf[5], inet_ntoa(peer->second.addr.sin_addr));
                    pthread_mutex_unlock(&mutex);
                    udp.send(buf, r, &peer->second.addr);
                    continue;
                }
            }
            printf("[%d bytes] %02x:%02x:%02x:%02x:%02x:%02x --> %02x:%02x:%02x:%02x:%02x:%02x (broadcast)\n", r,
                   buf[6], buf[7], buf[8], buf[9], buf[10], buf[11], buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
            pthread_mutex_unlock(&mutex);
            udp.send(buf, r, &broadcast);
        }
    }

    static void *threadProc(void *param) {
        ((miniVDS *) param)->transformIn();
        return nullptr;
    }

public:
    miniVDS(const char* name, int port) : tap(name), udp(port) {
        if (name)
            printf("Use TAP device %s\n", name);
        printf("Use UDP port %u\n", port);
        broadcast.sin_family = AF_INET;
        broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        broadcast.sin_port = htons(port);
    }

    void run() {
        pthread_t tid = 0;
        int r = ::pthread_create(&tid, nullptr, threadProc, this);
        if (r != 0)
            throw std::runtime_error("pthread_create FAILED!");
        ::pthread_detach(tid);

        transformOut();
    }
};

int main(int argc, char *argv[]) {
    printf("miniVDS v1.1 - https://github.com/pingbu/miniVDS/\n");
    bool daemon = false;
    const char* name = nullptr;
    int port = 4444;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
            daemon = true;
        else if (strncmp(argv[i], "-i=", 3) == 0)
            name = argv[i] + 3;
        else if (strncmp(argv[i], "-p=", 3) == 0)
            port = (int) strtol(argv[i] + 3, nullptr, 10);
        else if (strncmp(argv[i], "--port=", 7) == 0)
            port = (int) strtol(argv[i] + 7, nullptr, 10);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("  -d, --daemon      Run in background.\n"
                   "  -h, --help        Show this document.\n"
                   "  -i, --itf=ITF     Force the TAP interface name.\n"
                   "  -p, --port=PORT   Set the UDP port. Default port is 4444.\n"
                   "                    Each port represent a virtual switch. Endpoints using different ports are not connected.\n");
            return 0;
        } else {
            printf("Error: invalid parameter %s\n", argv[i]);
            return -1;
        }
    if (daemon) {
        printf("Running as a daemon.\n");
        int r = ::daemon(0, 0);
        if (r != 0)
            throw std::runtime_error("daemon FAILED!");
    }
    miniVDS(name, port).run();
    return 0;
}
