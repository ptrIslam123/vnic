# NIC Hardware Emulation Architecture

This document describes the architectural approach to software emulation of a network interface card (NIC) that closely follows the behavior of real hardware. The goal is a library that reproduces NIC operation at a level sufficient for high-quality testing and development of network applications while maintaining performance comparable to hardware solutions.

## 0.1.0 implementation status

This document describes the target architecture of VNic 0.1.0 (C++23). In version 0.1.x some decisions are still simplified:

- **Link** is implemented as an in-memory pipe only.
- **Copies:** the RX path copies the frame in `Link::putRx()`, then in `Link::takeRx()`, and into pool segments in `RxQueue::put()`; the TX path copies during linearization in `TxQueue::put()` and in `Link::putTx()`. Zero-copy is a goal, not the current state.
- **Memory:** `memory::Pool` and `memory::Allocator` are `malloc`/`free` stubs; there is no pre-allocation or NUMA affinity.
- **NUMA:** queues and pools are not bound to NUMA nodes; `socketId` is only a reserved configuration field.
- **RETA:** thread safety is provided by `utils::Rcu` backed by `std::shared_mutex`, not by lock-free RCU.
- **FCS:** `Link` carries frames without FCS, so `validate::CheckFcs()` always returns `true`; offloads are not emulated.
- **FIFO:** `Fifo<T>` is built on `std::queue` + `std::mutex` + `condition_variable`, not on a lock-free ring buffer.

---

## Key principles

### 1. Minimize data copies

One of the fundamental requirements is to **minimize the number of packet copies**. Every copy adds load on the CPU, memory bus, and cache. All subsequent operations are performed through pointers.

### 2. Separate descriptors from buffers

A packet is not stored as a single object. Instead, there are **two entities**:

- **Packet descriptor (PacketDescriptor)** — packet metadata: a reference to the buffer, length, timestamp, offload flags, and queue number. It has a fixed size.
- **Packet buffer (PacketBuffer)** — the memory area that stores the payload. It has a fixed size.

**Why this is needed:**
- Fixed buffer sizes simplify memory management.
- The buffer pool pre-allocates memory in fixed-size blocks, eliminating fragmentation.
- Descriptors and buffers live in separate pools and are reused independently.

### 3. Buffer chains for large packets

If a network packet exceeds the size of one buffer, multiple buffers are allocated and linked through the `next` field. One descriptor references the first buffer in the chain, and each subsequent buffer points to the next one.

**Advantages:**
- No copying when splitting into segments.
- Jumbo frames and TSO are handled naturally.
- Small packets (64 bytes) occupy exactly one buffer.

---

## Role of the Link: physical transmission medium

### Concept

The **Link** is the physical medium (cable, fiber) that carries **signals**, not packets. In software emulation, the Link is a **pure byte pipe**. It knows nothing about packets, IP, or TCP/UDP.

**What the Link does (current implementation):**
- Transfers bytes between two endpoints — two in-memory queues (RX and TX).
- Provides `putRx()`/`takeRx()` and `putTx()`/`takeTx()` methods.
- Tracks `up/down` state: while the link is down, the NIC does not consume data.

### takeRx()

`Link::takeRx(packet, timeout)` reads raw bytes from the Link RX queue. It is a **blocking** call with a timeout and returns `std::expected<std::size_t, ErrorCode>`: on success the result holds the number of appended bytes; if no data arrives, it returns `ErrorCode::Timeout`; if the output vector cannot grow, it returns `ErrorCode::LinkAllocationFailed`. `takeTx()` works the same way for the TX queue.

### putRx() / putTx()

`Link::putRx(bytes)` / `Link::putTx(bytes)` writes raw bytes to the corresponding Link queue. It is used to emulate external transmission or to inject packets directly (for example, in tests).

---

## Role of the NIC: processing device

The **NIC (Network Interface Card)** is a device that:
- Receives raw bytes from the Link.
- Converts them into packets (descriptors + buffers).
- Computes the RSS hash.
- Determines the queue number through RETA.
- Places the descriptor into a queue.

The NIC polls the Link through the `poll()` method. The Link has no background thread. The NIC decides when to take data from the Link.

### poll()

`NIC::poll()` is a loop method invoked from the main processing loop.

**What poll() does:**
1. Calls `link_.takeRx()` to obtain raw bytes.
2. If no bytes are available, the loop continues.
3. Parses the frame (`Parser::parse`) and runs validation (`VNic::validate`).
4. Computes the RSS hash (for IP frames).
5. Determines the queue number through RETA.
6. Allocates buffer segments and copies the payload (in 0.1.x this is not the only copy — see “0.1.x implementation status”).
7. Places the descriptor into the queue.

---

## RSS and queue distribution

**RSS (Receive Side Scaling)** is a technology that distributes incoming traffic across multiple queues based on a hash of packet fields (IP addresses, ports, and protocol). Each queue is serviced by its own CPU core, enabling parallel traffic processing.

### How RSS determines the NUMA node

1. RSS computes a hash from the 5-tuple (src IP, dst IP, src port, dst port, protocol).
2. The hash is used as an index into the RETA table.
3. RETA returns the queue number.
4. Each queue is bound to a specific NUMA node (through configuration).
5. The buffer is allocated from that NUMA node’s pool.

---

## Memory management

### Buffer pool

**MemoryPool** is a pool of pre-allocated fixed-size buffers. All buffers are allocated once during initialization and then reused.

**Why a pool:**
- Eliminates memory allocation in the hot path.
- Eliminates memory fragmentation.
- Enables fast buffer allocation and release.

**How it works:**
- RSS determines the queue.
- The queue is bound to a NUMA node.
- The buffer is allocated from that NUMA node’s pool.
- The descriptor is placed into the queue.

**Result:** all data is local to the core that processes the queue.

**Buffer life cycle:**
1. The buffer is allocated from the queue’s NUMA-node pool.
2. Data is copied into the buffer.
3. The descriptor is placed into the queue.
4. The application retrieves the descriptor.
5. The application processes the data.
6. The application returns the descriptor.
7. The buffer is returned to its pool.
8. The buffer becomes available for allocation again.

---

## Complete packet life cycle

### Receive (Rx)

1. **Link::takeRx()** — reads raw bytes from the Link RX queue.
2. **VNic::poll()** — a background thread polls the Link.
3. **Parser::parse()** — parses Ethernet/VLAN/L3/L4.
4. **VNic::validate()** — receive-side checks (size, MAC, VLAN, IPv4 length/checksum).
5. **RSS** — computes a hash from the 5-tuple (IPv4/IPv6); no hash is computed for non-IP frames.
6. **RETA** — determines the queue number (snapshot under RCU).
7. **PacketDescriptor** — fills in `timestamp`, `vlanId`, `hash`, and `queueId`.
8. **RxQueue::put()** — allocates buffer segments and copies the payload (in 0.1.x the frame has already been copied into the Link and into the poll-loop buffer).
9. **Application::rx()** — retrieves descriptors from the queue.
10. **Application** — processes the data.
11. **Application::freeRx()** — returns descriptors and releases the entire buffer chain.

### Transmit (Tx)

1. **Application** — builds a `PacketBuffer` and `PacketDescriptor` chain.
2. **VNic::tx()** — passes descriptors to the TX queue and returns
   `std::expected<std::size_t, ErrorCode>`: a partial burst is a success and
   yields the number transmitted; if nothing could be transmitted because of an
   error, the error is returned.
3. **TxQueue::put()** — linearizes the chain into a contiguous buffer (one copy).
4. **Link::putTx()** — writes bytes to the Link TX queue.
5. **Receiver::takeTx()** — reads bytes from the TX queue.
