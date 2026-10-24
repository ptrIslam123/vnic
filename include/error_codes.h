#pragma once

#include <cstdint>

/**
 * @file
 * @brief Common error codes used across the library.
 */

namespace vnic {

/**
 * @defgroup group_errors Error reporting
 * @brief A single error enum shared by every layer of the library.
 * @{
 */

/**
 * @brief Reason of a failed operation.
 *
 * Fallible operations return `std::expected<T, ErrorCode>` (or
 * `std::expected<void, ErrorCode>`). Layers propagate the code of the failing
 * child (for example, `VNic` returns the error of the `Link` or queue that
 * rejected the operation) instead of replacing it with a generic value.
 *
 * Values are grouped by component so new codes can be added without
 * renumbering existing ones.
 */
enum class ErrorCode : std::uint32_t {
    // common
    None               = 0,  ///< Operation succeeded.
    Unknown            = 1,  ///< Unspecified failure.
    InvalidArgument    = 2,  ///< Bad function argument.
    InvalidState       = 3,  ///< Operation is not allowed in the current state.
    InvalidOperation   = 4,  ///< Operation cannot be performed.
    NotConfigured      = 5,  ///< Object has not been configured.
    AlreadyConfigured  = 6,  ///< Object is already configured.
    NotStarted         = 7,  ///< Object has not been started.
    AlreadyStarted     = 8,  ///< Object is already started.
    Busy               = 9,  ///< Object is busy.
    Timeout            = 10, ///< Operation timed out.
    NotSupported       = 11, ///< Feature is not supported.
    NotImplemented     = 12, ///< Feature is not implemented yet.
    OutOfMemory        = 13, ///< Memory allocation failed.
    AllocationFailed   = 14, ///< Generic allocation failure.
    ResourceExhausted  = 15, ///< A finite resource is exhausted.
    InternalError      = 16, ///< Internal invariant violated.
    NullPointer        = 17, ///< A required pointer is null.
    BufferTooSmall     = 18, ///< Output buffer is too small.
    BufferOverflow     = 19, ///< Data does not fit into the buffer.
    OutOfRange         = 20, ///< Value is outside the accepted range.
    InvalidIndex       = 21, ///< Index does not address an existing object.
    InvalidSize        = 22, ///< Size is invalid.
    WouldBlock         = 23, ///< Operation would block.
    Interrupted        = 24, ///< Operation was interrupted.
    Cancelled          = 25, ///< Operation was cancelled.
    SystemError        = 26, ///< Operating-system failure.
    IoError            = 27, ///< Input/output failure.
    PermissionDenied   = 28, ///< Access is denied.
    CorruptedData      = 29, ///< Input data is corrupted.
    UnsupportedVersion = 30, ///< Version is not supported.
    TypeMismatch       = 31, ///< Object type does not match the request.
    Overflow           = 32, ///< Arithmetic or logical overflow.
    Underflow          = 33, ///< Arithmetic or logical underflow.
    ThreadError        = 34, ///< Generic thread failure.

    // state machine
    StateTransitionInvalid   = 40, ///< Requested transition is not allowed.
    StateConfigureNotAllowed = 41, ///< configure() is not allowed now.
    StateStartNotAllowed     = 42, ///< start() is not allowed now.
    StateStopNotAllowed      = 43, ///< stop() is not allowed now.
    StateConfigureFailed     = 44, ///< configureImpl() failed without a specific code.
    StateStartFailed         = 45, ///< startImpl() failed without a specific code.
    StateStopFailed          = 46, ///< stopImpl() failed without a specific code.
    StateAlreadyInProgress   = 47, ///< A lifecycle transition is already running.

    // config
    ConfigInvalid                    = 100, ///< Configuration is invalid.
    ConfigMissing                    = 101, ///< Required configuration is absent.
    ConfigAlreadyApplied             = 102, ///< Configuration is already active.
    ConfigMtuInvalid                 = 103, ///< MTU is zero or invalid.
    ConfigMacInvalid                 = 104, ///< MAC address is malformed.
    ConfigIpInvalid                  = 105, ///< IP address is malformed.
    ConfigVlanInvalid                = 106, ///< VLAN id is outside 0..4095.
    ConfigQueueCountInvalid          = 107, ///< Queue count is invalid.
    ConfigRssInvalid                 = 108, ///< RSS configuration is invalid.
    ConfigRssKeyTooShort             = 109, ///< RSS key is shorter than 40 bytes.
    ConfigRssRetaEmpty               = 110, ///< RETA is empty.
    ConfigRssRetaSizeNotPowerOfTwo   = 111, ///< RETA size is not a power of two.
    ConfigRssRetaQueueOutOfRange     = 112, ///< RETA references a missing RX queue.
    ConfigRssHashFunctionUnsupported = 113, ///< RSS hash function is unsupported.
    ConfigLinkInvalid                = 114, ///< Link configuration is invalid.
    ConfigLinkRxDepthInvalid         = 115, ///< RX pipe depth is zero.
    ConfigLinkTxDepthInvalid         = 116, ///< TX pipe depth is zero.
    ConfigLinkRxFrameSizeInvalid     = 117, ///< RX frame size is zero.
    ConfigLinkTxFrameSizeInvalid     = 118, ///< TX frame size is zero.
    ConfigQueueInvalid               = 119, ///< Queue configuration is invalid.
    ConfigQueueSizeInvalid           = 120, ///< Queue size is invalid.
    ConfigPoolInvalid                = 121, ///< Memory-pool configuration is invalid.
    ConfigAllocatorInvalid           = 122, ///< Allocator configuration is invalid.

    // link
    LinkError            = 150, ///< Generic link failure.
    LinkNotConfigured    = 151, ///< Link has not been configured.
    LinkConfigureFailed  = 152, ///< Link configuration failed.
    LinkDown             = 153, ///< Link is down.
    LinkAlreadyUp        = 154, ///< Link is already up.
    LinkAlreadyDown      = 155, ///< Link is already down.
    LinkPacketEmpty      = 156, ///< Cannot enqueue an empty frame.
    LinkRxQueueFull      = 157, ///< RX pipe is full.
    LinkTxQueueFull      = 158, ///< TX pipe is full.
    LinkAllocationFailed = 159, ///< Frame copy allocation failed.
    LinkPutFailed        = 160, ///< Generic enqueue failure.
    LinkTakeFailed       = 161, ///< Generic dequeue failure.
    LinkQueueError       = 162, ///< Error reported by the underlying FIFO.

    // memory
    MemoryError                = 180, ///< Generic memory failure.
    MemoryAllocationFailed     = 181, ///< Allocation failed.
    MemoryDeallocationFailed   = 182, ///< Deallocation failed.
    MemoryPoolExhausted        = 183, ///< Fixed-size pool is exhausted.
    MemoryPoolNotConfigured    = 184, ///< Pool has not been configured.
    MemoryPoolConfigInvalid    = 185, ///< Pool configuration is invalid.
    MemoryPoolCapacityExceeded = 186, ///< Pool capacity would be exceeded.
    MemoryInvalidPointer       = 187, ///< Pointer does not belong to the allocator.
    AllocatorNotConfigured     = 188, ///< Allocator has not been configured.
    AllocatorAllocationFailed  = 189, ///< Allocator failed to allocate a block.
    AllocatorInvalidSize       = 190, ///< Requested allocation size is invalid.
    AllocatorConfigInvalid     = 191, ///< Allocator configuration is invalid.

    // queues
    QueueError                = 220, ///< Generic queue failure.
    QueueFull                 = 221, ///< Queue is full.
    QueueEmpty                = 222, ///< Queue is empty.
    QueueClosed               = 223, ///< Queue is closed.
    QueueOverflow             = 224, ///< Queue overflow.
    QueueUnderflow            = 225, ///< Queue underflow.
    QueueNotConfigured        = 226, ///< Queue has not been configured.
    QueueNotStarted           = 227, ///< Queue has not been started.
    QueueAlreadyStarted       = 228, ///< Queue is already started.
    QueueStartFailed          = 229, ///< Queue start failed.
    QueueStopFailed           = 230, ///< Queue stop failed.
    QueueInvalidId            = 231, ///< Queue id is out of range.
    QueueInvalidConfig        = 232, ///< Queue configuration is invalid.
    QueueDescriptorInvalid    = 233, ///< Descriptor is invalid.
    QueuePoolExhausted        = 234, ///< Descriptor pool is exhausted.
    QueueSegmentLimitExceeded = 235, ///< Buffer chain has too many segments.
    QueueZeroLengthPacket     = 236, ///< Packet has zero length.
    QueuePacketTooLarge       = 237, ///< Packet exceeds the size limit.
    QueuePushFailed           = 238, ///< Generic enqueue failure.
    QueuePopFailed            = 239, ///< Generic dequeue failure.
    QueueCapacityInvalid      = 240, ///< Queue capacity is invalid.
    QueueFreeListEmpty        = 241, ///< No free descriptors remain.
    QueueUsedListFull         = 242, ///< Used-descriptor list is full.
    QueueChainInvalid         = 243, ///< Buffer chain is malformed.
    QueueCopyFailed           = 244, ///< Packet copy failed.
    QueueFreeListFull         = 245, ///< Free-descriptor list is full.

    // parser
    ParseError                    = 280, ///< Generic parse failure.
    ParseInvalidFrame             = 281, ///< Frame is malformed.
    ParseTruncated                = 282, ///< Frame is truncated.
    ParseEthernetTooShort         = 283, ///< Ethernet header is incomplete.
    ParseInvalidVlan              = 284, ///< VLAN tag is malformed.
    ParseUnsupportedEtherType     = 285, ///< EtherType is not supported.
    ParseIpv4TooShort             = 286, ///< IPv4 header is incomplete.
    ParseInvalidIpv4Version       = 287, ///< IPv4 version field is wrong.
    ParseInvalidIpv4HeaderLength  = 288, ///< IPv4 IHL is invalid.
    ParseInvalidIpv4TotalLength   = 289, ///< IPv4 total length is invalid.
    ParseIpv6TooShort             = 290, ///< IPv6 header is incomplete.
    ParseInvalidIpv6Version       = 291, ///< IPv6 version field is wrong.
    ParseInvalidIpv6PayloadLength = 292, ///< IPv6 payload length is invalid.
    ParseArpTooShort              = 293, ///< ARP header is incomplete.
    ParseInvalidArpHeaderLength   = 294, ///< ARP address length is invalid.
    ParseTcpTooShort              = 295, ///< TCP header is incomplete.
    ParseUdpTooShort              = 296, ///< UDP header is incomplete.
    ParseIcmpTooShort             = 297, ///< ICMP header is incomplete.
    ParseUnsupportedProtocol      = 298, ///< L4 protocol is not supported.
    ParseIpv6ExtensionUnsupported = 299, ///< IPv6 extension header is unsupported.
    ParseChecksumMismatch         = 300, ///< Header checksum is wrong.
    ParseHeaderTruncated          = 301, ///< A header ends outside the frame.
    ParseNoEthernetHeader         = 302, ///< No Ethernet header.
    ParseNoIpHeader               = 303, ///< No IP header.
    ParseNoL4Header               = 304, ///< No transport header.

    // validation
    ValidationError                = 330, ///< Generic validation failure.
    ValidationFailed               = 331, ///< Frame did not pass validation.
    ValidationFrameTooShort        = 332, ///< Frame is below the minimum size.
    ValidationFrameTooLong         = 333, ///< Frame exceeds MTU.
    ValidationFcsInvalid           = 334, ///< Frame check sequence is invalid.
    ValidationDstMacMismatch       = 335, ///< Destination MAC is not accepted.
    ValidationVlanRejected         = 336, ///< VLAN id is not allowed.
    ValidationIpTotalLengthInvalid = 337, ///< IPv4 total length disagrees with the frame.
    ValidationIpv4ChecksumInvalid  = 338, ///< IPv4 header checksum is wrong.
    ValidationMalformedFrame       = 339, ///< Frame is structurally invalid.
    ValidationUnsupportedFrame     = 340, ///< Frame type cannot be validated.

    // RSS
    RssError                    = 370, ///< Generic RSS failure.
    RssDisabled                 = 371, ///< RSS is disabled.
    RssInvalidProtocol          = 372, ///< Protocol mask is invalid.
    RssInvalidHashFunction      = 373, ///< Hash function is invalid.
    RssKeyTooShort              = 374, ///< RSS key is too short.
    RssKeyInvalid               = 375, ///< RSS key is invalid.
    RssRetaEmpty                = 376, ///< RETA is empty.
    RssRetaSizeInvalid          = 377, ///< RETA size is invalid.
    RssRetaQueueOutOfRange      = 378, ///< RETA references a missing queue.
    RssHashFailed               = 379, ///< Hash computation failed.
    RssUnsupportedFlow          = 380, ///< Flow type cannot be hashed.
    RssInvalidTuple             = 381, ///< 5-tuple is invalid.
    RssSymmetricModeUnsupported = 382, ///< Symmetry mode is unsupported.
    RssNoIpHeader               = 383, ///< No IP header for the hash.
    RssNoPorts                  = 384, ///< No L4 ports for the hash.
    HashInvalidInput            = 385, ///< Hash input is invalid.
    HashUnsupportedFunction     = 386, ///< Hash function is unsupported.
    HashKeyInvalid              = 387, ///< Hash key is invalid.
    HashInputTooLong            = 388, ///< Hash input exceeds its limit.
    HashCalculationFailed       = 389, ///< Hash computation failed.

    // TX
    TxError                = 420, ///< Generic transmit failure.
    TxDescriptorNull       = 421, ///< Descriptor has no buffer.
    TxSegmentLimitExceeded = 422, ///< Buffer chain is too long.
    TxLengthMismatch       = 423, ///< Chain length disagrees with totalLength.
    TxPacketTooLarge       = 424, ///< Packet exceeds the transmit limit.
    TxLinkUnavailable      = 425, ///< TX queue has no link.
    TxLinkRejected         = 426, ///< Link rejected the frame.
    TxQueueFull            = 427, ///< TX queue is full.
    TxQueueNotConfigured   = 428, ///< TX queue has not been configured.
    TxQueueNotStarted      = 429, ///< TX queue has not been started.
    TxAllocationFailed     = 430, ///< Transmit buffer allocation failed.
    TxInvalidLength        = 431, ///< Transmit length is invalid.

    // RX
    RxError              = 450, ///< Generic receive failure.
    RxInvalidQueueId     = 451, ///< RX queue id is invalid.
    RxQueueFull          = 452, ///< RX queue is full.
    RxQueueEmpty         = 453, ///< RX queue is empty.
    RxQueueNotConfigured = 454, ///< RX queue has not been configured.
    RxQueueNotStarted    = 455, ///< RX queue has not been started.
    RxDescriptorInvalid  = 456, ///< RX descriptor is invalid.
    RxAllocationFailed   = 457, ///< RX buffer allocation failed.
    RxFreeListEmpty      = 458, ///< No free RX descriptors.
    RxUsedListFull       = 459, ///< Used RX descriptor list is full.
    RxChainInvalid       = 460, ///< RX buffer chain is malformed.
    RxCopyFailed         = 461, ///< Frame copy failed.
    RxNoFreeDescriptors  = 462, ///< No descriptors available.
    RxFrameTooLarge      = 463, ///< Frame exceeds the RX limit.
    RxPoolExhausted      = 464, ///< RX memory pool is exhausted.

    // threads
    ThreadCreateFailed   = 491, ///< Thread creation failed.
    ThreadJoinFailed     = 492, ///< Thread join failed.
    ThreadStartFailed    = 493, ///< Thread start failed.
    ThreadStopFailed     = 494, ///< Thread stop failed.
    ThreadNotJoinable    = 495, ///< Thread is not joinable.
    ThreadAlreadyRunning = 496, ///< Thread is already running.
    ThreadInterrupted    = 497, ///< Thread was interrupted.

    // --------------------------------------------------------------- clock (510)
    ClockError       = 510, ///< Generic clock failure.
    ClockReadFailed  = 511, ///< Clock read failed.
    ClockUnavailable = 512, ///< Monotonic clock is unavailable.
};

/// @}

} // namespace vnic
