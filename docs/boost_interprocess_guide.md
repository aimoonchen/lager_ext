# Boost.Interprocess 完全使用指南

## 概述

Boost.Interprocess 是一个用于进程间通信(IPC)和共享内存的 C++ 库。核心特性：

- **共享内存管理** - 多进程共享同一块内存区域
- **内存映射文件** - 将文件映射到内存进行高效访问
- **进程间同步** - 互斥锁、条件变量、信号量
- **IPC 容器** - 可在共享内存中使用的 STL 兼容容器
- **消息队列** - 进程间消息传递

---

## 目录

1. [核心概念](#1-核心概念)
2. [共享内存](#2-共享内存)
3. [内存映射文件](#3-内存映射文件)
4. [同步原语](#4-同步原语)
5. [IPC 容器与分配器](#5-ipc-容器与分配器)
6. [消息队列](#6-消息队列)
7. [智能指针](#7-智能指针)
8. [常见陷阱与解决方案](#8-常见陷阱与解决方案)

---

## 1. 核心概念

### 1.1 offset_ptr - 可重定位指针

**为什么需要它？**

普通指针存储绝对地址，但共享内存在不同进程中可能映射到不同的基地址。
`offset_ptr` 存储的是 **相对偏移量**（目标地址 - 自身地址），因此无论映射到哪个地址都能正确工作。

```cpp
#include <boost/interprocess/offset_ptr.hpp>

// 内部实现原理（简化）：
template<class T>
class offset_ptr {
    // 存储偏移而非绝对地址
    std::ptrdiff_t offset_;
    
    T* get() const {
        // offset=1 表示 nullptr（特殊编码）
        if (offset_ == 1) return nullptr;
        // 用自身地址 + 偏移量计算目标地址
        return reinterpret_cast<T*>(
            reinterpret_cast<char*>(this) + offset_
        );
    }
};
```

**关键规则**：
- 共享内存中的所有指针必须使用 `offset_ptr`
- 标准库容器使用裸指针，**不能**直接放入共享内存
- 必须使用 `boost::interprocess` 或 `boost::container` 的容器

### 1.2 资源持久性模型

| 持久性级别 | 生命周期 | 示例 |
|-----------|---------|------|
| **进程级** | 最后一个进程引用结束时销毁 | 匿名共享内存 |
| **内核级** | 系统重启前有效 | POSIX 共享内存、命名信号量 |
| **文件系统级** | 显式删除前有效 | 内存映射文件、Windows 共享内存 |

---

## 2. 共享内存

### 2.1 managed_shared_memory（推荐）

**最佳实践**：

```cpp
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace bip = boost::interprocess;

// RAII 清理器 - 处理崩溃残留
struct shm_remove {
    const char* name;
    shm_remove(const char* n) : name(n) { 
        bip::shared_memory_object::remove(name);  // 清理上次残留
    }
    ~shm_remove() { 
        bip::shared_memory_object::remove(name);  // 正常清理
    }
};

int main() {
    shm_remove remover("MySharedMemory");
    
    // 创建 1MB 共享内存段
    bip::managed_shared_memory segment(
        bip::create_only,
        "MySharedMemory",
        1024 * 1024  // 1MB
    );
    
    // 定义共享内存分配器
    typedef bip::allocator<int, bip::managed_shared_memory::segment_manager>
        ShmAllocator;
    typedef bip::vector<int, ShmAllocator> ShmVector;
    
    // 在共享内存中构造命名对象
    ShmVector* vec = segment.construct<ShmVector>("MyVector")
        (segment.get_segment_manager());  // 传入分配器参数
    
    vec->push_back(1);
    vec->push_back(2);
    vec->push_back(3);
    
    // 查找已存在的对象
    auto found = segment.find<ShmVector>("MyVector");
    if (found.first) {
        std::cout << "Found vector with " << found.second << " elements\n";
    }
    
    // 销毁对象
    segment.destroy<ShmVector>("MyVector");
    
    return 0;
}
```

### 2.2 创建模式

```cpp
// 仅创建（已存在则失败）
bip::managed_shared_memory seg(bip::create_only, "name", size);

// 仅打开（不存在则失败）
bip::managed_shared_memory seg(bip::open_only, "name");

// 打开或创建
bip::managed_shared_memory seg(bip::open_or_create, "name", size);

// 只读打开
bip::managed_shared_memory seg(bip::open_read_only, "name");
```

### 2.3 对象构造方式

```cpp
// 1. 命名对象 - 可通过名字查找
MyClass* p = segment.construct<MyClass>("unique_name")(ctor_args...);

// 2. 匿名对象 - 无名称，无法 find
MyClass* p = segment.construct<MyClass>(bip::anonymous_instance)(args...);

// 3. 查找或构造 - 原子操作
MyClass* p = segment.find_or_construct<MyClass>("name")(args...);

// 4. 数组构造
MyClass* arr = segment.construct<MyClass>("array")[10](args...);
```

### 2.4 内存管理

```cpp
// 查询可用空间
std::size_t free = segment.get_free_memory();

// 尝试增长（仅部分平台支持）
bool grew = bip::managed_shared_memory::grow("name", extra_bytes);

// 收缩到实际使用大小
bool shrunk = bip::managed_shared_memory::shrink_to_fit("name");
```

---

## 3. 内存映射文件

### 3.1 managed_mapped_file

**适用场景**：数据需要持久化到文件，超大数据集

```cpp
#include <boost/interprocess/managed_mapped_file.hpp>

namespace bip = boost::interprocess;

int main() {
    // 自动管理的映射文件
    bip::managed_mapped_file file(
        bip::open_or_create,
        "data.bin",
        1024 * 1024 * 100  // 100MB
    );
    
    // 用法与 managed_shared_memory 完全一致
    typedef bip::allocator<int, bip::managed_mapped_file::segment_manager>
        FileAllocator;
    typedef bip::vector<int, FileAllocator> FileVector;
    
    FileVector* vec = file.find_or_construct<FileVector>("data")
        (file.get_segment_manager());
    
    vec->push_back(42);
    
    // 显式刷新到磁盘（可选）
    file.flush();
    
    return 0;
}
```

### 3.2 底层 file_mapping + mapped_region

**更细粒度控制**：

```cpp
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <fstream>

namespace bip = boost::interprocess;

int main() {
    const char* filename = "myfile.dat";
    const std::size_t file_size = 1024 * 1024;
    
    // 1. 创建/扩展文件
    {
        std::filebuf fbuf;
        fbuf.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
        fbuf.pubseekoff(file_size - 1, std::ios::beg);
        fbuf.sputc(0);
    }
    
    // 2. 创建文件映射对象
    bip::file_mapping mapping(filename, bip::read_write);
    
    // 3. 映射到内存区域
    bip::mapped_region region(mapping, bip::read_write);
    
    // 4. 直接访问内存
    void* addr = region.get_address();
    std::size_t size = region.get_size();
    
    // 写入数据
    std::memset(addr, 0, size);
    
    // 刷新到文件
    region.flush();
    
    return 0;
}
```

---

## 4. 同步原语

### 4.1 命名互斥锁

**进程间互斥**：

```cpp
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace bip = boost::interprocess;

int main() {
    // 创建或打开命名互斥锁
    bip::named_mutex mutex(bip::open_or_create, "my_mutex");
    
    {
        // RAII 锁定
        bip::scoped_lock<bip::named_mutex> lock(mutex);
        
        // 临界区操作...
        
    }  // 自动解锁
    
    // 清理（通常在程序结束时）
    bip::named_mutex::remove("my_mutex");
    
    return 0;
}
```

### 4.2 共享内存内嵌互斥锁

**更高性能**（避免内核命名查找）：

```cpp
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace bip = boost::interprocess;

struct SharedData {
    bip::interprocess_mutex mutex;
    int counter;
};

int main() {
    bip::managed_shared_memory segment(
        bip::open_or_create, "MyShm", 65536
    );
    
    SharedData* data = segment.find_or_construct<SharedData>("Data")();
    
    {
        bip::scoped_lock<bip::interprocess_mutex> lock(data->mutex);
        data->counter++;
    }
    
    return 0;
}
```

### 4.3 条件变量

```cpp
#include <boost/interprocess/sync/named_condition.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

namespace bip = boost::interprocess;

// 生产者
void producer() {
    bip::named_mutex mutex(bip::open_or_create, "mtx");
    bip::named_condition cond(bip::open_or_create, "cond");
    
    {
        bip::scoped_lock<bip::named_mutex> lock(mutex);
        // 修改共享数据...
        cond.notify_one();  // 通知消费者
    }
}

// 消费者
void consumer() {
    bip::named_mutex mutex(bip::open_or_create, "mtx");
    bip::named_condition cond(bip::open_or_create, "cond");
    
    bip::scoped_lock<bip::named_mutex> lock(mutex);
    cond.wait(lock);  // 等待通知
    // 处理数据...
}
```

### 4.4 信号量

```cpp
#include <boost/interprocess/sync/named_semaphore.hpp>

namespace bip = boost::interprocess;

int main() {
    // 创建初始值为 3 的信号量
    bip::named_semaphore sem(bip::open_or_create, "my_sem", 3);
    
    sem.wait();      // P 操作，-1
    // 临界区...
    sem.post();      // V 操作，+1
    
    // 非阻塞尝试
    if (sem.try_wait()) {
        // 获得资源
    }
    
    // 带超时
    boost::posix_time::ptime timeout = 
        boost::posix_time::microsec_clock::universal_time() +
        boost::posix_time::milliseconds(100);
    if (sem.timed_wait(timeout)) {
        // 在超时前获得资源
    }
    
    return 0;
}
```

### 4.5 读写锁

```cpp
#include <boost/interprocess/sync/named_sharable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace bip = boost::interprocess;

void reader() {
    bip::named_sharable_mutex mutex(bip::open_or_create, "rw_mutex");
    
    // 共享锁（读锁）- 多个读者可同时持有
    bip::sharable_lock<bip::named_sharable_mutex> lock(mutex);
    // 读取数据...
}

void writer() {
    bip::named_sharable_mutex mutex(bip::open_or_create, "rw_mutex");
    
    // 独占锁（写锁）
    bip::scoped_lock<bip::named_sharable_mutex> lock(mutex);
    // 写入数据...
}
```

---

## 5. IPC 容器与分配器

### 5.1 可用容器

```cpp
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/containers/list.hpp>
#include <boost/interprocess/containers/slist.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/flat_set.hpp>
#include <boost/interprocess/containers/flat_map.hpp>
#include <boost/interprocess/containers/string.hpp>
```

### 5.2 基本分配器

```cpp
namespace bip = boost::interprocess;

typedef bip::managed_shared_memory::segment_manager SegmentManager;

// 基本分配器 - 直接从段管理器分配
typedef bip::allocator<int, SegmentManager> BasicAlloc;

// 使用
bip::vector<int, BasicAlloc> vec(segment.get_segment_manager());
```

### 5.3 节点分配器（优化频繁小分配）

```cpp
#include <boost/interprocess/allocators/node_allocator.hpp>

// node_allocator - 预分配节点池，减少碎片
// 适用于: list, set, map 等节点容器
typedef bip::node_allocator<int, SegmentManager> NodeAlloc;

bip::list<int, NodeAlloc> mylist(segment.get_segment_manager());
```

### 5.4 自适应池分配器

```cpp
#include <boost/interprocess/allocators/adaptive_pool.hpp>

// adaptive_pool - 自适应调整池大小
// 在分配量变化大时表现更好
typedef bip::adaptive_pool<int, SegmentManager> AdaptiveAlloc;
```

### 5.5 共享内存字符串

```cpp
#include <boost/interprocess/containers/string.hpp>

typedef bip::allocator<char, SegmentManager> CharAlloc;
typedef bip::basic_string<char, std::char_traits<char>, CharAlloc> ShmString;

// 构造
ShmString* str = segment.construct<ShmString>("greeting")
    ("Hello, IPC!", segment.get_segment_manager());
```

### 5.6 嵌套容器

```cpp
// Map<string, vector<int>> 在共享内存中

typedef bip::allocator<char, SegmentManager> CharAlloc;
typedef bip::basic_string<char, std::char_traits<char>, CharAlloc> ShmString;

typedef bip::allocator<int, SegmentManager> IntAlloc;
typedef bip::vector<int, IntAlloc> ShmIntVector;

typedef std::pair<const ShmString, ShmIntVector> MapValue;
typedef bip::allocator<MapValue, SegmentManager> MapAlloc;
typedef bip::map<ShmString, ShmIntVector, std::less<ShmString>, MapAlloc> ShmMap;

// 构造时需要传递分配器
ShmMap* mymap = segment.construct<ShmMap>("mymap")
    (std::less<ShmString>(), segment.get_segment_manager());

// 插入元素
ShmString key("key1", segment.get_segment_manager());
ShmIntVector value(segment.get_segment_manager());
value.push_back(1);
value.push_back(2);

mymap->insert(std::make_pair(key, value));
```

---

## 6. 消息队列

### 6.1 基本使用

```cpp
#include <boost/interprocess/ipc/message_queue.hpp>

namespace bip = boost::interprocess;

// 发送端
void sender() {
    bip::message_queue::remove("my_queue");
    
    // 创建队列：最多 100 条消息，每条最大 256 字节
    bip::message_queue mq(
        bip::create_only,
        "my_queue",
        100,     // max_num_msg
        256      // max_msg_size
    );
    
    const char* msg = "Hello!";
    mq.send(msg, strlen(msg) + 1, 0);  // priority = 0
}

// 接收端
void receiver() {
    bip::message_queue mq(bip::open_only, "my_queue");
    
    char buffer[256];
    std::size_t recvd_size;
    unsigned int priority;
    
    mq.receive(buffer, sizeof(buffer), recvd_size, priority);
    std::cout << "Received: " << buffer << std::endl;
}
```

### 6.2 非阻塞和超时

```cpp
// 非阻塞发送
if (mq.try_send(msg, size, priority)) {
    // 发送成功
}

// 非阻塞接收
if (mq.try_receive(buffer, sizeof(buffer), recvd_size, priority)) {
    // 接收成功
}

// 带超时
boost::posix_time::ptime timeout = 
    boost::posix_time::microsec_clock::universal_time() +
    boost::posix_time::milliseconds(1000);

if (mq.timed_send(msg, size, priority, timeout)) {
    // 在超时前发送成功
}

if (mq.timed_receive(buffer, sizeof(buffer), recvd_size, priority, timeout)) {
    // 在超时前接收成功
}
```

### 6.3 优先级队列

```cpp
// 高优先级消息先被接收
mq.send("urgent", 7, 10);   // priority = 10
mq.send("normal", 7, 1);    // priority = 1

// 接收时 "urgent" 会先出队
```

---

## 7. 智能指针

### 7.1 shared_ptr（共享内存版）

```cpp
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/smart_ptr/weak_ptr.hpp>

namespace bip = boost::interprocess;

typedef bip::managed_shared_memory::segment_manager SegmentManager;
typedef bip::allocator<void, SegmentManager> VoidAllocator;
typedef bip::deleter<MyClass, SegmentManager> MyDeleter;
typedef bip::shared_ptr<MyClass, VoidAllocator, MyDeleter> MySharedPtr;

// 创建共享指针
MyClass* raw = segment.construct<MyClass>(bip::anonymous_instance)();
MySharedPtr ptr(raw, 
                segment.get_allocator<void>(),
                MyDeleter(segment.get_segment_manager()));
```

### 7.2 make_managed_shared_ptr（推荐）

```cpp
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>

auto ptr = bip::make_managed_shared_ptr(
    segment.construct<MyClass>(bip::anonymous_instance)(args...),
    segment
);
```

---

## 8. 常见陷阱与解决方案

### 8.1 ❌ 使用标准库容器

```cpp
// 错误！std::vector 使用裸指针
std::vector<int>* vec = segment.construct<std::vector<int>>("bad")();
```

**✅ 解决方案**：使用 `boost::interprocess::vector`

### 8.2 ❌ 存储裸指针

```cpp
struct Bad {
    int* ptr;  // 绝对地址，跨进程失效！
};
```

**✅ 解决方案**：

```cpp
struct Good {
    bip::offset_ptr<int> ptr;
};
```

### 8.3 ❌ 存储引用

```cpp
struct Bad {
    std::string& ref;  // 引用本质是指针
};
```

**✅ 解决方案**：使用 `offset_ptr` 或重新设计

### 8.4 ❌ 虚函数

```cpp
struct Bad {
    virtual void foo();  // vtable 指针会失效！
};
```

**✅ 解决方案**：避免虚函数，使用函数指针或类型标签

### 8.5 ❌ 忘记清理残留资源

```cpp
// 程序崩溃后，共享内存可能残留
```

**✅ 解决方案**：使用 RAII 清理模式

```cpp
struct shm_remove {
    const char* name;
    shm_remove(const char* n) : name(n) { 
        bip::shared_memory_object::remove(name);  // 清理残留
    }
    ~shm_remove() { 
        bip::shared_memory_object::remove(name);
    }
};
```

### 8.6 ❌ 内存不足

```cpp
// 段太小会抛出 bad_alloc
```

**✅ 解决方案**：

1. 预估足够大小
2. 使用 `grow()` 动态扩展（如平台支持）
3. 捕获异常并处理

```cpp
try {
    vec->push_back(x);
} catch (bip::bad_alloc&) {
    // 处理内存不足
}
```

### 8.7 跨平台注意事项

| 特性 | Windows | POSIX |
|-----|---------|-------|
| 共享内存持久性 | 文件系统级 | 内核级 |
| 最大段大小 | 受限于磁盘 | 受限于 /dev/shm |
| 命名规则 | 较宽松 | 必须以 "/" 开头（某些系统） |

---

## 快速参考表

| 需求 | 推荐类 |
|-----|-------|
| 共享复杂数据结构 | `managed_shared_memory` |
| 持久化数据 | `managed_mapped_file` |
| 进程间互斥 | `named_mutex` 或内嵌 `interprocess_mutex` |
| 生产者-消费者 | `message_queue` 或 `named_condition` |
| 读多写少 | `named_sharable_mutex` |
| 频繁小分配 | `node_allocator` |
| 字符串 | `bip::basic_string` |
| 动态数组 | `bip::vector` |
| 关联容器 | `bip::map` / `bip::flat_map` |

---

## 完整示例：多进程计数器

```cpp
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <iostream>

namespace bip = boost::interprocess;

struct SharedCounter {
    bip::interprocess_mutex mutex;
    int value;
};

int main(int argc, char* argv[]) {
    if (argc == 1) {  // 主进程
        struct shm_remove {
            shm_remove() { bip::shared_memory_object::remove("CounterShm"); }
            ~shm_remove() { bip::shared_memory_object::remove("CounterShm"); }
        } remover;
        
        bip::managed_shared_memory segment(
            bip::create_only, "CounterShm", 65536
        );
        
        SharedCounter* counter = segment.construct<SharedCounter>("Counter")();
        counter->value = 0;
        
        std::cout << "Counter initialized. Run with any argument to increment.\n";
        std::cout << "Press Enter to exit and cleanup...\n";
        std::cin.get();
        
        std::cout << "Final value: " << counter->value << std::endl;
    } 
    else {  // 子进程
        bip::managed_shared_memory segment(bip::open_only, "CounterShm");
        
        auto found = segment.find<SharedCounter>("Counter");
        if (found.first) {
            SharedCounter* counter = found.first;
            
            bip::scoped_lock<bip::interprocess_mutex> lock(counter->mutex);
            counter->value++;
            std::cout << "Incremented to: " << counter->value << std::endl;
        }
    }
    
    return 0;
}
```

---

*文档版本：基于 Boost 1.90.0*
