# VNic — Userspace NIC Emulation

**VNic** is a library for emulating a network interface card (NIC) in user space. It reproduces the behavior of real network hardware: RX/TX queues, descriptors, RSS, RETA, and the link — without physical hardware.

The project is inspired by **DPDK**: separation of port and queue configuration, descriptors, and buffer chains. In 0.1.x the Link and buffer pools remain simplified (frames are copied and pools use `malloc`); see “0.1.x limitations”.

---

## Motivation

Developing and testing network applications with DPDK or eBPF faces a fundamental problem: **without a real NIC, system behavior cannot be verified**.

VNic addresses this problem:

| Problem | VNic solution |
|---------|---------------|
| Physical hardware required | Fully software-emulated |
| Hard to test in CI/CD | Runs in user space, no root required |
| Cannot reproduce scenarios (overflows, drops, link down) | Full control over NIC state |
| Expensive and slow testbed deployment | Lightweight C++23 library |

---

**Mission** — make the development and testing of high-performance network systems accessible without physical hardware.

- An engineer must be able to **reproduce any NIC behavior** in unit tests.
- **The emulation must not fall short of real hardware** in architectural realism.
- **Testing network applications** must be as simple as testing ordinary backend code.

---

## Key features

Implemented in 0.1.0:

- **RX/TX queues** built on FIFO and descriptors
- **Frame parser**: Ethernet, VLAN (802.1Q/802.1AD), IPv4, IPv6, ARP, TCP/UDP/ICMP
- **RSS** with configurable hash functions (Toeplitz, XOR, CRC32) for IPv4 and IPv6
- **RETA** (Redirection Table): power-of-two validation and thread-safe updates via `utils::Rcu` (in 0.1.x backed by `std::shared_mutex`)
- **Receive-side checks**: frame size (runts/MTU), destination MAC filter, VLAN filter, IPv4 total length, and checksum. FCS is not checked in 0.1.x: `Link` carries frames without FCS, and `validate::CheckFcs()` is a stub
- **Packet chains** (`next`) for jumbo frames
- **Link** (in-memory): up/down state and configurable RX/TX queue depth and slot size
- **Statistics** for the NIC (RX and TX) and for each queue separately
- **Descriptor and buffer-based operation**
- **Unified error handling**: native `std::expected<T, ErrorCode>` / `std::expected<void, ErrorCode>`, with the failing component’s error propagated up the chain

Planned (see `dock/architecture.md`):

- **Buffer pools** with pre-allocation (currently a `malloc` stub)
- **NUMA-aware** pools and queue-to-NUMA-node affinity
- **Offloads** (checksum/TSO/VLAN offload)

### 0.1.x limitations

- The Link is in-memory only; there is no kernel/netlink backend.
- End-to-end zero-copy is not implemented: the Link and queues copy frames, and pools use `malloc`.
- FCS is not checked (`validate::CheckFcs()` is a stub).
- `utils::Rcu<T>` uses `std::shared_mutex`, not a lock-free scheme.

---

## Dependencies

There are no external dependencies: only a C++23 compiler and CMake ≥ 3.24 are required. GoogleTest is fetched automatically (only when building tests).

The supported platform is Linux with GCC or Clang: public headers use Linux headers (`<linux/...>`, `<net/if_arp.h>`), and counters and state use built-in `__atomic_*` operations.

## Building

```bash
git clone https://github.com/ptrIslam123/vnic.git
cd vnic
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Tests

The suite contains GoogleTest unit tests.

```bash
ctest --test-dir build --output-on-failure
```

To check memory safety and undefined behavior:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVNIC_SANITIZE=ON
cmake --build build-asan -j"$(nproc)"
ctest --test-dir build-asan --output-on-failure
```

## Test coverage

Coverage is collected with gcov and the report is generated with lcov/genhtml. The `gcov`, `lcov`, and `genhtml` tools must be installed. On Debian/Ubuntu:

```bash
sudo apt install lcov
```

Build with coverage and run the tests:

```bash
cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DVNIC_COVERAGE=ON
cmake --build build-coverage -j"$(nproc)"
cmake --build build-coverage --target coverage
```

The `coverage` target clears counters, runs `ctest`, and collects coverage only for the `vnic` library; system headers, GoogleTest, tests, and examples are excluded from the report. If `lcov`/`genhtml` are not found, CMake prints a warning and does not create the `coverage` target.

View the HTML report:

```bash
xdg-open build-coverage/coverage-html/index.html
# or without a GUI:
python3 -m http.server -d build-coverage/coverage-html 8000
# http://localhost:8000
```

Console summary:

```bash
lcov --summary build-coverage/coverage.filtered.info
```

The HTML report shows line by line which parts of `src/` and `include/` are not covered by tests.

## Installation and integration

```bash
cmake --install build --prefix /usr/local
```

The installation includes the library, public headers, CMake package config, and `LICENSE` and `README.md` under `share/doc/vnic`.

Then, in your project:

```cmake
find_package(vnic 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE vnic::vnic)
```

## Documentation

- `dock/architecture.md` — architecture and subsystem design.
- `TODO.md` — known limitations and planned architectural work.

API documentation is generated from Doxygen comments in the headers (`include/`).
Building it requires `doxygen`:

```bash
cmake -S . -B build -DVNIC_BUILD_DOCS=ON
cmake --build build --target vnic-docs
# output: dock/api/html/index.html
```

Or directly: `doxygen Doxyfile`.

> **Important:** the `vnic-docs` target is not part of the default build, so the documentation is **not** regenerated on every build (`cmake --build build`). Doxygen runs only when the target is requested explicitly (`--target vnic-docs`) and only if the project was configured with `-DVNIC_BUILD_DOCS=ON`. To regenerate automatically, add `add_dependencies(vnic vnic-docs)` to `CMakeLists.txt`.
