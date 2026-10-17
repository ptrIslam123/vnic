#pragma once

#include "link/link.h"
#include "stats/stats.h"
#include "utils/state_ful.h"
#include "packet/packet_descriptor.h"

#include <span>

namespace vnic {

class TxQueue final : public utils::Stateful<TxQueue> {
public:
    struct Config {};

    bool put(PacketDescriptor&& desc);

    const Config& getConfig() const;
    const stats::Stats& getStats() const;

private:
    friend utils::Stateful<TxQueue>;

    bool configureImpl(const Config& config, Link& link);
    bool startImpl();
    bool stopImpl();

    Link* link_{};
    stats::Stats stats_;
    Config config_;

};

} // namespace vnic
