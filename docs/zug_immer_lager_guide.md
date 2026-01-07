# Zug + Immer + Lager 完全使用指南

这三个库共同构成了 C++ 中函数式响应式编程的完整工具链：

| 库 | 功能 | 核心概念 |
|----|------|----------|
| **Zug** | Transducers（可组合的序列转换器） | 与序列无关的算法 |
| **Immer** | 不可变数据结构 | 持久化容器、结构共享 |
| **Lager** | 单向数据流架构 | Store、Reducer、Effect |

---

# 第一部分：Zug - Transducers 库

## 1. 什么是 Transducer？

Transducer 是**与序列类型无关的可组合转换**。传统的 `std::transform` 或 `std::filter` 绑定到迭代器，而 transducer 可以应用于任何"序列"——迭代器、流、信号、事件等。

```cpp
// 定义一个 transducer：过滤正数并转成字符串
auto xf = zug::filter([](int x) { return x > 0; })
        | zug::map([](int x) { return std::to_string(x); });

// 可以应用于任何序列类型！
```

## 2. 核心 API

### 2.1 基本使用：`into` 和 `into_vector`

```cpp
#include <zug/into.hpp>
#include <zug/into_vector.hpp>
#include <zug/transducer/filter.hpp>
#include <zug/transducer/map.hpp>

auto data = std::vector<int>{1, -2, 3, -4, 5};

// 方式1：指定目标容器
auto result = zug::into(
    std::vector<std::string>{},  // 目标容器
    zug::filter([](int x) { return x > 0; }) |
    zug::map([](int x) { return std::to_string(x); }),
    data
);
// result = {"1", "3", "5"}

// 方式2：自动推断 vector
auto result2 = zug::into_vector(
    zug::filter([](int x) { return x > 0; }),
    data
);
// result2 = {1, 3, 5}
```

### 2.2 归约：`transduce`

```cpp
#include <zug/transduce.hpp>

auto data = std::vector<int>{1, 2, 3, 4, 5};

// 先过滤偶数，再求和
int sum = zug::transduce(
    zug::filter([](int x) { return x % 2 == 0; }),  // transducer
    std::plus<int>{},  // reducing function
    0,                 // initial value
    data               // input sequence
);
// sum = 2 + 4 = 6
```

### 2.3 惰性序列：`sequence`

```cpp
#include <zug/sequence.hpp>

auto data = std::vector<int>{1, 2, 3, 4, 5};

// 创建惰性序列（不立即求值）
auto lazy_seq = zug::sequence(
    zug::filter([](int x) { return x % 2 == 0; }) |
    zug::map([](int x) { return x * 2; }),
    data
);

// 迭代时才计算
for (int x : lazy_seq) {
    std::cout << x << " ";  // 4 8
}
```

### 2.4 生成器和接收器：`run`

```cpp
#include <zug/run.hpp>
#include <zug/transducer/read.hpp>
#include <zug/transducer/write.hpp>

// 从 stdin 读取整数，过滤正数，输出到 stdout
zug::run(
    zug::read<int>(std::cin) |
    zug::filter([](int x) { return x > 0; }) |
    zug::write(std::cout)
);
```

## 3. Transducer 目录

### 3.1 映射类

```cpp
// map - 转换每个元素
zug::map([](int x) { return x * 2; })

// map_indexed - 带索引的映射
zug::map_indexed([](std::size_t i, int x) { return i + x; })
```

### 3.2 过滤类

```cpp
// filter - 保留满足条件的
zug::filter([](int x) { return x > 0; })

// remove - filter 的反向
zug::remove([](int x) { return x < 0; })  // 等价于 filter(x >= 0)

// take - 只取前 N 个
zug::take(5)

// take_while - 取满足条件的（遇到不满足立即停止）
zug::take_while([](int x) { return x < 10; })

// drop - 跳过前 N 个
zug::drop(3)

// drop_while - 跳过满足条件的
zug::drop_while([](int x) { return x < 0; })

// take_nth - 每 N 个取一个
zug::take_nth(3)  // 取第 0, 3, 6, 9... 个
```

### 3.3 去重类

```cpp
// dedupe - 去除连续重复
zug::dedupe  // [1,1,2,2,1] -> [1,2,1]

// distinct - 去除所有重复
zug::distinct  // [1,2,1,2,3] -> [1,2,3]
```

### 3.4 展平类

```cpp
// cat - 展平嵌套容器
zug::cat  // [[1,2], [3,4]] -> [1,2,3,4]

// mapcat - map + cat
zug::mapcat([](int x) { return std::vector{x, x*2}; })
// [1,2] -> [1,2,2,4]
```

### 3.5 分组类

```cpp
// partition - 按固定大小分组
zug::partition(3)  // [1,2,3,4,5,6] -> [[1,2,3], [4,5,6]]

// partition_by - 按条件分组（条件变化时开始新组）
zug::partition_by([](int x) { return x % 2; })
// [1,3,2,4,5] -> [[1,3], [2,4], [5]]
```

### 3.6 累积类

```cpp
// scan - 累积计算（输出每一步的结果）
zug::scan(0, std::plus<>{})
// [1,2,3,4] -> [1,3,6,10]

// enumerate - 添加索引
zug::enumerate
// ["a","b","c"] -> [(0,"a"), (1,"b"), (2,"c")]
```

### 3.7 生成类

```cpp
// range - 生成数字序列
zug::range(10)        // [0,1,2,...,9]
zug::range(1, 10)     // [1,2,...,9]
zug::range(0, 10, 2)  // [0,2,4,6,8]

// repeat - 无限重复
zug::repeat(42) | zug::take(3)  // [42,42,42]

// count - 从某数开始的无限序列
zug::count(0) | zug::take(5)  // [0,1,2,3,4]

// cycle - 循环容器
zug::cycle(std::vector{1,2,3}) | zug::take(7)  // [1,2,3,1,2,3,1]
```

### 3.8 组合类

```cpp
// chain - 连接多个序列
zug::chain(vec1, vec2, vec3)

// interleave - 交替取元素（用于多输入）
zug::into_vector(zug::interleave, vec1, vec2)
// [1,2,3], ["a","b"] -> [1,"a",2,"b",3]

// zip - 将多个输入打包成 tuple
zug::into_vector(zug::zip, vec1, vec2)
// [1,2,3], ["a","b"] -> [(1,"a"), (2,"b")]

// product - 笛卡尔积
zug::product(vec1, vec2)
```

## 4. Transducer 组合

Transducers **从左到右**组合（与函数组合方向相反）：

```cpp
// 使用 | 运算符（推荐）
auto xf = zug::filter(pred) | zug::map(f) | zug::take(10);

// 使用 comp 函数
auto xf = zug::comp(zug::filter(pred), zug::map(f), zug::take(10));

// 等价的 Haskell 伪代码：
// take 10 . map f . filter pred
```

## 5. 最佳实践

```cpp
// ✅ 推荐：将 transducer 定义为可复用组件
auto normalize = zug::filter([](int x) { return x > 0; })
               | zug::map([](int x) { return x / 100.0; });

auto data1 = zug::into_vector(normalize, input1);
auto data2 = zug::into_vector(normalize, input2);

// ✅ 推荐：使用 into_vector 简化常见情况
auto result = zug::into_vector(xf, data);

// ✅ 推荐：惰性序列用于大数据集或无限序列
auto lazy = zug::sequence(zug::range(1000000) | zug::filter(pred));
```

---

# 第二部分：Immer - 不可变数据结构

## 1. 核心概念

Immer 提供的容器在"修改"时**返回新容器**，原容器不变。通过**结构共享**，新旧容器共享大部分内存。

```cpp
#include <immer/vector.hpp>

const auto v1 = immer::vector<int>{1, 2, 3};
const auto v2 = v1.push_back(4);  // v1 不变！

assert(v1.size() == 3);  // v1 = [1,2,3]
assert(v2.size() == 4);  // v2 = [1,2,3,4]
```

## 2. 容器类型

### 2.1 `immer::vector<T>` - 不可变向量

```cpp
#include <immer/vector.hpp>

immer::vector<int> v;
v = v.push_back(1);          // 追加
v = v.set(0, 42);            // 修改索引位置
v = v.update(0, [](int x) { return x + 1; });  // 更新函数
v = v.take(5);               // 取前 5 个

int x = v[0];                // 随机访问 O(log32 N) ≈ O(1)
int y = v.at(0);             // 带边界检查
```

### 2.2 `immer::flex_vector<T>` - 灵活向量

支持 `vector` 的所有操作，**额外支持**：

```cpp
#include <immer/flex_vector.hpp>

immer::flex_vector<int> v{1, 2, 3, 4, 5};

v = v.push_front(0);         // 头部插入
v = v.drop(2);               // 跳过前 2 个 -> [3,4,5]
v = v.insert(1, 99);         // 在索引 1 插入
v = v.erase(2);              // 删除索引 2
v = v + other_flex;          // 连接（O(log N)）
```

### 2.3 `immer::map<K, V>` - 不可变哈希映射

```cpp
#include <immer/map.hpp>

immer::map<std::string, int> m;
m = m.set("a", 1);           // 插入/更新
m = m.update("a", [](int x) { return x + 1; });  // 更新函数
m = m.erase("a");            // 删除

int v = m["a"];              // 访问（不存在返回默认值）
const int* p = m.find("a");  // 查找（不存在返回 nullptr）
if (m.count("a")) { ... }    // 检查存在性
```

### 2.4 `immer::set<T>` - 不可变哈希集合

```cpp
#include <immer/set.hpp>

immer::set<int> s;
s = s.insert(1);
s = s.erase(1);

if (s.count(1)) { ... }
```

### 2.5 `immer::table<T>` - 具有主键的不可变表

用于存储有 ID 字段的结构体：

```cpp
#include <immer/table.hpp>

struct Item {
    std::string id;  // 第一个成员作为主键
    int value;
};

immer::table<Item> t;
t = t.insert({"item1", 42});

const Item& item = t["item1"];
const Item* p = t.find("item1");
t = t.erase("item1");
```

### 2.6 `immer::box<T>` - 不可变盒子（单值）

用于大型不可拷贝类型的共享：

```cpp
#include <immer/box.hpp>

immer::box<std::string> b{"hello"};
immer::box<std::string> b2 = b.update([](auto& s) { return s + " world"; });

std::string value = *b;      // 解引用
```

### 2.7 `immer::array<T>` - 不可变数组

类似 `std::array` 但不可变，适合小型固定大小集合。

## 3. Transient - 高效批量更新

当需要连续多次修改时，使用 `transient` 避免中间拷贝：

```cpp
#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>

immer::vector<int> build_vector(int n) {
    // 转换为可变的 transient
    auto t = immer::vector<int>{}.transient();
    
    for (int i = 0; i < n; ++i) {
        t.push_back(i);  // 就地修改，O(1)
    }
    
    // 转回不可变
    return t.persistent();
}
```

所有容器都有对应的 transient 类型：
- `vector_transient`
- `flex_vector_transient`
- `map_transient`
- `set_transient`
- `table_transient`

## 4. 算法

Immer 提供优化的算法，利用内部结构：

```cpp
#include <immer/algorithm.hpp>

immer::vector<int> v{1, 2, 3, 4, 5};

// 比 std::for_each 更快
immer::for_each(v, [](int x) { std::cout << x; });

// 比 std::accumulate 更快
int sum = immer::accumulate(v, 0);

// 比 std::all_of 更快
bool all_positive = immer::all_of(v, [](int x) { return x > 0; });
```

## 5. 内存策略

可以自定义内存管理：

```cpp
// 使用自定义堆
using my_vector = immer::vector<int, immer::default_memory_policy>;

// 启用垃圾回收（需要 libgc）
using gc_vector = immer::vector<int, immer::gc_memory_policy>;
```

## 6. 最佳实践

```cpp
// ✅ 始终使用 const
const auto v = immer::vector<int>{1, 2, 3};

// ✅ 批量更新使用 transient
auto result = [&] {
    auto t = v.transient();
    for (int i = 0; i < 1000; ++i)
        t.push_back(i);
    return t.persistent();
}();

// ✅ 大对象使用 box
immer::map<std::string, immer::box<LargeStruct>> m;

// ✅ 利用 identity 检测变化
if (v1.identity() == v2.identity()) {
    // 两个向量完全相同（共享底层存储）
}

// ❌ 避免在循环中不使用 transient
for (int i = 0; i < n; ++i)
    v = v.push_back(i);  // 每次都创建新容器！
```

---

# 第三部分：Lager - 单向数据流架构

## 1. 架构概述

```
     ┌─────────────────────────────────────┐
     │             Action                  │
     │   (用户意图，如 IncrementAction)     │
     └───────────────────┬─────────────────┘
                         │ dispatch()
                         ▼
     ┌─────────────────────────────────────┐
     │             Reducer                 │
     │   update(Model, Action) -> Model    │
     └───────────────────┬─────────────────┘
                         │
                         ▼
     ┌─────────────────────────────────────┐
     │              Model                  │
     │   (应用状态，不可变值类型)            │
     └───────────────────┬─────────────────┘
                         │ watch()
                         ▼
     ┌─────────────────────────────────────┐
     │               View                  │
     │   draw(Model) -> 渲染 UI            │
     └─────────────────────────────────────┘
```

## 2. 核心组件

### 2.1 Model（模型）

应用的完整状态，是一个**不可变值类型**：

```cpp
#include <immer/vector.hpp>
#include <lager/extra/struct.hpp>

struct todo_item {
    std::string text;
    bool done = false;
};

struct model {
    immer::vector<todo_item> items;
    std::string input;
};

// 注册结构体（用于序列化和调试）
LAGER_STRUCT(, model, items, input);
LAGER_STRUCT(, todo_item, text, done);
```

### 2.2 Action（动作）

表示用户意图的值类型，使用 `std::variant` 组合多种动作：

```cpp
struct add_todo_action {
    std::string text;
};

struct toggle_action {
    std::size_t index;
};

struct remove_action {
    std::size_t index;
};

using action = std::variant<add_todo_action, toggle_action, remove_action>;

LAGER_STRUCT(, add_todo_action, text);
LAGER_STRUCT(, toggle_action, index);
LAGER_STRUCT(, remove_action, index);
```

### 2.3 Reducer（归约器）

**纯函数**，接收当前状态和动作，返回新状态：

```cpp
#include <lager/util.hpp>

model update(model m, action a) {
    return std::visit(lager::visitor{
        [&](add_todo_action act) {
            m.items = m.items.push_back({act.text, false});
            m.input = "";
            return m;
        },
        [&](toggle_action act) {
            m.items = m.items.update(act.index, [](auto item) {
                item.done = !item.done;
                return item;
            });
            return m;
        },
        [&](remove_action act) {
            m.items = m.items.erase(act.index);
            return m;
        }
    }, a);
}
```

### 2.4 Store（存储）

胶合所有组件的核心：

```cpp
#include <lager/store.hpp>
#include <lager/event_loop/manual.hpp>

int main() {
    // 创建 store
    auto store = lager::make_store<action>(
        model{},                      // 初始状态
        lager::with_manual_event_loop{}  // 事件循环
    );
    
    // 监听状态变化
    watch(store, [](const model& m) {
        draw_ui(m);
    });
    
    // 分发动作
    store.dispatch(add_todo_action{"Buy milk"});
    store.dispatch(toggle_action{0});
    
    return 0;
}
```

## 3. Effect（副作用）

Reducer 必须是纯函数，副作用通过 Effect 处理：

```cpp
#include <lager/effect.hpp>

// Reducer 返回 pair<Model, Effect>
std::pair<model, lager::effect<action>> 
update(model m, action a) {
    return std::visit(lager::visitor{
        [&](load_action) {
            // 返回一个 effect，稍后执行
            return std::pair{m, [](auto&& ctx) {
                auto data = load_from_file("data.json");
                ctx.dispatch(data_loaded_action{data});
            }};
        },
        [&](save_action) {
            return std::pair{m, [m](auto&& ctx) {
                save_to_file("data.json", m);
            }};
        },
        [&](add_todo_action act) {
            m.items = m.items.push_back({act.text});
            return std::pair{m, lager::noop};  // 无副作用
        }
    }, a);
}
```

## 4. Cursor 和 Lens - 局部状态访问

### 4.1 Cursor

提供对状态子部分的读写访问：

```cpp
#include <lager/state.hpp>
#include <lager/cursor.hpp>

lager::state<model> state{model{}};

// 创建指向 items 的 cursor
lager::cursor<immer::vector<todo_item>> items = 
    state[&model::items].make();

// 读取
const auto& current_items = items.get();

// 写入
items.set(current_items.push_back({"new item"}));

// 监听
items.watch([](const auto& items) {
    std::cout << "Items changed: " << items.size() << "\n";
});
```

### 4.2 Lens

用于深层嵌套访问的光学抽象：

```cpp
#include <lager/lenses.hpp>
#include <lager/lenses/attr.hpp>
#include <lager/lenses/at.hpp>

using namespace lager::lenses;

// 定义 lens
auto first_item_text = at(0) | with_opt(attr(&todo_item::text));

// 读取
auto text = lager::view(first_item_text, model);  // std::optional<string>

// 写入
auto new_model = lager::set(first_item_text, model, "updated");

// 更新
auto new_model2 = lager::over(first_item_text, model, [](auto& s) {
    return s + "!";
});
```

### 4.3 常用 Lens

```cpp
// attr - 成员访问
attr(&person::name)

// at - 索引访问（返回 optional）
at(0)

// at_or - 索引访问，提供默认值
at_or(0, default_value)

// with_opt - 将 lens 提升到 optional
at(0) | with_opt(attr(&item::text))

// value_or - optional -> 值
at(0) | with_opt(attr(&item::text)) | value_or("")

// first, second - tuple/pair 访问
lenses::first
lenses::second

// fan - 组合多个 lens
lenses::fan(attr(&foo::a), attr(&foo::b))

// zip - 组合 lens 到 tuple lens
lenses::zip(lens1, lens2)
```

## 5. Event Loop 集成

### 5.1 手动事件循环

```cpp
#include <lager/event_loop/manual.hpp>

auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{}
);
```

### 5.2 Qt 集成

```cpp
#include <lager/event_loop/qt.hpp>

auto store = lager::make_store<action>(
    model{},
    lager::with_qt_event_loop{*qApp}
);
```

### 5.3 Boost.Asio 集成

```cpp
#include <lager/event_loop/boost_asio.hpp>

boost::asio::io_context io;
auto store = lager::make_store<action>(
    model{},
    lager::with_boost_asio_event_loop{io}
);
```

## 6. 依赖注入

将服务注入 Effect 的上下文：

```cpp
struct file_service {
    void save(const model& m);
    model load();
};

auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_deps(std::ref(my_file_service))
);

// 在 effect 中使用
auto effect = [](auto&& ctx) {
    auto& fs = ctx.template get<file_service>();
    fs.save(ctx.get());
};
```

## 7. 时间旅行调试

```cpp
#include <lager/debug/http_server.hpp>

auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_debugger(
        lager::http_debug_server{8080}
    )
);

// 访问 http://localhost:8080 进行时间旅行调试
```

## 8. 高级特性

### 8.1 自定义 Enhancer（中间件）

Enhancer 是一个高阶函数，可以修改 Store 的创建过程。它接收一个 `next` 函数并返回一个新函数：

```cpp
// Enhancer 签名
// (next) -> (action, model, reducer, loop, deps, tags) -> store

// 示例：日志中间件
auto with_logging = [](auto next) {
    return [next](auto action,
                  auto&& model,
                  auto&& reducer,
                  auto&& loop,
                  auto&& deps,
                  auto&& tags) {
        // 包装 reducer 添加日志
        auto logging_reducer = [reducer](auto&& m, auto&& a) {
            std::cout << "Action dispatched!" << std::endl;
            return reducer(std::forward<decltype(m)>(m),
                          std::forward<decltype(a)>(a));
        };
        
        return next(action,
                   std::forward<decltype(model)>(model),
                   logging_reducer,
                   std::forward<decltype(loop)>(loop),
                   std::forward<decltype(deps)>(deps),
                   std::forward<decltype(tags)>(tags));
    };
};

// 使用
auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    with_logging
);
```

### 8.2 内置 Enhancer

| Enhancer | 功能 |
|----------|------|
| `with_reducer(fn)` | 替换默认 reducer |
| `with_deps(args...)` | 添加依赖注入 |
| `with_futures` | 启用 future 支持 |
| `with_tags<Tags...>` | 添加标签到 Store |
| `with_debugger(server)` | 启用时间旅行调试 |

```cpp
// 组合多个 enhancer
auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_reducer(my_reducer),
    lager::with_deps(std::ref(service1), std::ref(service2)),
    lager::with_futures
);
```

### 8.3 Transactional 模式

默认情况下（`automatic_tag`），每个 action 处理后自动通知观察者。使用 `transactional_tag` 可以批量提交：

```cpp
#include <lager/commit.hpp>

// 创建 transactional store
auto store = lager::make_store<action, lager::transactional_tag>(
    model{},
    lager::with_manual_event_loop{}
);

// 分发多个 action
store.dispatch(action1{});
store.dispatch(action2{});
store.dispatch(action3{});

// 手动提交，通知观察者
lager::commit(store);
```

对于 `lager::state`：

```cpp
#include <lager/state.hpp>

// transactional state（默认）
lager::state<model, lager::transactional_tag> state{model{}};
state.set(new_model);
lager::commit(state);  // 手动提交

// automatic state
lager::state<model, lager::automatic_tag> auto_state{model{}};
auto_state.set(new_model);  // 自动通知观察者
```

### 8.4 Future 支持

启用 `with_futures` 后，`dispatch()` 返回 `lager::future`，可以链式调用：

```cpp
auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_futures
);

// dispatch 返回 future
store.dispatch(load_action{})
    .then([&] {
        return store.dispatch(process_action{});
    })
    .then([&] {
        std::cout << "All done!" << std::endl;
    });
```

`lager::future` API：

```cpp
// 链式调用
future.then([]{ /* 回调 */ });

// 并行等待
future1.also(std::move(future2));  // 两者都完成时继续
```

### 8.5 Effect 序列化

多个 Effect 可以串联执行：

```cpp
#include <lager/effect.hpp>

lager::effect<action> eff1 = [](auto& ctx) {
    // 第一个副作用
    ctx.dispatch(step1_done{});
};

lager::effect<action> eff2 = [](auto& ctx) {
    // 第二个副作用
    ctx.dispatch(step2_done{});
};

// 串联执行 eff1 → eff2
auto combined = lager::sequence(eff1, eff2);

// 在 reducer 中返回
std::pair<model, lager::effect<action>> update(model m, action a) {
    return {m, lager::sequence(eff1, eff2, eff3)};
}
```

### 8.6 Sensor - 外部数据源

`lager::sensor` 是一个惰性计算的只读数据源：

```cpp
#include <lager/sensor.hpp>

// 创建 sensor
auto time_sensor = lager::make_sensor([]() {
    return std::chrono::system_clock::now();
});

// 读取当前值
auto now = time_sensor.get();

// 监听变化（需要手动触发重计算）
watch(time_sensor, [](auto time) {
    std::cout << "Time updated!" << std::endl;
});

// 强制重新计算并通知
lager::commit(time_sensor);
```

### 8.7 Reader/Writer 分离

`cursor` 可以分离为只读 `reader` 和只写 `writer`：

```cpp
#include <lager/reader.hpp>
#include <lager/writer.hpp>

lager::state<model> state{model{}};

// 创建 cursor
auto cursor = state[&model::items].make();

// 分离为 reader（只读）
lager::reader<immer::vector<item>> reader = cursor;

// 分离为 writer（只写）
lager::writer<immer::vector<item>> writer = cursor;

// reader 只能读取
const auto& items = reader.get();
watch(reader, [](const auto& items) { /* ... */ });

// writer 只能写入
writer.set(new_items);
writer.update([](auto items) { return items.push_back(item{}); });
```

### 8.8 Xform - 派生 Cursor

使用 `xform` 创建派生视图：

```cpp
auto state = lager::state<model>{model{}};

// 只读派生（单向 transducer）
lager::reader<int> count = state.xform(
    zug::map([](const model& m) { return m.items.size(); })
);

// 读写派生（双向 transducer）
lager::cursor<int> value = state.xform(
    zug::map([](const model& m) { return m.value; }),     // 读取
    lager::update([](const model& m, int v) {             // 写入
        auto result = m;
        result.value = v;
        return result;
    })
);
```

### 8.9 依赖注入详解

```cpp
// 定义服务接口
struct file_service {
    void save(const std::string& data);
    std::string load();
};

struct http_service {
    std::string fetch(const std::string& url);
};

// 创建带依赖的 store
file_service fs;
http_service http;

auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_deps(
        std::ref(fs),                              // 按类型注入
        lager::dep::as<lager::dep::key<            // 按 key 注入
            struct http_key, http_service&>>(std::ref(http))
    )
);

// 在 Effect 中使用
lager::effect<action, lager::deps<file_service&>> eff = [](auto& ctx) {
    auto& fs = ctx.template get<file_service>();
    fs.save("data");
};

// 使用 key 获取
lager::effect<action, lager::deps<lager::dep::key<http_key, http_service&>>> 
http_eff = [](auto& ctx) {
    auto& http = ctx.template get<http_key>();
    auto data = http.fetch("http://api.example.com");
};
```

依赖规格类型：

| 规格 | 说明 |
|------|------|
| `dep::val<T>` | 值类型 |
| `dep::ref<T>` | 引用类型 |
| `dep::opt<T>` | 可选依赖 |
| `dep::fn<T>` | 延迟计算（函数提供） |
| `dep::key<K, T>` | 带 key 的依赖 |

```cpp
// 可选依赖
lager::deps<lager::dep::opt<logger&>> deps;
if (deps.has<logger>()) {
    deps.get<logger>().log("message");
}

// 延迟计算
lager::deps<lager::dep::fn<config>> deps;
auto cfg = deps.get<config>();  // 调用时计算
```

### 8.10 Context 和 Actions

`context` 提供了 `dispatch` 和事件循环访问：

```cpp
// Effect 签名
lager::effect<action> eff = [](const lager::context<action>& ctx) {
    // 分发新 action
    ctx.dispatch(another_action{});
    
    // 访问事件循环
    ctx.loop().post([&ctx] {
        ctx.dispatch(delayed_action{});
    });
    
    ctx.loop().async([&ctx] {
        // 异步任务
    });
    
    ctx.loop().pause();   // 暂停事件循环
    ctx.loop().resume();  // 恢复事件循环
    ctx.loop().finish();  // 结束事件循环
};
```

多 Action 类型支持：

```cpp
// 支持多种 action 类型
using my_actions = lager::actions<action_a, action_b, action_c>;

lager::effect<my_actions> multi_eff = [](const lager::context<my_actions>& ctx) {
    ctx.dispatch(action_a{});
    ctx.dispatch(action_b{});
};
```

### 8.11 时间旅行调试详解

```cpp
#include <lager/debug/debugger.hpp>
#include <lager/debug/http_server.hpp>

// 配置 HTTP 调试服务器
auto server = lager::http_debug_server{
    argc, argv,           // 程序参数（用于显示）
    8080,                 // 端口
    "/path/to/resources"  // 静态资源路径
};

auto store = lager::make_store<action>(
    model{},
    lager::with_manual_event_loop{},
    lager::with_debugger(server)
);

// 调试器 API（通过 HTTP）：
// GET  /api/           - 获取状态概要
// GET  /api/step/{n}   - 获取第 n 步的状态和 action
// POST /api/goto/{n}   - 跳转到第 n 步
// POST /api/undo       - 撤销
// POST /api/redo       - 重做
// POST /api/pause      - 暂停应用
// POST /api/resume     - 恢复应用
```

调试器动作类型：

```cpp
// 调试器包装后的 action 类型
using debug_action = std::variant<
    action,                           // 原始 action
    lager::debugger<...>::goto_action,  // 跳转
    lager::debugger<...>::undo_action,  // 撤销
    lager::debugger<...>::redo_action,  // 重做
    lager::debugger<...>::pause_action, // 暂停
    lager::debugger<...>::resume_action // 恢复
>;
```

### 8.12 LAGER_STRUCT 宏

用于自动派生结构体的常用功能：

```cpp
#include <lager/extra/struct.hpp>

// 外部声明
struct my_struct {
    int a;
    std::string b;
};
LAGER_STRUCT(my_namespace, my_struct, a, b);

// 嵌套声明（在结构体内部）
struct other_struct {
    int x;
    int y;
    LAGER_STRUCT_NESTED(other_struct, x, y);
};

// 模板支持
template<typename T>
struct generic_struct {
    T value;
};
LAGER_STRUCT_TEMPLATE(generic_struct, value);
```

派生的功能：
- `operator==` / `operator!=`（EQ）
- Boost.Hana 适配（HANA）
- Cereal 序列化支持
- 大小检查（SIZE_CHECK）

## 9. 完整示例：计数器

```cpp
#include <lager/store.hpp>
#include <lager/event_loop/manual.hpp>
#include <lager/extra/struct.hpp>
#include <iostream>
#include <variant>

// Model
struct model { int value = 0; };
LAGER_STRUCT(, model, value);

// Actions
struct increment {};
struct decrement {};
struct reset { int new_value = 0; };
using action = std::variant<increment, decrement, reset>;

// Reducer
model update(model m, action a) {
    return std::visit(lager::visitor{
        [&](increment) { return model{m.value + 1}; },
        [&](decrement) { return model{m.value - 1}; },
        [&](reset r)   { return model{r.new_value}; }
    }, a);
}

// View
void draw(const model& m) {
    std::cout << "Value: " << m.value << "\n";
}

int main() {
    auto store = lager::make_store<action>(
        model{},
        lager::with_manual_event_loop{}
    );
    
    watch(store, draw);
    
    store.dispatch(increment{});  // Value: 1
    store.dispatch(increment{});  // Value: 2
    store.dispatch(decrement{});  // Value: 1
    store.dispatch(reset{100});   // Value: 100
    
    return 0;
}
```

## 9. 最佳实践

```cpp
// ✅ Model 使用不可变容器
struct model {
    immer::vector<item> items;  // 而非 std::vector
    immer::map<id_t, item> by_id;
};

// ✅ Reducer 保持纯粹
model update(model m, action a) {
    // 只做数据转换，不做 IO
}

// ✅ 副作用通过 Effect 返回
std::pair<model, lager::effect<action>> update(...) {
    return {new_model, [](auto& ctx) {
        // 在这里做 IO
    }};
}

// ✅ 使用 LAGER_STRUCT 注册类型
LAGER_STRUCT(my_namespace, my_type, field1, field2);

// ✅ 复杂访问使用 Lens
auto deep_lens = attr(&a::b) | attr(&b::c) | at(0);

// ✅ 状态分片使用 Cursor
auto sub_cursor = root_state[&model::sub_model].make();

// ❌ 避免在 Reducer 中进行 IO
model update(model m, action a) {
    save_to_file(m);  // 错误！应该返回 effect
    return m;
}
```

---

# 快速参考表

## Zug

| 需求 | API |
|------|-----|
| 转换元素 | `map(f)` |
| 过滤元素 | `filter(pred)` |
| 取前 N 个 | `take(n)` |
| 跳过前 N 个 | `drop(n)` |
| 累积计算 | `scan(init, f)` |
| 展平 | `cat` / `mapcat(f)` |
| 去重 | `dedupe` / `distinct` |
| 组合 transducers | `xf1 \| xf2` |
| 立即求值到 vector | `into_vector(xf, data)` |
| 惰性求值 | `sequence(xf, data)` |

## Immer

| 需求 | API |
|------|-----|
| 不可变向量 | `immer::vector<T>` |
| 灵活向量 | `immer::flex_vector<T>` |
| 不可变映射 | `immer::map<K,V>` |
| 不可变集合 | `immer::set<T>` |
| 主键表 | `immer::table<T>` |
| 单值包装 | `immer::box<T>` |
| 批量更新 | `v.transient()` → 修改 → `.persistent()` |

## Lager

| 需求 | API |
|------|-----|
| 创建 Store | `make_store<Action>(init, event_loop)` |
| 分发动作 | `store.dispatch(action)` |
| 监听变化 | `watch(store, callback)` |
| 读取状态 | `store.get()` |
| 局部访问 | `state[&Model::field].make()` |
| 深层访问 | `lager::lenses::attr(...)` |
| 副作用 | 返回 `pair<Model, effect<Action>>` |
| 依赖注入 | `with_deps(service)` |
| 时间旅行 | `with_debugger(http_debug_server{port})` |

---

*文档版本：基于 zug/immer/lager 最新主分支*
