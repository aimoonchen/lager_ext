
# Lager 核心概念完整指南

本文档介绍 lager 库中的所有核心概念：store、cursor、sensor、state、lens、effects、deps、context，以及它们如何协同工作。

> **基于源码的深入分析** - 本指南结合 Lager 源码实现，提供准确的技术细节。

## 架构概览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Lager Architecture                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                            STORE                                     │   │
│  │  ┌─────────┐    dispatch()    ┌─────────┐                           │   │
│  │  │ Action  │ ───────────────► │ Reducer │                           │   │
│  │  └─────────┘                  └────┬────┘                           │   │
│  │                                    │                                 │   │
│  │                    ┌───────────────┼───────────────┐                │   │
│  │                    │               │               │                │   │
│  │                    ▼               ▼               ▼                │   │
│  │              ┌─────────┐    ┌───────────┐    ┌─────────┐           │   │
│  │              │  Model  │    │  Effect   │    │  Deps   │           │   │
│  │              │ (State) │    │(Side-Eff) │    │(Service)│           │   │
│  │              └────┬────┘    └─────┬─────┘    └────┬────┘           │   │
│  │                   │               │               │                 │   │
│  │                   │               └───────┬───────┘                 │   │
│  │                   │                       │                         │   │
│  │                   │               ┌───────▼───────┐                 │   │
│  │                   │               │    Context    │                 │   │
│  │                   │               │ (dispatch+deps)│                 │   │
│  │                   │               └───────────────┘                 │   │
│  └───────────────────┼─────────────────────────────────────────────────┘   │
│                      │                                                      │
│        ┌─────────────┼─────────────┐                                       │
│        │             │             │                                        │
│        ▼             ▼             ▼                                        │
│   ┌─────────┐   ┌─────────┐   ┌─────────┐                                  │
│   │ Cursor  │   │ Reader  │   │  Lens   │  ← 访问/修改 State 的方式        │
│   │(R/W)    │   │(R only) │   │(Focus)  │                                  │
│   └─────────┘   └─────────┘   └─────────┘                                  │
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │  Independent State Containers (outside store)                        │  │
│   │  ┌─────────┐        ┌─────────┐                                     │  │
│   │  │  State  │        │ Sensor  │                                     │  │
│   │  │ (R/W)   │        │(R only) │                                     │  │
│   │  └─────────┘        └─────────┘                                     │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 概念详解

| 概念 | 类型 | 作用 |
|------|------|------|
| **Store** | 核心容器 | 包含 Model + Reducer + EventLoop，管理整个应用状态 |
| **Model** | 数据 | 应用的不可变状态 |
| **Action** | 数据 | 描述"发生了什么"的事件 |
| **Reducer** | 函数 | `(Model, Action) -> Model` 或 `-> pair<Model, Effect>` |
| **Effect** | 函数 | 副作用，如 IO、网络请求、定时器 |
| **Deps** | 依赖 | 注入到 Effect 中的服务（如 HTTP 客户端） |
| **Context** | 上下文 | 包含 `dispatch()` 和 `deps`，传给 Effect |
| **Reader** | 只读视图 | 可以读取和监听状态变化 |
| **Cursor** | 读写视图 | Reader + 写入能力 |
| **Lens** | 聚焦器 | 聚焦到状态的某个部分 |
| **State** | 独立状态 | Store 之外的可观察状态容器（可读写） |
| **Sensor** | 独立只读 | Store 之外的只读值来源（如系统时间） |

## 完整示例代码

```cpp
// lager_complete_example.cpp
// 展示 lager 所有核心概念的最小示例

#include <lager/store.hpp>
#include <lager/state.hpp>
#include <lager/sensor.hpp>
#include <lager/cursor.hpp>
#include <lager/lens.hpp>
#include <lager/lenses/attr.hpp>
#include <lager/watch.hpp>
#include <lager/effect.hpp>
#include <lager/deps.hpp>
#include <lager/context.hpp>
#include <lager/event_loop/manual.hpp>

#include <iostream>
#include <string>
#include <variant>
#include <chrono>

// ============================================================
// 1. MODEL - 应用状态
// ============================================================
struct AppModel {
    std::string username;
    int counter = 0;
    bool logged_in = false;
};

// 用于 lens 访问的属性宏（或手动定义）
LAGER_STRUCT(AppModel, username, counter, logged_in);

// ============================================================
// 2. ACTIONS - 描述事件
// ============================================================
struct Increment { int amount = 1; };
struct Decrement { int amount = 1; };
struct Login { std::string user; };
struct Logout {};
struct FetchDataComplete { std::string data; };

using AppAction = std::variant<
    Increment, 
    Decrement, 
    Login, 
    Logout, 
    FetchDataComplete
>;

// ============================================================
// 3. DEPS - 依赖注入的服务
// ============================================================
struct Logger {
    void log(const std::string& msg) const {
        std::cout << "[LOG] " << msg << std::endl;
    }
};

struct DataService {
    std::string fetch_data() const {
        return "Data from server";
    }
};

// ============================================================
// 4. EFFECT - 副作用
// ============================================================
// Effect 签名: (Context&) -> void 或 -> future
// Context 提供 dispatch() 和 deps

using AppDeps = lager::deps<Logger&, DataService&>;
using AppEffect = lager::effect<AppAction, AppDeps>;

// ============================================================
// 5. REDUCER - 纯函数处理状态转换
// ============================================================
// 返回类型: Model 或 pair<Model, Effect>

std::pair<AppModel, AppEffect> update(AppModel model, AppAction action) {
    return std::visit(lager::visitor{
        [&](Increment a) -> std::pair<AppModel, AppEffect> {
            model.counter += a.amount;
            // 返回无副作用
            return {model, lager::noop};
        },
        
        [&](Decrement a) -> std::pair<AppModel, AppEffect> {
            model.counter -= a.amount;
            return {model, lager::noop};
        },
        
        [&](Login a) -> std::pair<AppModel, AppEffect> {
            model.username = a.user;
            model.logged_in = true;
            
            // 返回副作用：登录后获取数据
            return {model, [](auto& ctx) {
                // 从 context 获取 deps
                auto& logger = ctx.template get<Logger&>();
                auto& service = ctx.template get<DataService&>();
                
                logger.log("User logged in, fetching data...");
                std::string data = service.fetch_data();
                
                // 通过 context dispatch 新 action
                ctx.dispatch(FetchDataComplete{data});
            }};
        },
        
        [&](Logout a) -> std::pair<AppModel, AppEffect> {
            model.username = "";
            model.logged_in = false;
            
            return {model, [](auto& ctx) {
                ctx.template get<Logger&>().log("User logged out");
            }};
        },
        
        [&](FetchDataComplete a) -> std::pair<AppModel, AppEffect> {
            // 处理异步获取的数据
            return {model, [data = a.data](auto& ctx) {
                ctx.template get<Logger&>().log("Received: " + data);
            }};
        },
    }, action);
}

// ============================================================
// 6. MAIN - 组装所有组件
// ============================================================
int main() {
    // --- 创建依赖服务 ---
    Logger logger;
    DataService data_service;
    
    // --- 创建事件循环 ---
    auto loop = lager::with_manual_event_loop{};
    
    // --- 创建 STORE ---
    auto store = lager::make_store<AppAction>(
        AppModel{"", 0, false},              // 初始状态
        loop,                                 // 事件循环
        lager::with_reducer(update),          // reducer
        lager::with_deps(                     // 依赖注入
            std::ref(logger),
            std::ref(data_service)
        )
    );
    
    // ============================================================
    // 7. READER & WATCH - 监听状态变化
    // ============================================================
    lager::reader<AppModel> reader = store;  // store 可以转为 reader
    
    lager::watch(reader, [](const AppModel& m) {
        std::cout << "[WATCH] State changed: "
                  << "user=" << m.username 
                  << ", counter=" << m.counter 
                  << ", logged_in=" << m.logged_in << std::endl;
    });
    
    // ============================================================
    // 8. LENS - 聚焦到状态的某个部分
    // ============================================================
    // 创建聚焦到 counter 的 lens
    auto counter_lens = lager::lenses::attr(&AppModel::counter);
    
    // 使用 lens 读取
    int current_counter = lager::view(counter_lens, store.get());
    std::cout << "Counter via lens: " << current_counter << std::endl;
    
    // 使用 zoom 创建聚焦的 cursor
    auto counter_cursor = store.zoom(counter_lens);
    std::cout << "Counter via cursor: " << counter_cursor.get() << std::endl;
    
    // ============================================================
    // 9. CURSOR - 读写视图
    // ============================================================
    lager::cursor<AppModel> cursor = store;  // store 也是 cursor
    
    // cursor 可以直接设置值（绕过 reducer，慎用）
    // cursor.set(AppModel{"direct", 100, true});
    
    // ============================================================
    // 10. STATE - 独立的可观察状态容器
    // ============================================================
    // state 是 store 之外的独立状态，不通过 reducer
    lager::state<int> external_counter{42};
    
    lager::watch(external_counter, [](int v) {
        std::cout << "[EXTERNAL STATE] Changed to: " << v << std::endl;
    });
    
    // 直接修改 state
    external_counter.set(100);
    lager::commit(external_counter);  // 触发通知
    
    // ============================================================
    // 11. SENSOR - 只读的外部值来源
    // ============================================================
    // sensor 从外部获取值（如系统时间、配置等）
    auto time_sensor = lager::make_sensor([]() {
        return std::chrono::system_clock::now();
    });
    
    // 可以 watch sensor
    lager::watch(time_sensor, [](auto time) {
        auto t = std::chrono::system_clock::to_time_t(time);
        std::cout << "[SENSOR] Time updated: " << std::ctime(&t);
    });
    
    // ============================================================
    // 12. 运行：Dispatch Actions
    // ============================================================
    std::cout << "\n=== Dispatching Actions ===" << std::endl;
    
    // dispatch action 到 store
    store.dispatch(Increment{5});
    loop.step();  // manual event loop 需要手动 step
    
    store.dispatch(Login{"Alice"});
    loop.step();  // 处理 Login
    loop.step();  // 处理 Login 产生的 Effect (FetchDataComplete)
    
    store.dispatch(Decrement{2});
    loop.step();
    
    store.dispatch(Logout{});
    loop.step();
    
    // ============================================================
    // 13. CONTEXT - 在 Effect 中使用
    // ============================================================
    // Context 在 Effect 内部使用，提供:
    // - ctx.dispatch(action): 派发新 action
    // - ctx.get<T>(): 获取依赖
    // (见上面 update 函数中的 Effect 实现)
    
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Username: " << store->username << std::endl;
    std::cout << "Counter: " << store->counter << std::endl;
    std::cout << "Logged in: " << store->logged_in << std::endl;
    
    return 0;
}
```

## 概念总结表

| 概念 | 创建方式 | 主要用途 |
|------|---------|---------|
| **Store** | `lager::make_store<Action>(model, loop, ...)` | 应用状态的核心容器 |
| **Action** | `std::variant<A1, A2, ...>` | 描述事件 |
| **Reducer** | `update(Model, Action) -> Model/pair` | 状态转换逻辑 |
| **Effect** | `lager::effect<Action, Deps>` | 副作用函数 |
| **Deps** | `lager::deps<Service1&, Service2&>` | 依赖类型列表 |
| **Context** | Effect 参数 | 提供 dispatch + deps |
| **Reader** | `lager::reader<T> = store` | 只读状态视图 |
| **Cursor** | `lager::cursor<T> = store` | 读写状态视图 |
| **Lens** | `lager::lenses::attr(&T::member)` | 聚焦到状态的某部分 |
| **State** | `lager::state<T>{initial}` | 独立可观察状态 |
| **Sensor** | `lager::make_sensor(fn)` | 只读外部值来源 |
| **Watch** | `lager::watch(observable, callback)` | 监听变化 |
| **Commit** | `lager::commit(state)` | 触发 state 通知 |

## 数据流图

```
User Action
    │
    ▼
store.dispatch(action)
    │
    ▼
┌───────────────────┐
│  Event Loop       │  ← 确保单线程处理
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  Reducer          │  ← 纯函数，可测试
│  (model, action)  │
│        │          │
│        ▼          │
│  new_model        │
│  + effect         │
└─────────┬─────────┘
          │
    ┌─────┴─────┐
    │           │
    ▼           ▼
new_model    effect(ctx)
    │           │
    ▼           ▼
watchers     side-effects
notified     executed
    │           │
    ▼           ▼
UI update    ctx.dispatch()
             (new actions)
```

## Reader vs Cursor vs State vs Sensor

```
                    ┌─────────────┐
                    │   Store     │
                    │  (核心)     │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │   Reader    │ │   Cursor    │ │    Lens     │
    │  (只读)     │ │  (读写)     │ │  (聚焦)     │
    │             │ │             │ │             │
    │  .get()     │ │  .get()     │ │  view()     │
    │  watch()    │ │  .set()     │ │  set()      │
    │             │ │  watch()    │ │  over()     │
    └─────────────┘ └─────────────┘ └─────────────┘


    ┌─────────────────────────────────────────────┐
    │         独立状态容器 (Store 之外)            │
    │                                             │
    │  ┌─────────────┐       ┌─────────────┐     │
    │  │   State     │       │   Sensor    │     │
    │  │  (读写)     │       │  (只读)     │     │
    │  │             │       │             │     │
    │  │  .get()     │       │  .get()     │     │
    │  │  .set()     │       │  watch()    │     │
    │  │  watch()    │       │  recompute()│     │
    │  │  commit()   │       │             │     │
    │  └─────────────┘       └─────────────┘     │
    │                                             │
    └─────────────────────────────────────────────┘
```

## 使用场景对比

| 场景 | 推荐使用 |
|------|---------|
| 应用主状态管理 | **Store** |
| UI 组件只需读取状态 | **Reader** |
| UI 组件需要修改状态 | **Cursor** (通过 dispatch) 或 **Lens** (聚焦后 set) |
| 聚焦到嵌套状态的某部分 | **Lens** + `store.zoom(lens)` |
| 独立于 Store 的临时状态 | **State** |
| 外部只读数据源（时间、配置等） | **Sensor** |
| 监听任何状态变化 | **watch()** |
| 批量修改后统一通知 | **commit()** |

## 注意事项

1. **Store 是单向数据流的核心**，所有状态变更应该通过 `dispatch(action)` → `reducer` 的流程

2. **Cursor.set() 绕过 Reducer**，应谨慎使用，可能破坏状态一致性

3. **Effect 是处理副作用的唯一方式**，保持 Reducer 纯净

4. **Deps 通过依赖注入**，使 Effect 可测试（可以注入 mock 服务）

5. **State 和 Sensor 是独立于 Store 的**，适用于不需要 Redux 模式的简单场景

6. **Lens 是函数式的聚焦工具**，可以组合使用访问深层嵌套结构

---

## 深入：Lens 的 van Laarhoven 实现

Lager 的 Lens 实现基于 **van Laarhoven 表示法**，这是函数式编程中的标准 Lens 表示。

### 核心思想

```
Lens s a = ∀f. Functor f ⇒ (a → f a) → s → f s
```

一个 Lens 本质上是一个函数，它接受一个 "聚焦函数" 和一个 "整体"，返回一个 "包装在 Functor 中的整体"。

### 两个关键 Functor

Lager 使用两种不同的 Functor 来实现 `view`、`set`、`over` 操作：

```cpp
// 1. const_functor - 用于 view 操作（只读取，忽略修改）
template <typename T>
struct const_functor {
    T value;
    
    // fmap: 忽略函数，保持原值
    template <typename Fn>
    const_functor operator()(Fn&&) && {
        return std::move(*this);  // 不调用 Fn，直接返回
    }
};

// 2. identity_functor - 用于 set/over 操作（应用修改）
template <typename T>
struct identity_functor {
    T value;
    
    // fmap: 应用函数
    template <typename Fn>
    auto operator()(Fn&& f) && {
        return make_identity_functor(f(std::forward<T>(value)));
    }
};
```

### 操作实现

```cpp
// view: 使用 const_functor 提取值
template <typename Lens, typename Whole>
auto view(Lens lens, Whole&& whole) {
    return lens([](auto&& x) { 
        return const_functor<std::decay_t<decltype(x)>>{x}; 
    })(whole).value;
}

// set: 使用 identity_functor 替换值
template <typename Lens, typename Whole, typename Part>
auto set(Lens lens, Whole&& whole, Part&& part) {
    return lens([&](auto&&) { 
        return identity_functor<std::decay_t<Part>>{part}; 
    })(whole).value;
}

// over: 使用 identity_functor 应用函数
template <typename Lens, typename Whole, typename Fn>
auto over(Lens lens, Whole&& whole, Fn&& fn) {
    return lens([&](auto&& x) { 
        return identity_functor{fn(x)}; 
    })(whole).value;
}
```

### Lens 组合

Lens 可以用 `|` 运算符组合，实现深层嵌套访问：

```cpp
// 从左到右组合
auto street_lens = attr(&Person::address) | attr(&Address::street);

// 等价于函数组合
auto street_lens = [](auto f) {
    return attr(&Person::address)([&](auto& addr) {
        return attr(&Address::street)(f)(addr);
    });
};
```

**为什么这样可以组合？** 因为 Functor 满足结合律：

```
(L1 | L2)(f) = L1(L2(f))
```

---

## 深入：Tags 和通知策略

Lager 的 `state` 支持两种通知策略，通过 Tag 类型参数控制。

### automatic_tag vs transactional_tag

| Tag | 行为 | 源码位置 |
|-----|------|---------|
| `automatic_tag` | 每次 `set()` 后立即通知所有 watchers | `lager/tags.hpp` |
| `transactional_tag` | 累积修改，直到调用 `commit()` 才通知 | `lager/tags.hpp` |

### 实现原理

```cpp
// state_node 根据 Tag 类型选择不同行为
template <typename T, typename TagT = transactional_tag>
class state_node : public reader_node<T> {
    void send_up(const value_type& value) final {
        this->push_down(value);  // 更新内部值
        
        if constexpr (std::is_same_v<TagT, automatic_tag>) {
            this->send_down();   // 立即传播给子节点
            this->notify();       // 立即通知 watchers
        }
        // transactional_tag: 什么都不做，等待 commit()
    }
};

// commit() 触发延迟的通知
template <typename Node>
void commit(Node& node) {
    node.send_down();  // 传播到子节点
    node.notify();      // 通知所有 watchers
}
```

### 使用场景

```cpp
// 场景1：实时响应（如滑块控件）
lager::state<int, lager::automatic_tag> slider_value{50};
slider_value.set(60);  // 立即触发 UI 更新

// 场景2：批量更新（如表单提交）
lager::state<FormData, lager::transactional_tag> form_data{};
form_data.set(updated_name);     // 不触发
form_data.set(updated_email);    // 不触发
form_data.set(updated_phone);    // 不触发
lager::commit(form_data);        // 一次性通知，避免多次重绘
```

---

## 深入：节点层次结构

Lager 内部使用节点树来管理状态传播。

### 节点类型

```
                    ┌──────────────────┐
                    │   root_node      │  ← 抽象基类
                    └────────┬─────────┘
                             │
           ┌─────────────────┼─────────────────┐
           │                 │                 │
           ▼                 ▼                 ▼
    ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
    │ reader_node │   │ cursor_node │   │ sensor_node │
    │ <T>         │   │ <T>         │   │ <T>         │
    └──────┬──────┘   └──────┬──────┘   └─────────────┘
           │                 │
           ▼                 ▼
    ┌─────────────┐   ┌─────────────┐
    │ state_node  │   │ store_node  │
    │ <T, Tag>    │   │ <Action,    │
    │             │   │  Model,     │
    │             │   │  Deps>      │
    └─────────────┘   └─────────────┘
```

### 数据传播方向

```
       send_up()          send_down()
    ──────────────►    ◄──────────────
    
    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
    │  Parent     │───►│   Node      │───►│   Child     │
    │  Node       │◄───│             │◄───│   Node      │
    └─────────────┘    └─────────────┘    └─────────────┘
    
    push_down(): 更新自身的值（不通知）
    send_down(): 传播到子节点 + 通知 watchers
    send_up():   传播到父节点
```

### 为什么需要这种设计？

1. **Cursor.zoom()** 创建子节点，形成树结构
2. **值传播** 需要双向：父 → 子（读取），子 → 父（写入）
3. **通知优化** 通过 `transactional_tag` 可以批量处理

---

## 深入：Effect 的执行机制

### Effect 类型签名

```cpp
template <typename Action, typename Deps = lager::deps<>>
using effect = std::function<void(const context<Action, Deps>&)>;
```

### Effect 执行时机

```
dispatch(action)
    │
    ▼
┌─────────────────────────────────────────┐
│              Event Loop                  │
│                                          │
│  1. 调用 reducer(model, action)          │
│     → (new_model, effect)                │
│                                          │
│  2. 更新内部状态为 new_model             │
│                                          │
│  3. 通知所有 watchers                    │
│                                          │
│  4. 执行 effect(context)      ◄─── 异步  │
│     effect 内部可以:                     │
│     - ctx.dispatch(new_action)           │
│     - ctx.get<Dep>() 访问依赖            │
│                                          │
└─────────────────────────────────────────┘
```

### Effect 组合

```cpp
// 多个 effect 可以组合
auto effect1 = [](auto& ctx) { /* ... */ };
auto effect2 = [](auto& ctx) { /* ... */ };

// 使用 sequence 组合
auto combined = lager::sequence(effect1, effect2);

// 使用 batch 组合多个相同类型的 effect
std::vector<lager::effect<Action>> effects = {...};
auto batched = lager::batch(effects);
```

### noop vs 空 lambda

```cpp
// 推荐：使用 lager::noop 表示无副作用
return {model, lager::noop};

// 不推荐：空 lambda 也可以工作，但语义不清晰
return {model, [](auto&) {}};
```

---

## 深入：Context 的能力

Context 是 Effect 的执行环境，提供两个核心能力：

### 1. dispatch - 派发新 Action

```cpp
// 在 Effect 中派发新 Action
auto effect = [](auto& ctx) {
    // 异步操作完成后
    ctx.dispatch(DataLoaded{result});
};
```

**注意**: `dispatch` 是异步的，Action 会被放入事件队列，不会立即执行。

### 2. get<T>() - 获取依赖

```cpp
// 获取注入的依赖
auto effect = [](auto& ctx) {
    auto& logger = ctx.template get<Logger&>();
    auto& http = ctx.template get<HttpClient&>();
    
    logger.log("Fetching data...");
    http.get("/api/data").then([&ctx](auto data) {
        ctx.dispatch(DataReceived{data});
    });
};
```

### Context 的类型

```cpp
template <typename Action, typename Deps = lager::deps<>>
class context {
public:
    // 派发 Action
    void dispatch(Action action) const;
    
    // 获取依赖（编译时检查类型是否在 Deps 中）
    template <typename T>
    T& get() const;
};
```

---

## 深入：Event Loop 集成

### 支持的 Event Loop

| Event Loop | 头文件 | 用途 |
|------------|--------|------|
| `with_manual_event_loop` | `event_loop/manual.hpp` | 测试、控制台应用 |
| `with_boost_asio_event_loop` | `event_loop/boost_asio.hpp` | 服务器应用 |
| `with_qt_event_loop` | `event_loop/qt.hpp` | Qt GUI 应用 |
| `with_sdl_event_loop` | `event_loop/sdl.hpp` | 游戏开发 |

### Event Loop 接口

```cpp
struct event_loop_interface {
    // 将任务放入队列，在当前帧处理
    virtual void post(std::function<void()>) = 0;
    
    // 异步执行（可能在另一个线程）
    virtual void async(std::function<void()>) = 0;
    
    // 结束事件循环
    virtual void finish() = 0;
    
    // 暂停/恢复
    virtual void pause() = 0;
    virtual void resume() = 0;
};
```

### Manual Event Loop 示例

```cpp
auto loop = lager::with_manual_event_loop{};
auto store = lager::make_store<Action>(initial, loop, ...);

// 手动驱动事件循环
store.dispatch(action1);
store.dispatch(action2);

// 处理所有待处理的 action
while (loop.step()) {
    // 每次 step() 处理一个 action
}

// 或者一次性处理所有
loop.step();  // 处理 action1
loop.step();  // 处理 action2
```

---

## 最佳实践

### 1. Reducer 设计

```cpp
// ✅ 好：纯函数，无副作用
Model update(Model m, Action a) {
    return std::visit(visitor{...}, a);
}

// ❌ 差：在 reducer 中产生副作用
Model update(Model m, Action a) {
    std::cout << "Action received";  // 副作用！
    return m;
}
```

### 2. Effect 设计

```cpp
// ✅ 好：Effect 处理所有副作用
return {model, [](auto& ctx) {
    ctx.get<Logger&>().log("Action processed");
}};

// ✅ 好：异步操作完成后 dispatch 新 Action
return {model, [](auto& ctx) {
    fetch_data_async().then([&](auto data) {
        ctx.dispatch(DataReceived{data});
    });
}};
```

### 3. 依赖注入

```cpp
// ✅ 好：通过 Deps 注入，便于测试
using Deps = lager::deps<IHttpClient&, ILogger&>;

// 生产环境
auto store = make_store<Action>(
    model, loop,
    lager::with_deps(real_http_client, real_logger)
);

// 测试环境
auto store = make_store<Action>(
    model, loop,
    lager::with_deps(mock_http_client, mock_logger)
);
```

### 4. Lens 组合

```cpp
// ✅ 好：使用 lens 访问深层嵌套
auto email_lens = 
    attr(&AppState::user) 
    | attr(&User::profile) 
    | attr(&Profile::email);

auto email = view(email_lens, state);

// ❌ 差：手动解构
auto email = state.user.profile.email;  // 无法组合，无法复用
```

---

*最后更新: 2026年1月 - 添加 van Laarhoven Lens、Tags、节点层次结构等深入分析*
