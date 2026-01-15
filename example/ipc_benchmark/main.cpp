// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file main.cpp
/// @brief Cross-process benchmark: IPC Channel vs Windows SendMessage/PostMessage
///
/// Usage:
///   ipc_benchmark                  # Run cross-process comparison
///   ipc_benchmark -n 100000        # Custom iterations
///   ipc_benchmark --server         # Run as server (internal)

#include <lager_ext/ipc.h>
#include <lager_ext/value.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace lager_ext;
using namespace lager_ext::ipc;

//=============================================================================
// Configuration
//=============================================================================

constexpr int WARMUP_ITERATIONS = 200;
constexpr int DEFAULT_ITERATIONS = 10000; // Reasonable default for accurate measurement
constexpr int SMALL_DATA_SIZE = 64;
constexpr int MEDIUM_DATA_SIZE = 200;

#ifdef _WIN32
constexpr UINT WM_BENCHMARK_START = WM_USER + 100;
constexpr UINT WM_BENCHMARK_PING = WM_USER + 101;
constexpr UINT WM_BENCHMARK_PONG = WM_USER + 102;
constexpr UINT WM_BENCHMARK_DONE = WM_USER + 103;
constexpr UINT WM_BENCHMARK_SYNC_PING = WM_USER + 104; // For SendMessage test
#endif

const std::string CHANNEL_NAME = "IpcBenchmarkChannel";

//=============================================================================
// Global state for Windows benchmark
//=============================================================================
#ifdef _WIN32
std::atomic<int> g_pongsReceived{0};
std::atomic<bool> g_serverRunning{true};
HWND g_partnerHwnd = nullptr;
#endif

//=============================================================================
// Utility Functions
//=============================================================================

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    void reset() { start_ = std::chrono::high_resolution_clock::now(); }

    double elapsedNs() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::nano>(end - start_).count();
    }

    double elapsedUs() const { return elapsedNs() / 1000.0; }
    double elapsedMs() const { return elapsedNs() / 1000000.0; }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void printResult(const std::string& test, double avgNs, double minNs, double maxNs) {
    std::cout << std::left << std::setw(35) << test << "avg: " << std::setw(10) << std::fixed << std::setprecision(1)
              << avgNs << " ns"
              << "  min: " << std::setw(10) << minNs << " ns"
              << "  max: " << std::setw(10) << maxNs << " ns\n";
}

// Calculate percentile (sorted vector required)
double percentile(std::vector<double>& sorted, double p) {
    if (sorted.empty())
        return 0;
    size_t idx = static_cast<size_t>(p / 100.0 * sorted.size());
    if (idx >= sorted.size())
        idx = sorted.size() - 1;
    return sorted[idx];
}

struct Stats {
    double avg;
    double min;
    double max;
    double median;
    double p50;
    double p95;
    double p99;
    double throughput;
};

Stats computeStats(std::vector<double>& times, int iterations) {
    Stats s{};
    if (times.empty())
        return s;

    s.avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    s.min = *std::min_element(times.begin(), times.end());
    s.max = *std::max_element(times.begin(), times.end());

    // Sort for percentiles
    std::sort(times.begin(), times.end());
    s.median = percentile(times, 50);
    s.p50 = s.median;
    s.p95 = percentile(times, 95);
    s.p99 = percentile(times, 99);

    double totalNs = std::accumulate(times.begin(), times.end(), 0.0);
    s.throughput = iterations / (totalNs / 1e9);

    return s;
}

void printDetailedStats(const std::string& test, const Stats& s) {
    std::cout << std::left << std::setw(35) << test << "\n";
    std::cout << "    avg: " << std::setw(12) << std::fixed << std::setprecision(1) << s.avg << " ns"
              << "  median: " << std::setw(12) << s.median << " ns\n";
    std::cout << "    min: " << std::setw(12) << s.min << " ns"
              << "  max: " << std::setw(15) << s.max << " ns\n";
    std::cout << "    p95: " << std::setw(12) << s.p95 << " ns"
              << "  p99: " << std::setw(12) << s.p99 << " ns\n";
    std::cout << "    throughput: " << std::setprecision(0) << s.throughput << " ops/sec\n";
}

//=============================================================================
// Windows Message Benchmark (Cross-Process)
//=============================================================================

#ifdef _WIN32

LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_BENCHMARK_PING:
        // Reply with PONG (for PostMessage test)
        if (g_partnerHwnd) {
            PostMessage(g_partnerHwnd, WM_BENCHMARK_PONG, wParam, 0);
        }
        return 0;

    case WM_BENCHMARK_SYNC_PING:
        // Just return immediately - for SendMessage latency test
        return 42;

    case WM_COPYDATA: {
        // Echo back via COPYDATA
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (g_partnerHwnd && cds) {
            COPYDATASTRUCT reply;
            reply.dwData = cds->dwData + 1;
            reply.cbData = cds->cbData;
            reply.lpData = cds->lpData;
            SendMessage(g_partnerHwnd, WM_COPYDATA, (WPARAM)hwnd, (LPARAM)&reply);
        }
        return TRUE;
    }

    case WM_BENCHMARK_START:
        g_partnerHwnd = (HWND)wParam;
        return 0;

    case WM_BENCHMARK_DONE:
        g_serverRunning = false;
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_BENCHMARK_PONG:
        g_pongsReceived++;
        return 0;

    case WM_COPYDATA:
        g_pongsReceived++;
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int runServer() {
    std::cerr << "[Server] Starting...\n";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = ServerWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "IpcBenchServer";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "IpcBenchServer", "Server", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandle(nullptr), nullptr);
    if (!hwnd) {
        std::cerr << "[Server] Failed to create window\n";
        return 1;
    }

    // Server creates its outbound channel first (producer for replies)
    auto channelOut = Channel::create(CHANNEL_NAME + "_toclient", 8192);
    std::cerr << "[Server] Created reply channel\n";

    // Output HWND for client to parse
    std::cout << "HWND=" << (uintptr_t)hwnd << "\n";
    std::cout.flush();

    // IPC channel for receiving messages from client
    // Will connect lazily - client creates this channel later after Windows tests
    std::unique_ptr<Channel> channelIn;
    auto lastConnectAttempt = std::chrono::steady_clock::now();

    MSG msg;
    int idleCount = 0;
    while (g_serverRunning) {
        bool didWork = false;

        // Windows messages - process ALL pending messages first
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_serverRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            didWork = true;
        }

        if (!g_serverRunning)
            break;

        // Lazy connect to client's IPC channel (client creates it after Windows tests)
        // Keep trying periodically until connected
        if (!channelIn) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastConnectAttempt).count();
            if (elapsed >= 100) { // Try every 100ms
                channelIn = Channel::open(CHANNEL_NAME + "_toserver");
                if (channelIn) {
                    std::cerr << "[Server] Connected to client IPC channel\n";
                }
                lastConnectAttempt = now;
            }
        }

        // IPC messages - echo back (batch process)
        if (channelIn && channelOut) {
            for (int batch = 0; batch < 100; ++batch) {
                uint32_t msgId;
                uint8_t buf[256];
                int len = channelIn->tryReceiveRaw(msgId, buf, sizeof(buf));
                if (len > 0) {
                    channelOut->postRaw(msgId + 1, buf, len);
                    didWork = true;
                } else {
                    break;
                }
            }
        }

        // Adaptive sleep: spin when busy, sleep when idle
        // But don't exit - wait for explicit WM_BENCHMARK_DONE
        if (didWork) {
            idleCount = 0;
        } else {
            idleCount++;
            if (idleCount > 10000) {
                // Long idle - sleep longer but stay alive
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                idleCount = 5000; // Prevent overflow, keep sleeping
            } else if (idleCount > 100) {
                std::this_thread::yield();
            }
            // else: spin
        }
    }

    std::cerr << "[Server] Exiting\n";
    return 0;
}

void benchmarkWindowsPostMessage(HWND serverHwnd, HWND clientHwnd, int iterations) {
    printHeader("Windows PostMessage Benchmark (Cross-Process)");

    // Notify server of our HWND
    PostMessage(serverHwnd, WM_BENCHMARK_START, (WPARAM)clientHwnd, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<double> times;
    times.reserve(iterations);

    // Warmup
    g_pongsReceived = 0;
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        PostMessage(serverHwnd, WM_BENCHMARK_PING, i, 0);
    }
    while (g_pongsReceived < WARMUP_ITERATIONS) {
        MSG msg;
        while (PeekMessage(&msg, clientHwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        std::this_thread::yield();
    }

    // Benchmark
    g_pongsReceived = 0;
    std::cout << "PostMessage PING -> PONG round-trip:\n";

    for (int i = 0; i < iterations; ++i) {
        Timer t;
        PostMessage(serverHwnd, WM_BENCHMARK_PING, i, 0);

        int target = g_pongsReceived.load() + 1;
        while (g_pongsReceived.load() < target) {
            MSG msg;
            while (PeekMessage(&msg, clientHwnd, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        times.push_back(t.elapsedNs());
    }

    double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double minVal = *std::min_element(times.begin(), times.end());
    double maxVal = *std::max_element(times.begin(), times.end());
    printResult("  PostMessage round-trip", avg, minVal, maxVal);

    double throughput = iterations / (std::accumulate(times.begin(), times.end(), 0.0) / 1e9);
    std::cout << "\n  Throughput: " << std::fixed << std::setprecision(0) << throughput << " round-trips/second\n";
}

void benchmarkSendMessage(HWND serverHwnd, int iterations) {
    printHeader("Windows SendMessage Benchmark (Cross-Process)");

    std::vector<double> times;
    times.reserve(iterations);

    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        SendMessage(serverHwnd, WM_BENCHMARK_SYNC_PING, i, 0);
    }

    std::cout << "SendMessage (no data, sync):\n";
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        LRESULT result = SendMessage(serverHwnd, WM_BENCHMARK_SYNC_PING, i, 0);
        (void)result; // Expected: 42
        times.push_back(t.elapsedNs());
    }

    double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double minVal = *std::min_element(times.begin(), times.end());
    double maxVal = *std::max_element(times.begin(), times.end());
    printResult("  SendMessage round-trip", avg, minVal, maxVal);

    double throughput = iterations / (std::accumulate(times.begin(), times.end(), 0.0) / 1e9);
    std::cout << "\n  Throughput: " << std::fixed << std::setprecision(0) << throughput << " calls/second\n";
}

void benchmarkWmCopyData(HWND serverHwnd, HWND clientHwnd, int iterations) {
    printHeader("Windows SendMessage + WM_COPYDATA (Cross-Process)");

    std::vector<uint8_t> smallData(SMALL_DATA_SIZE, 0xCD);
    std::vector<uint8_t> mediumData(MEDIUM_DATA_SIZE, 0xEF);
    std::vector<double> times;
    times.reserve(iterations);

    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = static_cast<DWORD>(smallData.size());
    cds.lpData = smallData.data();

    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        SendMessage(serverHwnd, WM_COPYDATA, (WPARAM)clientHwnd, (LPARAM)&cds);
    }

    // Small data
    std::cout << "Small data (" << SMALL_DATA_SIZE << " bytes):\n";
    times.clear();
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        SendMessage(serverHwnd, WM_COPYDATA, (WPARAM)clientHwnd, (LPARAM)&cds);
        times.push_back(t.elapsedNs());
    }

    double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double minVal = *std::min_element(times.begin(), times.end());
    double maxVal = *std::max_element(times.begin(), times.end());
    printResult("  WM_COPYDATA round-trip", avg, minVal, maxVal);

    // Medium data
    std::cout << "\nMedium data (" << MEDIUM_DATA_SIZE << " bytes):\n";
    cds.cbData = static_cast<DWORD>(mediumData.size());
    cds.lpData = mediumData.data();

    times.clear();
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        SendMessage(serverHwnd, WM_COPYDATA, (WPARAM)clientHwnd, (LPARAM)&cds);
        times.push_back(t.elapsedNs());
    }

    avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    minVal = *std::min_element(times.begin(), times.end());
    maxVal = *std::max_element(times.begin(), times.end());
    printResult("  WM_COPYDATA round-trip", avg, minVal, maxVal);

    double throughput = iterations / (std::accumulate(times.begin(), times.end(), 0.0) / 1e9);
    std::cout << "\n  Throughput: " << std::fixed << std::setprecision(0) << throughput << " messages/second\n";
}

void benchmarkIpcCrossProcess(int iterations) {
    printHeader("IPC Channel Cross-Process Benchmark");

    // Client side: create our outbound channel first
    auto toServer = Channel::create(CHANNEL_NAME + "_toserver", 8192);
    if (!toServer) {
        std::cerr << "Failed to create IPC producer channel\n";
        return;
    }

    // Wait for server to create its reply channel
    std::unique_ptr<Channel> fromServer;
    for (int retry = 0; retry < 100 && !fromServer; ++retry) {
        fromServer = Channel::open(CHANNEL_NAME + "_toclient");
        if (!fromServer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (!fromServer) {
        std::cerr << "Failed to connect to server reply channel\n";
        return;
    }

    std::cout << "IPC channels connected.\n\n";
    std::cout.flush();

    std::vector<uint8_t> smallData(SMALL_DATA_SIZE, 0xAB);
    std::vector<uint8_t> mediumData(MEDIUM_DATA_SIZE, 0xCD);
    std::vector<double> times;
    times.reserve(iterations);

    // Quick warmup - verify channels work
    std::cout << "Testing channel connectivity...\n";
    std::cout.flush();

    bool sendOk = toServer->postRaw(99, smallData.data(), smallData.size());
    std::cout << "  Send to server: " << (sendOk ? "OK" : "FAILED") << "\n";
    std::cout.flush();

    // Wait a bit for server to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint32_t testId;
    uint8_t testBuf[256];
    int testLen = fromServer->tryReceiveRaw(testId, testBuf, sizeof(testBuf));
    std::cout << "  Receive from server: " << (testLen > 0 ? "OK" : "TIMEOUT") << " (len=" << testLen << ")\n";
    std::cout.flush();

    if (testLen <= 0) {
        std::cerr << "ERROR: Channel communication failed, aborting benchmark\n";
        return;
    }

    // Warmup
    std::cout << "\nWarming up (" << WARMUP_ITERATIONS << " iterations)...\n";
    std::cout.flush();
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        toServer->postRaw(1, smallData.data(), smallData.size());
        uint32_t id;
        uint8_t buf[256];
        int spins = 0;
        while (fromServer->tryReceiveRaw(id, buf, sizeof(buf)) <= 0 && spins < 100000) {
            spins++;
        }
    }
    std::cout << "Warmup complete.\n\n";
    std::cout.flush();

    // Small data
    std::cout << "Running small data test (" << SMALL_DATA_SIZE << " bytes)...\n" << std::flush;
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        toServer->postRaw(1, smallData.data(), smallData.size());

        uint32_t id;
        uint8_t buf[256];
        while (fromServer->tryReceiveRaw(id, buf, sizeof(buf)) <= 0) {
            // Spin wait
        }
        times.push_back(t.elapsedNs());
    }

    std::cout << "\nSmall data (" << SMALL_DATA_SIZE << " bytes):\n";
    double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double minVal = *std::min_element(times.begin(), times.end());
    double maxVal = *std::max_element(times.begin(), times.end());
    printResult("  IPC round-trip", avg, minVal, maxVal);
    std::cout.flush();

    // Medium data
    std::cout << "Running medium data test (" << MEDIUM_DATA_SIZE << " bytes)...\n" << std::flush;
    times.clear();
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        toServer->postRaw(2, mediumData.data(), mediumData.size());

        uint32_t id;
        uint8_t buf[256];
        while (fromServer->tryReceiveRaw(id, buf, sizeof(buf)) <= 0) {
            // Spin wait
        }
        times.push_back(t.elapsedNs());
    }

    std::cout << "Medium data (" << MEDIUM_DATA_SIZE << " bytes):\n";
    avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    minVal = *std::min_element(times.begin(), times.end());
    maxVal = *std::max_element(times.begin(), times.end());
    printResult("  IPC round-trip", avg, minVal, maxVal);

    double throughput = iterations / (std::accumulate(times.begin(), times.end(), 0.0) / 1e9);
    std::cout << "\nThroughput: " << std::fixed << std::setprecision(0) << throughput << " round-trips/second\n";
}

int runCrossProcessBenchmark(int iterations) {
    printHeader("Starting Cross-Process Benchmark");
    std::cout << "Spawning server process...\n\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // Server stderr goes to console
    si.hStdInput = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi = {};
    std::string cmdLine = std::string(exePath) + " --server";

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        std::cerr << "Failed to start server: " << GetLastError() << "\n";
        return 1;
    }
    CloseHandle(hWritePipe);

    // Read HWND from server
    char buffer[1024];
    DWORD bytesRead;
    std::string output;
    for (int i = 0; i < 50; ++i) {
        if (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buffer[bytesRead] = 0;
            output += buffer;
            if (output.find("HWND=") != std::string::npos)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto pos = output.find("HWND=");
    if (pos == std::string::npos) {
        std::cerr << "Failed to get server HWND\nOutput: " << output << "\n";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        return 1;
    }

    uintptr_t hwndVal = 0;
    std::istringstream(output.substr(pos + 5)) >> hwndVal;
    HWND serverHwnd = (HWND)hwndVal;
    std::cout << "Server HWND: " << hwndVal << "\n\n";

    // Create client window
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ClientWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "IpcBenchClient";
    RegisterClassA(&wc);

    HWND clientHwnd = CreateWindowExA(0, "IpcBenchClient", "Client", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                      GetModuleHandle(nullptr), nullptr);
    if (!clientHwnd) {
        std::cerr << "Failed to create client window\n";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        return 1;
    }

    // Run benchmarks - Windows tests first while server is responsive
    // Then IPC test (uses dedicated channels)
    benchmarkSendMessage(serverHwnd, iterations);
    benchmarkWindowsPostMessage(serverHwnd, clientHwnd, iterations);
    benchmarkWmCopyData(serverHwnd, clientHwnd, iterations);
    benchmarkIpcCrossProcess(iterations);

    // Shutdown
    PostMessage(serverHwnd, WM_BENCHMARK_DONE, 0, 0);
    WaitForSingleObject(pi.hProcess, 5000);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);
    DestroyWindow(clientHwnd);

    return 0;
}

#endif // _WIN32

//=============================================================================
// Summary
//=============================================================================

void printSummary() {
    printHeader("Cross-Process IPC Performance Comparison");

    std::cout << "Method                             | Typical Latency | Notes\n";
    std::cout << std::string(70, '-') << "\n";
    std::cout << "IPC Channel (Shared Memory)        | ~0.1-1 us       | Lock-free, user-mode\n";
    std::cout << "SendMessage (no data)              | ~1-5 us         | Kernel transition\n";
    std::cout << "PostMessage + reply                | ~10-50 us       | Async + reply roundtrip\n";
    std::cout << "SendMessage + WM_COPYDATA          | ~5-20 us        | Kernel copy + sync\n";
    std::cout << "\n";

    std::cout << "IPC Channel Advantages:\n";
    std::cout << "  - Lock-free ring buffer using std::atomic\n";
    std::cout << "  - Cache-line aligned producer/consumer indices\n";
    std::cout << "  - Inline data storage (up to 240 bytes)\n";
    std::cout << "  - Zero system calls in hot path\n";
    std::cout << "  - No kernel transition overhead\n";
    std::cout << "\n";

    std::cout << "Windows Messaging Advantages:\n";
    std::cout << "  - Native Windows API, well-supported\n";
    std::cout << "  - Works with any process (no shared memory setup)\n";
    std::cout << "  - Built-in message queue management\n";
    std::cout << "\n";

    std::cout << "Recommended Use Cases for IPC Channel:\n";
    std::cout << "  - Game engine <-> Editor communication\n";
    std::cout << "  - Main process <-> Worker process\n";
    std::cout << "  - Real-time data streaming between 2 apps\n";
    std::cout << "  - High-frequency message exchange (>10K msg/s)\n";
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char* argv[]) {
    int iterations = DEFAULT_ITERATIONS;
    bool serverMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            serverMode = true;
        } else if (arg == "--iterations" || arg == "-n") {
            if (i + 1 < argc) {
                iterations = std::atoi(argv[++i]);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "IPC Cross-Process Benchmark: Channel vs Windows Messages\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --iterations N, -n N Number of iterations (default: " << DEFAULT_ITERATIONS << ")\n";
            std::cout << "  --server             Run as server (internal use)\n";
            std::cout << "  --help, -h           Show this help\n";
            return 0;
        } else {
            // Legacy: first arg is iterations
            iterations = std::atoi(arg.c_str());
        }
    }

#ifdef _WIN32
    // Server mode - run by child process
    if (serverMode) {
        return runServer();
    }

    std::cout << "IPC Cross-Process Benchmark: Channel vs Windows Messages\n";
    std::cout << "=========================================================\n\n";
    std::cout << "Iterations: " << iterations << "\n\n";

    try {
        int result = runCrossProcessBenchmark(iterations);
        printSummary();

        std::cout << "\nBenchmark complete.\n";
        return result;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "This benchmark requires Windows.\n";
    return 1;
#endif
}