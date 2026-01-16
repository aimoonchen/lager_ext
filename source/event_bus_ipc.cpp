// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file event_bus_ipc.cpp
/// @brief Implementation of RemoteBus for cross-process event passing

#include <lager_ext/event_bus_ipc.h>
#include <lager_ext/value.h>

#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lager_ext {

// ============================================================================
// RemoteBus::Impl
// ============================================================================

class RemoteBus::Impl {
public:
    /// Remote handler with ID for lifecycle management
    struct RemoteHandler {
        uint64_t id;
        std::move_only_function<void(const ImmerValue&) const> handler;
    };

    /// Domain handler with ID for lifecycle management
    struct DomainHandler {
        uint64_t id;
        std::move_only_function<void(const RemoteBus::DomainEnvelope&, const ImmerValue&) const> handler;
    };

    Impl(std::string_view channel_name, EventBus& bus, Role role, std::size_t capacity)
        : channel_name_(channel_name), bus_(bus), role_(role) {
        try {
            switch (role) {
            case Role::Server:
                channel_ = ipc::Channel::create(std::string(channel_name), capacity);
                connected_ = (channel_ != nullptr);
                break;

            case Role::Client:
                channel_ = ipc::Channel::open(std::string(channel_name));
                connected_ = (channel_ != nullptr);
                break;

            case Role::Peer:
                // Try create first, fallback to connect
                channel_pair_ = ipc::ChannelPair::create(std::string(channel_name), capacity);
                if (!channel_pair_) {
                    channel_pair_ = ipc::ChannelPair::connect(std::string(channel_name));
                }
                connected_ = (channel_pair_ != nullptr);
                break;
            }
        } catch (const std::exception& e) {
            last_error_ = e.what();
            connected_ = false;
        }
    }

    ~Impl() = default;

    bool post_remote(std::string_view event_name, const ImmerValue& payload) {
        if (!connected_) {
            return false;
        }

        // Envelope: {"n": name, "d": data}
        ImmerValue envelope = ImmerValue::map({{"n", std::string(event_name)}, {"d", payload}});

        if (channel_pair_) {
            return channel_pair_->post(detail::IPC_EVT_EVENT, envelope);
        }
        if (channel_) {
            return channel_->post(detail::IPC_EVT_EVENT, envelope);
        }
        return false;
    }

    bool broadcast(std::string_view event_name, const ImmerValue& payload) {
        bus_.publish(event_name, payload);
        return post_remote(event_name, payload);
    }

    Connection subscribe_remote_impl(std::string_view event_name, std::move_only_function<void(const ImmerValue&) const> handler) {
        uint64_t slot_id = next_slot_id_++;
        std::string name(event_name);
        
        remote_handlers_[name].push_back({slot_id, std::move(handler)});
        
        // Return a Connection that actually disconnects
        return Connection([this, name, slot_id]() {
            remove_remote_handler(name, slot_id);
        });
    }

    Connection on_request_impl(std::string_view event_name, std::move_only_function<ImmerValue(const ImmerValue&)> handler) {
        std::string name(event_name);
        request_handlers_[name] = std::move(handler);
        
        return Connection([this, name]() {
            request_handlers_.erase(name);
        });
    }

    Connection bridge_to_local(std::string_view event_name) {
        return subscribe_remote_impl(event_name,
                                     [this, name = std::string(event_name)](const ImmerValue& v) { bus_.publish(name, v); });
    }

    std::size_t poll() {
        if (!connected_) {
            return 0;
        }

        std::size_t count = 0;

        auto process_received = [&](const ipc::Channel::ReceivedMessage& msg) {
            process_message_full(msg);
            ++count;
        };

        if (channel_pair_) {
            while (auto msg = channel_pair_->tryReceive()) {
                process_received(*msg);
            }
        } else if (channel_) {
            while (auto msg = channel_->tryReceive()) {
                process_received(*msg);
            }
        }

        return count;
    }

    std::size_t poll(std::chrono::milliseconds timeout) {
        if (!connected_) {
            return 0;
        }

        auto start = std::chrono::steady_clock::now();
        std::size_t total = 0;

        while (std::chrono::steady_clock::now() - start < timeout) {
            std::size_t count = poll();
            total += count;
            if (count == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        return total;
    }

    std::optional<ImmerValue> send(std::string_view event_name, const ImmerValue& payload,
                              std::chrono::milliseconds timeout) {
        if (!connected_ || !channel_pair_) {
            return std::nullopt;
        }

        uint64_t req_id = next_request_id_++;

        // Request envelope with ID
        ImmerValue envelope =
            ImmerValue::map({{"n", std::string(event_name)}, {"d", payload}, {"r", static_cast<int64_t>(req_id)}});

        if (!channel_pair_->post(detail::IPC_EVT_REQUEST, envelope)) {
            return std::nullopt;
        }

        // Wait for response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (auto msg = channel_pair_->tryReceive()) {
                if (msg->msgId == detail::IPC_EVT_RESPONSE) {
                    const ImmerValue& env = msg->data;
                    if (auto rid = try_get_int(env, "r"); rid && static_cast<uint64_t>(*rid) == req_id) {
                        return try_get_value(env, "d");
                    }
                } else {
                    process_message(msg->msgId, msg->data);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        return std::nullopt;
    }

    bool connected() const { return connected_; }
    const std::string& channel_name() const { return channel_name_; }
    const std::string& last_error() const { return last_error_; }
    EventBus& bus() { return bus_; }

private:
    // Full message processing with domain support
    void process_message_full(const ipc::Channel::ReceivedMessage& msg) {
        try {
            // First, dispatch to domain handlers (regardless of msgId type)
            // Domain handlers receive the raw payload
            dispatch_to_domain_handlers(msg, msg.data);

            // Then process as before for named event handlers
            process_message(msg.msgId, msg.data);
        } catch (...) {
            // Ignore malformed messages
        }
    }

    void process_message(uint32_t msgId, const ImmerValue& envelope) {
        try {
            auto name = try_get_string(envelope, "n");
            auto payload = try_get_value(envelope, "d");

            if (!name || !payload) {
                return;
            }

            if (msgId == detail::IPC_EVT_REQUEST) {
                handle_request(*name, *payload, envelope);
            } else {
                dispatch_to_handlers(*name, *payload);
            }
        } catch (...) {
            // Ignore malformed messages
        }
    }

    void handle_request(const std::string& name, const ImmerValue& payload, const ImmerValue& envelope) {
        auto it = request_handlers_.find(name);
        if (it == request_handlers_.end()) {
            return;
        }

        ImmerValue response = it->second(payload);
        auto req_id = try_get_value(envelope, "r");
        if (!req_id) {
            return;
        }

        ImmerValue resp_envelope = ImmerValue::map({{"n", name}, {"d", response}, {"r", *req_id}});

        if (channel_pair_) {
            channel_pair_->post(detail::IPC_EVT_RESPONSE, resp_envelope);
        } else if (channel_) {
            channel_->post(detail::IPC_EVT_RESPONSE, resp_envelope);
        }
    }

    void dispatch_to_handlers(const std::string& name, const ImmerValue& payload) {
        auto it = remote_handlers_.find(name);
        if (it != remote_handlers_.end()) {
            for (const auto& rh : it->second) {
                if (rh.handler) {
                    rh.handler(payload);
                }
            }
        }
    }

    void remove_remote_handler(const std::string& name, uint64_t slot_id) {
        auto it = remote_handlers_.find(name);
        if (it != remote_handlers_.end()) {
            auto& handlers = it->second;
            std::erase_if(handlers, [slot_id](const RemoteHandler& rh) {
                return rh.id == slot_id;
            });
            if (handlers.empty()) {
                remote_handlers_.erase(it);
            }
        }
    }

    // Helper functions for safe ImmerValue access
    static std::optional<std::string> try_get_string(const ImmerValue& v, const char* key) {
        try {
            return v.at(key).as<std::string>();
        } catch (...) {
            return std::nullopt;
        }
    }

    static std::optional<int64_t> try_get_int(const ImmerValue& v, const char* key) {
        try {
            return v.at(key).as<int64_t>();
        } catch (...) {
            return std::nullopt;
        }
    }

    static std::optional<ImmerValue> try_get_value(const ImmerValue& v, const char* key) {
        try {
            return v.at(key);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string channel_name_;
    EventBus& bus_;
    Role role_;
    bool connected_ = false;
    std::string last_error_;

    std::unique_ptr<ipc::Channel> channel_;
    std::unique_ptr<ipc::ChannelPair> channel_pair_;

    // Remote event handlers with lifecycle management
    std::unordered_map<std::string, std::vector<RemoteHandler>> remote_handlers_;
    uint64_t next_slot_id_ = 1;
    
    // Request-response handlers
    std::unordered_map<std::string, std::move_only_function<ImmerValue(const ImmerValue&)>> request_handlers_;
    uint64_t next_request_id_ = 1;

    // Domain handlers - keyed by domain enum
    std::unordered_map<uint8_t, std::vector<DomainHandler>> domain_handlers_;

public:
    // Domain subscription implementation
    Connection subscribe_domain_impl(ipc::MessageDomain domain,
                                     std::move_only_function<void(const RemoteBus::DomainEnvelope&, const ImmerValue&) const> handler) {
        uint64_t slot_id = next_slot_id_++;
        uint8_t domain_key = static_cast<uint8_t>(domain);
        
        domain_handlers_[domain_key].push_back({slot_id, std::move(handler)});
        
        return Connection([this, domain_key, slot_id]() {
            remove_domain_handler(domain_key, slot_id);
        });
    }

    void unsubscribe_domain(ipc::MessageDomain domain) {
        uint8_t domain_key = static_cast<uint8_t>(domain);
        domain_handlers_.erase(domain_key);
    }

private:
    void remove_domain_handler(uint8_t domain_key, uint64_t slot_id) {
        auto it = domain_handlers_.find(domain_key);
        if (it != domain_handlers_.end()) {
            auto& handlers = it->second;
            std::erase_if(handlers, [slot_id](const DomainHandler& dh) {
                return dh.id == slot_id;
            });
            if (handlers.empty()) {
                domain_handlers_.erase(it);
            }
        }
    }

    void dispatch_to_domain_handlers(const ipc::Channel::ReceivedMessage& msg, const ImmerValue& payload) {
        uint8_t domain_key = static_cast<uint8_t>(msg.domain);
        auto it = domain_handlers_.find(domain_key);
        if (it != domain_handlers_.end()) {
            RemoteBus::DomainEnvelope envelope{
                .msgId = msg.msgId,
                .timestamp = msg.timestamp,
                .domain = msg.domain,
                .flags = msg.flags,
                .requestId = msg.requestId
            };
            for (const auto& dh : it->second) {
                if (dh.handler) {
                    dh.handler(envelope, payload);
                }
            }
        }
    }
};

// ============================================================================
// RemoteBus
// ============================================================================

RemoteBus::RemoteBus(std::string_view channel_name, EventBus& bus, Role role, std::size_t capacity)
    : impl_(std::make_unique<Impl>(channel_name, bus, role, capacity)) {}

RemoteBus::~RemoteBus() = default;
RemoteBus::RemoteBus(RemoteBus&&) noexcept = default;
RemoteBus& RemoteBus::operator=(RemoteBus&&) noexcept = default;

bool RemoteBus::post_remote(std::string_view event_name, const ImmerValue& payload) {
    return impl_->post_remote(event_name, payload);
}

bool RemoteBus::broadcast(std::string_view event_name, const ImmerValue& payload) {
    return impl_->broadcast(event_name, payload);
}

Connection RemoteBus::subscribe_remote_impl(std::string_view event_name, std::move_only_function<void(const ImmerValue&) const> handler) {
    return impl_->subscribe_remote_impl(event_name, std::move(handler));
}

Connection RemoteBus::on_request_impl(std::string_view event_name, std::move_only_function<ImmerValue(const ImmerValue&)> handler) {
    return impl_->on_request_impl(event_name, std::move(handler));
}

Connection RemoteBus::bridge_to_local(std::string_view event_name) {
    return impl_->bridge_to_local(event_name);
}

std::size_t RemoteBus::poll() {
    return impl_->poll();
}

std::size_t RemoteBus::poll(std::chrono::milliseconds timeout) {
    return impl_->poll(timeout);
}

std::optional<ImmerValue> RemoteBus::send(std::string_view event_name, const ImmerValue& payload,
                                     std::chrono::milliseconds timeout) {
    return impl_->send(event_name, payload, timeout);
}

bool RemoteBus::connected() const {
    return impl_->connected();
}

const std::string& RemoteBus::channel_name() const {
    return impl_->channel_name();
}

const std::string& RemoteBus::last_error() const {
    return impl_->last_error();
}

EventBus& RemoteBus::bus_ref() {
    return impl_->bus();
}

Connection RemoteBus::subscribe_domain_impl(MessageDomain domain,
                                            std::move_only_function<void(const DomainEnvelope&, const ImmerValue&) const> handler) {
    return impl_->subscribe_domain_impl(domain, std::move(handler));
}

void RemoteBus::unsubscribe_domain(MessageDomain domain) {
    impl_->unsubscribe_domain(domain);
}

} // namespace lager_ext
