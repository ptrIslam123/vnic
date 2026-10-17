# Graph Report - vnic  (2026-10-17)

## Corpus Check
- Corpus is ~9,028 words - fits in a single context window. You may not need a graph.

## Summary
- 512 nodes · 743 edges · 31 communities (29 shown, 2 thin omitted)
- Extraction: 95% EXTRACTED · 5% INFERRED · 0% AMBIGUOUS · INFERRED: 35 edges (avg confidence: 0.87)
- Token cost: 22,000 input · 4,000 output

## Community Hubs (Navigation)
- Packet Header Parsing
- Channel & Buffer Alloc
- Build & Architecture Docs
- Example Byte Utilities
- Protocol Layer Model
- TX Queue & Stats
- Packet Descriptor
- Link Abstraction
- FIFO Queue Utilities
- RSS Hashing
- VNIC Configuration
- VNIC Core Lifecycle
- RX Queue Engine
- Scoped Guard Utility
- Link Implementation
- Stateful State Machine
- VNIC Runtime Ops
- NIC Example App
- VLAN Header Structures
- RETA / RSS Control
- VNIC Configure Impl
- Unit Tests (RSS)
- Poll & Distribute Engine
- Queue Startup Flow
- Clock Utility
- Five-Tuple Extraction

## God Nodes (most connected - your core abstractions)
1. `VNic` - 43 edges
2. `Stats` - 26 edges
3. `Link` - 24 edges
4. `PacketDescriptor` - 22 edges
5. `Fifo` - 21 edges
6. `Parser` - 20 edges
7. `RxQueue` - 18 edges
8. `Channel` - 18 edges
9. `Config` - 14 edges
10. `FiveTuple` - 13 edges

## Surprising Connections (you probably didn't know these)
- `RSS — распределение трафика по очередям через хэш` --semantically_similar_to--> `RSS (Receive Side Scaling) with Toeplitz/XOR/CRC32 hashes`  [INFERRED] [semantically similar]
  dock/архитектура.md → README.md
- `Link — чистая труба для байтов (физическая среда)` --semantically_similar_to--> `Link with configurable speed, duplex, latency`  [INFERRED] [semantically similar]
  dock/архитектура.md → README.md
- `RETA — таблица перенаправления хэш → очередь` --semantically_similar_to--> `RETA (Redirection Table) with power-of-two check`  [INFERRED] [semantically similar]
  dock/архитектура.md → README.md
- `Цепочки буферов (field next) для больших пакетов` --semantically_similar_to--> `Packet chains (next) for Jumbo Frames and TSO`  [INFERRED] [semantically similar]
  dock/архитектура.md → README.md
- `MemoryPool — пул предвыделенных буферов` --semantically_similar_to--> `Memory pool with preallocation`  [INFERRED] [semantically similar]
  dock/архитектура.md → README.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Rx packet lifecycle flow (Link → poll → RSS → RETA → NUMA → pool → queue → app)** — dock_architecture_link_receive, dock_architecture_nic_poll, dock_architecture_rss, dock_architecture_reta, dock_architecture_numa, dock_architecture_buffer_pool_allocate, dock_architecture_rx_queue, dock_architecture_app_rxburst [EXTRACTED 1.00]
- **DPDK-inspired design principles (zero-copy, mempool, descriptor/buffer separation, RSS/RETA)** — readme_md_dpdk, readme_md_zero_copy, readme_md_memory_pool, dock_architecture_descriptor_buffer_separation, dock_architecture_rss, dock_architecture_reta [EXTRACTED 0.95]
- **NIC::poll() processing pipeline** — dock_architecture_nic_poll, dock_architecture_rss, dock_architecture_reta, dock_architecture_numa, dock_architecture_bandwidth_emulation [EXTRACTED 1.00]

## Communities (31 total, 2 thin omitted)

### Community 0 - "Packet Header Parsing"
Cohesion: 0.07
Nodes (42): arphdr, ethhdr, icmphdr, iphdr, ipv6hdr, parseArp, parseIpv4, tcphdr (+34 more)

### Community 1 - "Channel & Buffer Alloc"
Cohesion: 0.06
Nodes (30): uint32_t, PacketBuffer, length, next, Channel, allocator_, config_, lastWriteTime_ (+22 more)

### Community 2 - "Build & Architecture Docs"
Cohesion: 0.08
Nodes (39): base-nic example target, googletest (FetchContent v1.15.2), libnl-3.0 / libnl-route-3.0 dependency, make_example, make_test, unit-tests target, vnic project, Архитектура аппаратной эмуляции NIC (+31 more)

### Community 3 - "Example Byte Utilities"
Cohesion: 0.10
Nodes (21): array, ComputeChecksum(), __be32, byte, size_t, span, string, uint16_t (+13 more)

### Community 4 - "Protocol Layer Model"
Cohesion: 0.06
Nodes (32): L1Protocol, L2Protocol, L3Protocol, uint16_t, uint8_t, L1, eth, protocol (+24 more)

### Community 5 - "TX Queue & Stats"
Cohesion: 0.07
Nodes (18): Config, TxQueue, config_, configureImpl, link_, put, startImpl, stats_ (+10 more)

### Community 6 - "Packet Descriptor"
Cohesion: 0.08
Nodes (29): byte, span, uint16_t, uint32_t, uint64_t, PacketDescriptor, buffer, hash (+21 more)

### Community 7 - "Link Abstraction"
Cohesion: 0.08
Nodes (24): Config, Link, config_, configure, down, put, putRx, putTx (+16 more)

### Community 8 - "FIFO Queue Utilities"
Cohesion: 0.15
Nodes (16): condition_variable, Queue, Fifo, condVar_, mutex_, queue_, wake_, milliseconds (+8 more)

### Community 9 - "RSS Hashing"
Cohesion: 0.13
Nodes (23): FiveTuple, dadr, dport, iproto, sadr, sport, __be16, __be32 (+15 more)

### Community 10 - "VNIC Configuration"
Cohesion: 0.08
Nodes (25): Config, ipAddrs, link, macAddr, mtu, promiscuousEnabled, rss, rxQueueCount (+17 more)

### Community 11 - "VNIC Core Lifecycle"
Cohesion: 0.10
Nodes (19): VNic, config_, configureImpl, eCode_, getErrorCode, link_, resetStats, reta_ (+11 more)

### Community 12 - "RX Queue Engine"
Cohesion: 0.11
Nodes (19): Config, queueId, size, socketId, uint16_t, uint32_t, RxQueue, config_ (+11 more)

### Community 13 - "Scoped Guard Utility"
Cohesion: 0.15
Nodes (13): optional, bool(), optional, T, ScopedGuard, cancel, m_unlockCallback, ScopedGuard<T>::ScopedGuard() (+5 more)

### Community 14 - "Link Implementation"
Cohesion: 0.22
Nodes (14): byte, Config, milliseconds, Queue, size_t, span, vector, Link::configure() (+6 more)

### Community 15 - "Stateful State Machine"
Cohesion: 0.23
Nodes (9): configure(), span, uint8_t, start(), Stateful, state_, stop(), memory_order (+1 more)

### Community 16 - "VNIC Runtime Ops"
Cohesion: 0.18
Nodes (6): stopQueues, byte, Parser, VNic::distribute(), VNic::stopImpl(), VNic::validate()

### Community 17 - "NIC Example App"
Cohesion: 0.36
Nodes (6): ConsumerLoop(), DumpPacket(), main(), ProducerLoop(), freeRx, rx

### Community 18 - "VLAN Header Structures"
Cohesion: 0.25
Nodes (7): __be16, vlan_ethhdr, h_dest, h_source, h_vlan_encapsulated_proto, h_vlan_proto, h_vlan_TCI

### Community 19 - "RETA / RSS Control"
Cohesion: 0.33
Nodes (6): initReta, updateReta, Reta, VNic::configureRss(), VNic::initReta(), VNic::updateReta()

### Community 20 - "VNIC Configure Impl"
Cohesion: 0.40
Nodes (5): configureLink, configureQueues, configureRss, Config, VNic::configureImpl()

### Community 21 - "Unit Tests (RSS)"
Cohesion: 0.50
Nodes (3): BasicAssertions, TEST(), VnicTest

### Community 22 - "Poll & Distribute Engine"
Cohesion: 0.50
Nodes (4): distribute, validate, VNic::poll(), stop_token

### Community 23 - "Queue Startup Flow"
Cohesion: 0.67
Nodes (3): poll, startQueues, VNic::startImpl()

## Ambiguous Edges - Review These
- `vnic project` → `NIC — устройство обработки`  [AMBIGUOUS]
  dock/архитектура.md · relation: references

## Knowledge Gaps
- **139 isolated node(s):** `Config`, `configure`, `up`, `down`, `putRx` (+134 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **2 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `vnic project` and `NIC — устройство обработки`?**
  _Edge tagged AMBIGUOUS (relation: references) - confidence is low._
- **Why does `VNic` connect `VNIC Core Lifecycle` to `Example Byte Utilities`, `TX Queue & Stats`, `Link Abstraction`, `VNIC Configuration`, `RX Queue Engine`, `VNIC Runtime Ops`, `NIC Example App`, `RETA / RSS Control`, `VNIC Configure Impl`, `Poll & Distribute Engine`, `Queue Startup Flow`?**
  _High betweenness centrality (0.286) - this node is a cross-community bridge._
- **Why does `RxQueue` connect `RX Queue Engine` to `Example Byte Utilities`, `TX Queue & Stats`, `Packet Descriptor`, `FIFO Queue Utilities`, `VNIC Core Lifecycle`?**
  _High betweenness centrality (0.128) - this node is a cross-community bridge._
- **Why does `Link` connect `Link Abstraction` to `Example Byte Utilities`, `TX Queue & Stats`, `FIFO Queue Utilities`, `VNIC Core Lifecycle`, `Link Implementation`, `NIC Example App`?**
  _High betweenness centrality (0.126) - this node is a cross-community bridge._
- **What connects `Config`, `configure`, `up` to the rest of the system?**
  _139 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Packet Header Parsing` be split into smaller, more focused modules?**
  _Cohesion score 0.06871035940803383 - nodes in this community are weakly interconnected._
- **Should `Channel & Buffer Alloc` be split into smaller, more focused modules?**
  _Cohesion score 0.06097560975609756 - nodes in this community are weakly interconnected._