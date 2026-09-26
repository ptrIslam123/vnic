#include "link/link.h"
#include "utils/scoped_guard.h"
#include "clock/clock.h"

#include <netlink/netlink.h>
#include <netlink/route/link/veth.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/ethernet.h>

#include <cassert>
#include <cstring>

namespace vnic {

bool Link::configure(const Config& config) {
    config_ = config;

    if (config_.useVeth) {
        (void)destructVethPair(config_.vethIn, config_.vethOut);
        if (!constructVethPair(config_.vethIn, config_.vethOut)) {
            return false;
        }
    }

    fd_ = openVeth(config_.vethOut);
    if (fd_ < 0) {
        return false;
    }

    // Мбит/с -> байт/с
    speedBytesPerSecond_ = static_cast<std::uint64_t>(config_.speedMbps * 1'000'000 / 8);

    // Half duplex -> делим пропускную способность пополам
    if (config_.duplex == Config::Duplex::Half) {
        speedBytesPerSecond_ /= 2;
    }
    return true;
}

void Link::waitForBandwidth(std::size_t bytes) {
    while (true) {
        const auto now = clock::monotonic::Now();
        const auto dtNs = now - lastReadTime_;

        // Сколько байт можно считать за dtNs?
        const auto bytesAllowed = (speedBytesPerSecond_ * dtNs) / 1'000'000'000;

        if (bytesAllowed >= bytes) {
            // Хватает — обновляем lastWriteTime
            lastReadTime_ = now;
            return;
        }

        // Не хватает — ждём
        const auto bytesNeeded = bytes - bytesAllowed;
        const auto waitNs = (bytesNeeded * 1'000'000'000) / speedBytesPerSecond_;
        std::this_thread::sleep_for(std::chrono::nanoseconds{waitNs});
    }
}

void Link::write(std::span<std::byte> data) {
    // TODO нужно записать в дескрпитор чтобы читающая сторона смогла получить пакет
}

std::size_t Link::receive(
    std::vector<std::byte>& buffers,
    std::chrono::milliseconds timeout
) {
    // TODO пытаться прочить из дескритпора пакеты с конца вирт пары
    return 0;
}

bool Link::up() {
    return true;
}

bool Link::down() {
    return true;
}

int Link::openVeth(const std::string& veth) {
    const int fd{socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))};
    if (fd < 0) {
        return -1;
    }

    utils::ScopedGuard guard{[fd]() noexcept { close(fd); }};
    const unsigned int ifindex{if_nametoindex(veth.c_str())};
    if (ifindex == 0) {
        return -1;
    }

    sockaddr_ll sll{};
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex  = static_cast<int>(ifindex);
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) {
        return -1;
    }

    guard.cancel();
    return fd;
}

bool Link::constructVethPair(const std::string& vethIn, const std::string& vethOut) {
    assert(!vethIn.empty() && !vethOut.empty());
    auto socket{nl_socket_alloc()};
    if (!socket) [[unlikely]] {
        return false;
    }

    utils::ScopedGuard guard{[socket]() noexcept { nl_socket_free(socket); }};
    if (nl_connect(socket, NETLINK_ROUTE) < 0) [[unlikely]] {
        return false;
    }

    const auto ret{rtnl_link_veth_add(socket, vethIn.c_str(), vethOut.c_str(), getpid())};
    return ret >= 0;
}

bool Link::destructVethPair(const std::string& vethIn, const std::string& vethOut) {
    assert(!vethIn.empty() && !vethOut.empty());

    auto socket{nl_socket_alloc()};
    if (!socket) [[unlikely]] {
        return false;
    }

    utils::ScopedGuard socketGuard{[socket]() noexcept {
        nl_socket_free(socket);
    }};

    if (nl_connect(socket, NETLINK_ROUTE) < 0) [[unlikely]] {
        return false;
    }

    struct nl_cache* linkCache{nullptr};
    if (rtnl_link_alloc_cache(socket, AF_UNSPEC, &linkCache) < 0) [[unlikely]] {
        return false;
    }

    utils::ScopedGuard cacheGuard{[linkCache]() noexcept {
        nl_cache_free(linkCache);
    }};

    const auto ifindex{rtnl_link_name2i(linkCache, vethIn.c_str())};
    if (ifindex == 0) {
        // Интерфейс уже удалён — не ошибка
        return true;
    }

    auto link{rtnl_link_alloc()};
    if (!link) [[unlikely]] {
        return false;
    }

    utils::ScopedGuard linkGuard{[link]() noexcept {
        rtnl_link_put(link);
    }};

    rtnl_link_set_ifindex(link, ifindex);
    return rtnl_link_delete(socket, link) >= 0;
}

} // namespace vnic
