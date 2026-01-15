// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file ipc.cpp
/// @brief Implementation of lock-free IPC channel

#include <lager_ext/ipc.h>
#include <lager_ext/serialization.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <boost/interprocess/mapped_region.hpp>
// Use Windows native shared memory to avoid Boost intermodule singleton issues
#ifdef _WIN32
#include <boost/interprocess/windows_shared_memory.hpp>
#else
#include <boost/interprocess/shared_memory_object.hpp>
#endif
#include <cstring>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace bip = boost::interprocess;

namespace lager_ext {
namespace ipc {

//=============================================================================
// Helper wrappers for serialization (using lager_ext::serialize/deserialize)
//=============================================================================

/// Serialize Value directly to a pre-allocated buffer (zero-copy optimization)
/// @return Number of bytes written, or 0 if buffer too small or null value
static size_t serialize_value_to(const Value& value, uint8_t* buffer, size_t buffer_size) {
    if (value.is_null()) {
        return 0;
    }
    return lager_ext::serialize_to(value, buffer, buffer_size);
}

/// Get serialized size without allocating
static size_t get_serialized_size(const Value& value) {
    if (value.is_null()) {
        return 0;
    }
    return lager_ext::serialized_size(value);
}

/// Deserialize Value from bytes
static Value deserialize_value(const uint8_t* data, size_t size) {
    if (size == 0 || data == nullptr) {
        return Value{};
    }
    return lager_ext::deserialize(data, size);
}

//=============================================================================
// Lock-Free Ring Buffer (Shared Memory Layout)
//=============================================================================

/// Shared memory header for the queue
/// Uses cache-line padding to prevent false sharing between producer and consumer
struct alignas(CACHE_LINE_SIZE) QueueHeader {
    // Magic number for validation
    static constexpr uint64_t MAGIC = 0x535053435155454Eull; // "SPSCQUEN"

    uint64_t magic;
    uint32_t version;
    uint32_t capacity;
    size_t messageSize;
    size_t totalSize;

    // Producer-owned: write index (only producer writes, consumer reads)
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> writeIndex;
    char producerPadding[CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];

    // Consumer-owned: read index (only consumer writes, producer reads)
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> readIndex;
    char consumerPadding[CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];

    // Data follows header
    // Message messages[capacity];

    QueueHeader(uint32_t cap)
        : magic(MAGIC), version(1), capacity(cap), messageSize(sizeof(Message)),
          totalSize(sizeof(QueueHeader) + cap * sizeof(Message)) {
        writeIndex.store(0, std::memory_order_relaxed);
        readIndex.store(0, std::memory_order_relaxed);
        std::memset(producerPadding, 0, sizeof(producerPadding));
        std::memset(consumerPadding, 0, sizeof(consumerPadding));
    }

    bool isValid() const { return magic == MAGIC && version == 1 && capacity > 0; }

    Message* messageAt(uint64_t index) {
        auto* data = reinterpret_cast<char*>(this) + sizeof(QueueHeader);
        return reinterpret_cast<Message*>(data + (index % capacity) * sizeof(Message));
    }
};

static_assert(offsetof(QueueHeader, writeIndex) % CACHE_LINE_SIZE == 0, "writeIndex must be cache-line aligned");
static_assert(offsetof(QueueHeader, readIndex) % CACHE_LINE_SIZE == 0, "readIndex must be cache-line aligned");

//=============================================================================
// Channel::Impl
//=============================================================================

class Channel::Impl {
public:
    Impl() = default;

    bool createAsProducer(const std::string& name, size_t capacity) {
        name_ = name;
        isProducer_ = true;
        capacity_ = capacity;

        try {
            // Calculate total size
            size_t totalSize = sizeof(QueueHeader) + capacity * sizeof(Message);

#ifdef _WIN32
            // Windows: use native shared memory
            shm_ = std::make_unique<bip::windows_shared_memory>(
                bip::create_only, name.c_str(), bip::read_write, totalSize);

            // Map entire region
            region_ = bip::mapped_region(*shm_, bip::read_write);
#else
            // POSIX: Remove any existing shared memory
            bip::shared_memory_object::remove(name.c_str());

            // Create shared memory
            shm_ = bip::shared_memory_object(bip::create_only, name.c_str(), bip::read_write);
            shm_.truncate(static_cast<bip::offset_t>(totalSize));

            // Map entire region
            region_ = bip::mapped_region(shm_, bip::read_write);
#endif

            // Initialize header using placement new
            header_ = new (region_.get_address()) QueueHeader(static_cast<uint32_t>(capacity));

            return true;
        } catch (const std::exception& e) {
            lastError_ = std::string("Failed to create producer: ") + e.what();
            return false;
        }
    }

    bool openAsConsumer(const std::string& name) {
        name_ = name;
        isProducer_ = false;

        try {
#ifdef _WIN32
            // Windows: open existing native shared memory
            shm_ = std::make_unique<bip::windows_shared_memory>(
                bip::open_only, name.c_str(), bip::read_write);

            // Map region
            region_ = bip::mapped_region(*shm_, bip::read_write);
#else
            // POSIX: Open existing shared memory
            shm_ = bip::shared_memory_object(bip::open_only, name.c_str(), bip::read_write);

            // Map region
            region_ = bip::mapped_region(shm_, bip::read_write);
#endif

            // Get header
            header_ = static_cast<QueueHeader*>(region_.get_address());

            if (!header_->isValid()) {
                lastError_ = "Invalid shared memory header";
                return false;
            }

            capacity_ = header_->capacity;

            return true;
        } catch (const std::exception& e) {
            lastError_ = std::string("Failed to open consumer: ") + e.what();
            return false;
        }
    }

    //-------------------------------------------------------------------------
    // Producer Operations
    //-------------------------------------------------------------------------

    bool post(uint32_t msgId, const Value& data, MessageDomain domain) {
        if (!isProducer_ || !header_) [[unlikely]] {
            lastError_ = "Not a producer";
            return false;
        }

        // Fast path: null value
        if (data.is_null()) {
            return postRaw(msgId, nullptr, 0, domain);
        }

        // Check serialized size first (no allocation)
        size_t dataSize = get_serialized_size(data);
        if (dataSize > Message::INLINE_SIZE) [[unlikely]] {
            // TODO: Support large data via SharedMemoryPool
            lastError_ = "Data too large for inline storage (max 232 bytes)";
            return false;
        }

        // Check if queue is full
        uint64_t currentWrite = header_->writeIndex.load(std::memory_order_relaxed);
        uint64_t currentRead = header_->readIndex.load(std::memory_order_relaxed);

        if (currentWrite - currentRead >= capacity_) [[unlikely]] {
            lastError_ = "Queue full";
            return false;
        }

        // Get message slot and serialize directly into it (zero-copy)
        Message* msg = header_->messageAt(currentWrite);
        msg->msgId = msgId;
        msg->timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        msg->domain = domain;
        msg->flags = MessageFlags::None;
        msg->requestId = 0;
        msg->poolOffset = 0;

        // Serialize directly to shared memory - no intermediate buffer
        size_t written = serialize_value_to(data, msg->inlineData, Message::INLINE_SIZE);
        msg->dataSize = static_cast<uint32_t>(written);

        // Publish: increment write index with release semantics
        header_->writeIndex.store(currentWrite + 1, std::memory_order_release);

        return true;
    }

    bool postRaw(uint32_t msgId, const void* data, size_t size, MessageDomain domain) {
        if (!isProducer_ || !header_) [[unlikely]] {
            lastError_ = "Not a producer";
            return false;
        }

        // Check if data fits inline
        if (size > Message::INLINE_SIZE) [[unlikely]] {
            // TODO: Support large data via SharedMemoryPool
            lastError_ = "Data too large for inline storage (max 232 bytes)";
            return false;
        }

        // Check if queue is full
        // Note: Producer only needs to know the value, not synchronize data
        // Using relaxed for readIndex is safe here - we're just checking capacity
        uint64_t currentWrite = header_->writeIndex.load(std::memory_order_relaxed);
        uint64_t currentRead = header_->readIndex.load(std::memory_order_relaxed);

        if (currentWrite - currentRead >= capacity_) [[unlikely]] {
            lastError_ = "Queue full";
            return false;
        }

        // Get message slot
        Message* msg = header_->messageAt(currentWrite);

        // Fill message (initialize all fields)
        msg->msgId = msgId;
        msg->dataSize = static_cast<uint32_t>(size);
        msg->timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        msg->domain = domain;
        msg->flags = MessageFlags::None;
        msg->requestId = 0;
        msg->poolOffset = 0;

        if (size > 0) [[likely]] {
            std::memcpy(msg->inlineData, data, size);
        }

        // Publish: increment write index with release semantics
        // This ensures all writes to the message are visible before the index update
        header_->writeIndex.store(currentWrite + 1, std::memory_order_release);

        return true;
    }

    bool canPost() const {
        if (!header_) [[unlikely]]
            return false;
        // Both can use relaxed - this is just a capacity check, not synchronizing data
        uint64_t write = header_->writeIndex.load(std::memory_order_relaxed);
        uint64_t read = header_->readIndex.load(std::memory_order_relaxed);
        return (write - read) < capacity_;
    }

    size_t pendingCount() const {
        if (!header_) [[unlikely]]
            return 0;
        // Relaxed is fine for approximate count - no data dependency
        uint64_t write = header_->writeIndex.load(std::memory_order_relaxed);
        uint64_t read = header_->readIndex.load(std::memory_order_relaxed);
        return static_cast<size_t>(write - read);
    }

    //-------------------------------------------------------------------------
    // Consumer Operations
    //-------------------------------------------------------------------------

    std::optional<Channel::ReceivedMessage> tryReceive() {
        if (!header_) {
            lastError_ = "Channel not initialized";
            return std::nullopt;
        }

        // Check if queue is empty
        uint64_t currentRead = header_->readIndex.load(std::memory_order_relaxed);
        uint64_t currentWrite = header_->writeIndex.load(std::memory_order_acquire);

        if (currentRead >= currentWrite) {
            return std::nullopt; // Empty
        }

        // Get message
        Message* msg = header_->messageAt(currentRead);

        ReceivedMessage result;
        result.msgId = msg->msgId;
        result.timestamp = msg->timestamp;
        result.domain = msg->domain;
        result.flags = msg->flags;
        result.requestId = msg->requestId;

        // Deserialize data from inline storage
        // TODO: If LargePayload flag is set, read from SharedMemoryPool using poolOffset
        if (msg->dataSize > 0) {
            result.data = deserialize_value(msg->inlineData, msg->dataSize);
        } else {
            result.data = Value{};
        }

        // Consume: increment read index with release semantics
        header_->readIndex.store(currentRead + 1, std::memory_order_release);

        return result;
    }

    std::optional<Channel::ReceivedMessage> receive(std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        // Adaptive spinning: start with busy-wait, then sleep
        int spinCount = 0;
        constexpr int MAX_SPINS = 1000;

        while (true) {
            if (auto msg = tryReceive()) {
                return msg;
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }

            // Adaptive backoff
            if (spinCount < MAX_SPINS) {
                spinCount++;
// CPU pause instruction for busy-wait
#ifdef _MSC_VER
                _mm_pause();
#else
                __builtin_ia32_pause();
#endif
            } else {
                // Sleep to avoid burning CPU
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
    }

    int tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize) {
        if (!header_) {
            return 0;
        }

        uint64_t currentRead = header_->readIndex.load(std::memory_order_relaxed);
        uint64_t currentWrite = header_->writeIndex.load(std::memory_order_acquire);

        if (currentRead >= currentWrite) {
            return 0; // Empty
        }

        Message* msg = header_->messageAt(currentRead);
        outMsgId = msg->msgId;

        // Copy inline data
        if (msg->dataSize > 0) {
            if (msg->dataSize > maxSize) {
                return -1; // Buffer too small
            }
            std::memcpy(outData, msg->inlineData, msg->dataSize);
        }

        int result = static_cast<int>(msg->dataSize);
        header_->readIndex.store(currentRead + 1, std::memory_order_release);
        return result;
    }

    //-------------------------------------------------------------------------
    // Properties
    //-------------------------------------------------------------------------

    const std::string& name() const { return name_; }
    bool isProducer() const { return isProducer_; }
    size_t capacity() const { return capacity_; }
    const std::string& lastError() const { return lastError_; }

    ~Impl() {
#ifndef _WIN32
        // POSIX: cleanup shared memory if we are producer
        // Windows: kernel handles cleanup automatically via reference counting
        if (isProducer_ && !name_.empty()) {
            try {
                bip::shared_memory_object::remove(name_.c_str());
            } catch (...) {}
        }
#endif
    }

private:
    std::string name_;
    bool isProducer_ = false;
    size_t capacity_ = 0;
    mutable std::string lastError_;

#ifdef _WIN32
    std::unique_ptr<bip::windows_shared_memory> shm_;
#else
    bip::shared_memory_object shm_;
#endif
    bip::mapped_region region_;
    QueueHeader* header_ = nullptr;
};

//=============================================================================
// Channel Public API
//=============================================================================

Channel::Channel() : impl_(std::make_unique<Impl>()) {}
Channel::~Channel() = default;
Channel::Channel(Channel&&) noexcept = default;
Channel& Channel::operator=(Channel&&) noexcept = default;

std::unique_ptr<Channel> Channel::create(const std::string& name, size_t capacity) {
    auto channel = std::unique_ptr<Channel>(new Channel());
    if (!channel->impl_->createAsProducer(name, capacity)) {
        return nullptr;
    }
    return channel;
}

std::unique_ptr<Channel> Channel::open(const std::string& name) {
    auto channel = std::unique_ptr<Channel>(new Channel());
    if (!channel->impl_->openAsConsumer(name)) {
        return nullptr;
    }
    return channel;
}

bool Channel::post(uint32_t msgId, const Value& data, MessageDomain domain) {
    return impl_->post(msgId, data, domain);
}

bool Channel::postRaw(uint32_t msgId, const void* data, size_t size, MessageDomain domain) {
    return impl_->postRaw(msgId, data, size, domain);
}

bool Channel::canPost() const {
    return impl_->canPost();
}

size_t Channel::pendingCount() const {
    return impl_->pendingCount();
}

std::optional<Channel::ReceivedMessage> Channel::tryReceive() {
    return impl_->tryReceive();
}

std::optional<Channel::ReceivedMessage> Channel::receive(std::chrono::milliseconds timeout) {
    return impl_->receive(timeout);
}

int Channel::tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize) {
    return impl_->tryReceiveRaw(outMsgId, outData, maxSize);
}

const std::string& Channel::name() const {
    return impl_->name();
}

bool Channel::isProducer() const {
    return impl_->isProducer();
}

size_t Channel::capacity() const {
    return impl_->capacity();
}

const std::string& Channel::lastError() const {
    return impl_->lastError();
}

//=============================================================================
// ChannelPair::Impl
//=============================================================================

class ChannelPair::Impl {
public:
    bool createPair(const std::string& name, size_t capacity) {
        name_ = name;
        isCreator_ = true;

        // Create A->B channel (we produce, they consume)
        outChannel_ = Channel::create(name + "_AtoB", capacity);
        if (!outChannel_) {
            lastError_ = "Failed to create outgoing channel";
            return false;
        }

        // Create B->A channel (they produce, we consume)
        inChannel_ = Channel::create(name + "_BtoA", capacity);
        if (!inChannel_) {
            lastError_ = "Failed to create incoming channel";
            return false;
        }

        return true;
    }

    bool connectPair(const std::string& name) {
        name_ = name;
        isCreator_ = false;

        // Open A->B channel (they produce, we consume)
        inChannel_ = Channel::open(name + "_AtoB");
        if (!inChannel_) {
            lastError_ = "Failed to open incoming channel";
            return false;
        }

        // Open B->A channel (we produce, they consume)
        // Note: We need to wait for creator to create this
        int retries = 100;
        while (retries-- > 0) {
            outChannel_ = Channel::open(name + "_BtoA");
            if (outChannel_)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!outChannel_) {
            lastError_ = "Failed to open outgoing channel";
            return false;
        }

        return true;
    }

    bool post(uint32_t msgId, const Value& data) {
        if (!outChannel_)
            return false;
        return outChannel_->post(msgId, data);
    }

    bool postRaw(uint32_t msgId, const void* data, size_t size) {
        if (!outChannel_)
            return false;
        return outChannel_->postRaw(msgId, data, size);
    }

    std::optional<Channel::ReceivedMessage> tryReceive() {
        if (!inChannel_)
            return std::nullopt;
        return inChannel_->tryReceive();
    }

    int tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize) {
        if (!inChannel_)
            return 0;
        return inChannel_->tryReceiveRaw(outMsgId, outData, maxSize);
    }

    std::optional<Channel::ReceivedMessage> receive(std::chrono::milliseconds timeout) {
        if (!inChannel_)
            return std::nullopt;
        return inChannel_->receive(timeout);
    }

    std::optional<Value> send(uint32_t msgId, const Value& data, std::chrono::milliseconds timeout) {
        if (!post(msgId, data)) {
            return std::nullopt;
        }

        auto reply = receive(timeout);
        if (reply) {
            return reply->data;
        }
        return std::nullopt;
    }

    const std::string& name() const { return name_; }
    bool isCreator() const { return isCreator_; }
    const std::string& lastError() const { return lastError_; }

private:
    std::string name_;
    bool isCreator_ = false;
    mutable std::string lastError_;

    std::unique_ptr<Channel> outChannel_;
    std::unique_ptr<Channel> inChannel_;
};

//=============================================================================
// ChannelPair Public API
//=============================================================================

ChannelPair::ChannelPair() : impl_(std::make_unique<Impl>()) {}
ChannelPair::~ChannelPair() = default;
ChannelPair::ChannelPair(ChannelPair&&) noexcept = default;
ChannelPair& ChannelPair::operator=(ChannelPair&&) noexcept = default;

std::unique_ptr<ChannelPair> ChannelPair::create(const std::string& name, size_t capacity) {
    auto pair = std::unique_ptr<ChannelPair>(new ChannelPair());
    if (!pair->impl_->createPair(name, capacity)) {
        return nullptr;
    }
    return pair;
}

std::unique_ptr<ChannelPair> ChannelPair::connect(const std::string& name) {
    auto pair = std::unique_ptr<ChannelPair>(new ChannelPair());
    if (!pair->impl_->connectPair(name)) {
        return nullptr;
    }
    return pair;
}

bool ChannelPair::post(uint32_t msgId, const Value& data) {
    return impl_->post(msgId, data);
}

bool ChannelPair::postRaw(uint32_t msgId, const void* data, size_t size) {
    return impl_->postRaw(msgId, data, size);
}

std::optional<Channel::ReceivedMessage> ChannelPair::tryReceive() {
    return impl_->tryReceive();
}

int ChannelPair::tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize) {
    return impl_->tryReceiveRaw(outMsgId, outData, maxSize);
}

std::optional<Channel::ReceivedMessage> ChannelPair::receive(std::chrono::milliseconds timeout) {
    return impl_->receive(timeout);
}

std::optional<Value> ChannelPair::send(uint32_t msgId, const Value& data,
                                       std::chrono::milliseconds timeout) {
    return impl_->send(msgId, data, timeout);
}

const std::string& ChannelPair::name() const {
    return impl_->name();
}

bool ChannelPair::isCreator() const {
    return impl_->isCreator();
}

const std::string& ChannelPair::lastError() const {
    return impl_->lastError();
}

} // namespace ipc
} // namespace lager_ext