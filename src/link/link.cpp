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
    return true;
}

bool Link::up() {
    return true;
}

bool Link::down() {
    return true;
}

} // namespace vnic
