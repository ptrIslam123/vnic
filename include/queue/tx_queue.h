#pragma once

#include "include/queue/queue.h"

namespace vnic {

class TxQueue final : public Queue {
public:
    struct Config : Queue::Config {};
};

} // namespace vnic
