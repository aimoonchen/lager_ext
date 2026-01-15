// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

#include <lager_ext/windows_message.h>

#ifdef LAGER_EXT_ENABLE_IPC

#include <lager_ext/ipc.h>
#include <cstring>

namespace lager_ext {
namespace ipc {

// ============================================================================
// WindowsMessageBridge::Impl
// ============================================================================

class WindowsMessageBridge::Impl {
public:
    Impl(std::string_view channel_name, Role role, std::size_t capacity)
        : channel_name_(channel_name), role_(role) {
        
        switch (role) {
        case Role::Sender:
            // Sender creates the channel pair
            channel_pair_ = ChannelPair::create(std::string(channel_name), capacity);
            break;
        case Role::Receiver:
            // Receiver connects to existing channel pair
            channel_pair_ = ChannelPair::connect(std::string(channel_name));
            break;
        case Role::Bidirectional:
            // Bidirectional: same as Sender (creates pair), peer uses Receiver
            channel_pair_ = ChannelPair::create(std::string(channel_name), capacity);
            break;
        }
    }

    bool forward(UINT message, WPARAM wParam, LPARAM lParam) {
        if (!channel_pair_) {
            last_error_ = "Channel pair not initialized";
            return false;
        }

        // Pack message into binary struct (24 bytes, zero serialization overhead)
        WindowsMessageData data;
        data.message = message;
        data.reserved = 0;
        data.wParam = static_cast<uint64_t>(wParam);
        data.lParam = static_cast<int64_t>(lParam);

        bool ok = channel_pair_->postRaw(WINDOWS_MSG_FORWARD, &data, sizeof(data));
        if (ok) {
            ++messages_sent_;
        }
        return ok;
    }

    bool forward_if(UINT message, WPARAM wParam, LPARAM lParam, MessageFilter filter) {
        if (filter && !filter(message)) {
            return true;  // Filtered out, considered success
        }
        return forward(message, wParam, lParam);
    }

    std::size_t forward_batch(const WindowsMessageData* messages, std::size_t count) {
        if (!channel_pair_) {
            return 0;
        }

        std::size_t sent = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (channel_pair_->postRaw(WINDOWS_MSG_FORWARD, &messages[i], sizeof(WindowsMessageData))) {
                ++sent;
                ++messages_sent_;
            } else {
                break;  // Queue full
            }
        }
        return sent;
    }

    void on_message(MessageHandler handler) {
        handler_ = std::move(handler);
    }

    void on_message(SimpleHandler handler) {
        handler_ = [h = std::move(handler)](UINT msg, WPARAM w, LPARAM l) -> std::optional<LRESULT> {
            h(msg, w, l);
            return std::nullopt;
        };
    }

    void on_message_range(UINT msg_begin, UINT msg_end, SimpleHandler handler) {
        range_handlers_.push_back({msg_begin, msg_end, std::move(handler)});
    }

#ifdef _WIN32
    void dispatch_to(HWND hwnd, bool use_post) {
        dispatch_hwnd_ = hwnd;
        use_post_ = use_post;
    }
#endif

    std::size_t poll() {
        if (!channel_pair_) {
            return 0;
        }

        std::size_t processed = 0;
        WindowsMessageData data;
        uint32_t msgId;

        // Use tryReceiveRaw for zero-copy binary receive
        while (channel_pair_->tryReceiveRaw(msgId, &data, sizeof(data)) > 0) {
            if (msgId == WINDOWS_MSG_FORWARD) {
                dispatch_message(data.message,
                                static_cast<WPARAM>(data.wParam),
                                static_cast<LPARAM>(data.lParam));
                ++processed;
                ++messages_received_;
            }
        }
        return processed;
    }

    std::size_t poll(std::chrono::milliseconds timeout) {
        if (!channel_pair_) {
            return 0;
        }

        std::size_t processed = 0;
        auto deadline = std::chrono::steady_clock::now() + timeout;
        WindowsMessageData data;
        uint32_t msgId;

        while (std::chrono::steady_clock::now() < deadline) {
            int result = channel_pair_->tryReceiveRaw(msgId, &data, sizeof(data));
            if (result > 0) {
                if (msgId == WINDOWS_MSG_FORWARD) {
                    dispatch_message(data.message,
                                    static_cast<WPARAM>(data.wParam),
                                    static_cast<LPARAM>(data.lParam));
                    ++processed;
                    ++messages_received_;
                }
            } else {
                // No message, yield briefly
                std::this_thread::yield();
            }
        }
        return processed;
    }

    bool valid() const {
        return channel_pair_ != nullptr;
    }

    const std::string& channel_name() const { return channel_name_; }
    const std::string& last_error() const { return last_error_; }
    std::size_t messages_sent() const { return messages_sent_; }
    std::size_t messages_received() const { return messages_received_; }

private:
    void dispatch_message(UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef _WIN32
        // Dispatch to window if configured
        if (dispatch_hwnd_) {
            if (use_post_) {
                ::PostMessageW(dispatch_hwnd_, message, wParam, lParam);
            } else {
                ::SendMessageW(dispatch_hwnd_, message, wParam, lParam);
            }
            return;
        }
#endif

        // Check range handlers first
        for (const auto& rh : range_handlers_) {
            if (message >= rh.msg_begin && message < rh.msg_end) {
                rh.handler(message, wParam, lParam);
                return;
            }
        }

        // Call general handler
        if (handler_) {
            handler_(message, wParam, lParam);
        }
    }

private:
    std::string channel_name_;
    Role role_;
    std::string last_error_;

    std::unique_ptr<ChannelPair> channel_pair_;

    MessageHandler handler_;
    
    struct RangeHandler {
        UINT msg_begin;
        UINT msg_end;
        SimpleHandler handler;
    };
    std::vector<RangeHandler> range_handlers_;

#ifdef _WIN32
    HWND dispatch_hwnd_ = nullptr;
    bool use_post_ = false;
#endif

    std::size_t messages_sent_ = 0;
    std::size_t messages_received_ = 0;
};

// ============================================================================
// WindowsMessageBridge Public API
// ============================================================================

WindowsMessageBridge::WindowsMessageBridge(std::string_view channel_name, Role role,
                                           std::size_t capacity)
    : impl_(std::make_unique<Impl>(channel_name, role, capacity)) {}

WindowsMessageBridge::~WindowsMessageBridge() = default;

WindowsMessageBridge::WindowsMessageBridge(WindowsMessageBridge&&) noexcept = default;
WindowsMessageBridge& WindowsMessageBridge::operator=(WindowsMessageBridge&&) noexcept = default;

bool WindowsMessageBridge::forward(UINT message, WPARAM wParam, LPARAM lParam) {
    return impl_->forward(message, wParam, lParam);
}

bool WindowsMessageBridge::forward_if(UINT message, WPARAM wParam, LPARAM lParam, 
                                      MessageFilter filter) {
    return impl_->forward_if(message, wParam, lParam, std::move(filter));
}

std::size_t WindowsMessageBridge::forward_batch(const WindowsMessageData* messages, 
                                                 std::size_t count) {
    return impl_->forward_batch(messages, count);
}

void WindowsMessageBridge::on_message(MessageHandler handler) {
    impl_->on_message(std::move(handler));
}

void WindowsMessageBridge::on_message(SimpleHandler handler) {
    impl_->on_message(std::move(handler));
}

void WindowsMessageBridge::on_message_range(UINT msg_begin, UINT msg_end, 
                                            SimpleHandler handler) {
    impl_->on_message_range(msg_begin, msg_end, std::move(handler));
}

#ifdef _WIN32
void WindowsMessageBridge::dispatch_to(HWND hwnd, bool use_post) {
    impl_->dispatch_to(hwnd, use_post);
}
#endif

std::size_t WindowsMessageBridge::poll() {
    return impl_->poll();
}

std::size_t WindowsMessageBridge::poll(std::chrono::milliseconds timeout) {
    return impl_->poll(timeout);
}

bool WindowsMessageBridge::connected() const {
    return impl_->valid();
}

const std::string& WindowsMessageBridge::channel_name() const {
    return impl_->channel_name();
}

const std::string& WindowsMessageBridge::last_error() const {
    return impl_->last_error();
}

std::size_t WindowsMessageBridge::messages_sent() const {
    return impl_->messages_sent();
}

std::size_t WindowsMessageBridge::messages_received() const {
    return impl_->messages_received();
}

} // namespace ipc
} // namespace lager_ext

#endif // LAGER_EXT_ENABLE_IPC