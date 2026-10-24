# TODO / known compromises 0.1.x

This file captures temporary solutions that were deliberately left in
version 0.1.x. They are working and covered with tests, but should not be perceived as
the final architecture.

## 1. Zero-copy and ring buffers

- The RX path is currently performing several copies: `Link::putRx()` copies
  frame to the internal buffer, `Link::takeRx()` — to the buffer of the poll cycle, then
  `RxQueue::put()` — to the pool segments.
- The TX path copies the data twice: when the chain is linearized in
  `TxQueue::put()` and when placing the frame in `Link` (`Link::putTx()`).
- `Fifo<T>` implemented on `std::queue` + `std::mutex` + `condition_variable`,
  and not on the lock-free SPSC ring buffer.
- `utils::Rcu<T>` uses `std::shared_mutex`, so for every accepted
  the frame is taken by shared lock.

Plans:

- Switch to real zero-copy: transfer ownership of segments from the pool without
intermediate copies (or use 'iovec`-like chains).
- Replace the `Fifo' with a circular SPSC/MPSC buffer with atomic indexes.
- Replace the `Rcu` with a lock-free scheme (hazard pointers/ seqlock/ RCU with
delayed release).
- Consider batch processing in `poll()' instead of one frame per iteration.
- Add performance stress tests.

## 2. Memory and Pools

- `memory::Pool` and `memory::Allocator' are stubs for
`malloc`/`free` so far; there is no pre-allocation and NUMA binding.
- Queues use `thread_local` scratch arrays for segments and
  handles, so as not to allocate them on the stack at each call. This
is a temporary measure; after switching to pools, it can be removed.
- `Link::Config` sets the depth of the pipes ('rxQueueDepth`/`txQueueDepth') and the size
  slots ('rxFrameSize`/`txFrameSize'). The depth already limits the FIFO, but
the memory for the slots is not yet reserved in advance: the Allocator remains.
  `malloc' is a stub.

Plans:

- Implement a pre-allocated pool of fixed blocks without allocations in hot
  path.
- Add NUMA-aware placement of pools and queues.
- Add offloads (checksum/TSO/VLAN).

##3. FCS and offloads

- `validate::CheckFcs()' always returns `true`: `Link` transmits frames without
  FCS, so there is nothing to check yet.
- The `PacketDescriptor::offloads` field is reserved, but not used;
  offloads are not emulated.

Plans:

- Add to `Link::Config` FCS transmission mode and check CRC32 on reception.
- Implement checksum/TSO/VLAN offload flags and handling.