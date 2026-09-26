#pragma once

#include <string>
#include <span>
#include <thread>
#include <chrono>

#include <cstdint>

namespace vnic {

/*
этот класс реализует сущность канала.
она создает virt пару, первый конец доступен для того чтобы в него можно было класть пакеты.
Второй конец будет принадлежать этому классу, он будет его слушать на наличие income пакетов, и затем отдавать NIC.
Во врутренний его fifo можно класть как из второго конца virt пары, так и просто вызвать у него метод put что должно положить данные чтобы они стали доступны для NIC
*/
class Link final {
public:
    struct Config {
        bool useVeth;
        std::string vethIn;
        std::string vethOut;

        enum class Duplex : std::uint8_t {
            Full, Half
        } duplex = Duplex::Full;
        bool autoNegotiation;
        std::uint32_t speedMbps;
    };

    bool configure(const Config& config);
    bool up();
    bool down();
    void write(std::span<std::byte> data);
    std::size_t receive(
        std::vector<std::byte>& buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{100}
    );
    void waitForBandwidth(std::size_t bytes);

private:
    bool constructVethPair(const std::string& vethIn, const std::string& vethOut);
    bool destructVethPair(const std::string& vethIn, const std::string& vethOut);
    int openVeth(const std::string& veth);

    std::uint64_t speedBytesPerSecond_;
    std::uint64_t lastReadTime_;
    int fd_;
    Config config_;
};

} // namespace vnic
