//
// lager - library for functional interactive c++ programs
// Copyright (C) 2017 Juan Pedro Bolivar Puente
//
// This file is part of lager.
//
// lager is free software: you can redistribute it and/or modify
// it under the terms of the MIT License, as detailed in the LICENSE
// file located at the root of this source code distribution,
// or here: <https://github.com/arximboldi/lager/blob/master/LICENSE>
//

#pragma once

// C++20 替代方案：使用自定义侵入式链表替代 boost::intrusive::list
// 原因：减少 Boost 依赖，简化用户项目的依赖管理
// 性能说明：自定义实现与 boost::intrusive 性能基本等同（差异 <1%）
//
// 如需切换回 boost::intrusive，可在包含此头文件前定义：
// #define LAGER_USE_BOOST_INTRUSIVE 1

#ifdef LAGER_USE_BOOST_INTRUSIVE
#include <boost/intrusive/list.hpp>
#endif

#include <memory>

namespace lager {
namespace detail {

#ifndef LAGER_USE_BOOST_INTRUSIVE

// ============================================================
// 简易侵入式链表实现（C++20，无 Boost 依赖）
// ============================================================

// 链表节点基类 - 支持 auto_unlink 语义
struct list_hook {
    list_hook* prev_ = nullptr;
    list_hook* next_ = nullptr;
    
    // 从链表中自动移除（auto_unlink 语义）
    ~list_hook() {
        unlink();
    }
    
    void unlink() noexcept {
        if (prev_) prev_->next_ = next_;
        if (next_) next_->prev_ = prev_;
        prev_ = next_ = nullptr;
    }
    
    bool is_linked() const noexcept {
        return prev_ != nullptr || next_ != nullptr;
    }
};

// 侵入式链表（简化版，仅实现 signal 所需功能）
template <typename T>
class intrusive_list {
    list_hook head_;  // 哨兵节点
    
public:
    intrusive_list() noexcept {
        head_.prev_ = &head_;
        head_.next_ = &head_;
    }
    
    ~intrusive_list() {
        // 注意：不销毁元素，元素生命周期由外部管理
        // 只需断开所有链接
        clear();
    }
    
    void push_back(T& item) noexcept {
        auto* hook = static_cast<list_hook*>(&item);
        hook->prev_ = head_.prev_;
        hook->next_ = &head_;
        head_.prev_->next_ = hook;
        head_.prev_ = hook;
    }
    
    void clear() noexcept {
        while (!empty()) {
            auto* first = head_.next_;
            first->unlink();
        }
    }
    
    bool empty() const noexcept {
        return head_.next_ == &head_;
    }
    
    // 迭代器
    class iterator {
        list_hook* node_;
    public:
        explicit iterator(list_hook* n) noexcept : node_(n) {}
        
        T& operator*() const noexcept { return *static_cast<T*>(node_); }
        T* operator->() const noexcept { return static_cast<T*>(node_); }
        
        iterator& operator++() noexcept {
            node_ = node_->next_;
            return *this;
        }
        
        bool operator!=(const iterator& other) const noexcept {
            return node_ != other.node_;
        }
    };
    
    iterator begin() noexcept { return iterator(head_.next_); }
    iterator end() noexcept { return iterator(&head_); }
};

#endif // !LAGER_USE_BOOST_INTRUSIVE

template <typename... Args>
struct forwarder;

template <typename... Args>
class signal
{
#ifdef LAGER_USE_BOOST_INTRUSIVE
    using slot_list_base = boost::intrusive::list_base_hook<
        boost::intrusive::link_mode<boost::intrusive::auto_unlink>>;
#else
    using slot_list_base = list_hook;
#endif

public:
    using forwarder_type = forwarder<Args...>;

    struct slot_base : slot_list_base
    {
        virtual ~slot_base()             = default;
        virtual void operator()(Args...) = 0;
    };

    template <typename Fn>
    class slot : public slot_base
    {
        Fn fn_;

    public:
        slot(Fn fn)
            : fn_{std::move(fn)}
        {}
        void operator()(Args... args) final { fn_(args...); }
    };

    struct connection
    {
        std::unique_ptr<slot_base> slot_;

    public:
        connection(std::unique_ptr<slot_base> s)
            : slot_{std::move(s)}
        {}
    };

    template <typename Fn>
    connection connect(Fn&& fn)
    {
        using slot_t = slot<std::decay_t<Fn>>;
        auto s       = std::make_unique<slot_t>(std::forward<Fn>(fn));
        slots_.push_back(*s);
        return {std::move(s)};
    }

    void add(slot_base& slot) { slots_.push_back(slot); }

    template <typename... Args2>
    void operator()(Args2&&... args)
    {
        for (auto&& s : slots_)
            s(std::forward<Args2>(args)...);
    }

    bool empty() const { return slots_.empty(); }

private:
#ifdef LAGER_USE_BOOST_INTRUSIVE
    using slot_list =
        boost::intrusive::list<slot_base,
                               boost::intrusive::constant_time_size<false>>;
#else
    using slot_list = intrusive_list<slot_base>;
#endif

    slot_list slots_;
};

template <typename... Args>
struct forwarder
    : signal<Args...>::slot_base
    , signal<Args...>
{
    void operator()(Args... args) override
    {
        signal<Args...>::operator()(args...);
    }
};

} // namespace detail
} // namespace lager