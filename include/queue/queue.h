#pragma once

#include "queue/fifo.h"
#include "packet/packet.h"
#include "stats/stats.h"

namespace vnic {

class Queue final {
public:
    struct Config {
        std::uint32_t queueId;           // Идентификатор очереди (0, 1, 2, ...)
        std::uint32_t socketId;          // NUMA-узел для выделения памяти
        std::uint32_t size;             // Количество дескрипторов (размер кольцевого буфера)

        struct Thresholds {
            uint32_t prefetch;      // Когда начинать подгружать дескрипторы
            uint32_t host;          // Когда драйвер забирает дескрипторы
            uint32_t writeBack;     // Когда обновлять статус дескрипторов
        } thresholds;

        // --- УПРАВЛЕНИЕ ПАМЯТЬЮ ---
        uint32_t freeThreshold;     // Минимальное кол-во свободных дескрипторов
        // для освобождения буферов
        void* mempool;              // Пул буферов для пакетов (для Rx очереди)
        // Для Tx может быть nullptr
    };

    // Кольцевой буфер (дескрипторы)
    struct Descriptor {
        void* buffer;           // Указатель на буфер п акета
        std::uint32_t length;        // Длина данных в буфере
        std::uint64_t timestamp;     // Временная метка (если поддерживается)
        std::uint32_t flags;         // Статус, оффлоады и т.д.
    };

    bool configure(const Config& config);
    bool start();
    bool stop();

private:
    Fifo<Descriptor> free_;
    Fifo<Descriptor> used_;
    stats::Stats stats_;
};

class RxQueue final {
public:
    struct Config : Queue::Config {
        struct Offloads {
            // Rx оффлоады
            bool checksumIp;
            bool checksumTcp;
            bool checksumUdp;
            bool vlanStrip;
            bool timestamp;
        } offloads;
    };

    bool configure(const Config& config);
    bool start();
    bool stop();
    void push(Packet&& packet);

private:
    Queue queue_;
};

class TxQueue final {
public:
    struct Config : Queue::Config {
        struct Offloads {
            // Tx оффлоады
            bool tso;
            bool vlanInsert;
            bool multiSeg;
        } offloads;
    };

    bool configure(const Config& config);
    bool start();
    bool stop();

private:
    Queue queue_;
};

} // namespace vnic
