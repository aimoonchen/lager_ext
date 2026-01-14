// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file shared_buffer_demo.cpp
/// @brief Demo/Test for SharedBufferSPSC high-performance shared memory buffer

#include <lager_ext/shared_buffer_spsc.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace lager_ext::ipc;

//=============================================================================
// Test Data Structures
//=============================================================================

/// Simple camera state for testing
struct CameraState {
    float position[3];    // x, y, z  = 12 bytes
    float rotation[4];    // quaternion: x, y, z, w = 16 bytes
    float fov;            // 4 bytes
    uint32_t frame_id;    // 4 bytes
    // Total so far: 36 bytes, pad to 64
    char padding[28];
};

static_assert(std::is_trivially_copyable_v<CameraState>, "CameraState must be trivially copyable");
static_assert(sizeof(CameraState) == 64, "CameraState should be 64 bytes");

/// Larger data structure for bandwidth testing
struct LargeData {
    uint64_t sequence;
    uint64_t timestamp;
    float matrix[16];        // 4x4 matrix
    uint8_t blob[1024 - 80]; // Fill to 1KB
};

static_assert(sizeof(LargeData) == 1024, "LargeData should be 1024 bytes");

//=============================================================================
// Test Functions
//=============================================================================

void test_basic_operations() {
    printf("\n=== Test 1: Basic Operations ===\n");
    
    // Create producer
    auto producer = SharedBufferSPSC<CameraState>::create("TestCamera");
    if (!producer) {
        printf("ERROR: Failed to create producer: %s\n", 
               SharedBufferSPSC<CameraState>::last_error().c_str());
        return;
    }
    printf("Producer created: %s\n", producer->name().c_str());
    
    // Open consumer
    auto consumer = SharedBufferSPSC<CameraState>::open("TestCamera");
    if (!consumer) {
        printf("ERROR: Failed to open consumer: %s\n",
               SharedBufferSPSC<CameraState>::last_error().c_str());
        return;
    }
    printf("Consumer opened: %s\n", consumer->name().c_str());
    
    // Write using write()
    CameraState state1{};
    state1.position[0] = 1.0f;
    state1.position[1] = 2.0f;
    state1.position[2] = 3.0f;
    state1.fov = 60.0f;
    state1.frame_id = 1;
    
    producer->write(state1);
    printf("Written: pos=(%.1f, %.1f, %.1f), fov=%.1f, frame=%u\n",
           state1.position[0], state1.position[1], state1.position[2],
           state1.fov, state1.frame_id);
    
    // Read using read()
    const auto& read1 = consumer->read();
    printf("Read: pos=(%.1f, %.1f, %.1f), fov=%.1f, frame=%u\n",
           read1.position[0], read1.position[1], read1.position[2],
           read1.fov, read1.frame_id);
    
    // Verify
    if (read1.position[0] == 1.0f && read1.fov == 60.0f && read1.frame_id == 1) {
        printf("✓ Basic read/write PASSED\n");
    } else {
        printf("✗ Basic read/write FAILED\n");
    }
    
    // Write using WriteGuard (zero-copy)
    {
        auto guard = producer->write_guard();
        guard->position[0] = 10.0f;
        guard->position[1] = 20.0f;
        guard->position[2] = 30.0f;
        guard->fov = 90.0f;
        guard->frame_id = 2;
    }  // Auto-commit here
    
    // Read again
    const auto& read2 = consumer->read();
    printf("After WriteGuard: pos=(%.1f, %.1f, %.1f), fov=%.1f, frame=%u\n",
           read2.position[0], read2.position[1], read2.position[2],
           read2.fov, read2.frame_id);
    
    if (read2.position[0] == 10.0f && read2.fov == 90.0f && read2.frame_id == 2) {
        printf("✓ WriteGuard PASSED\n");
    } else {
        printf("✗ WriteGuard FAILED\n");
    }
}

void test_update_tracking() {
    printf("\n=== Test 2: Update Tracking ===\n");
    
    auto producer = SharedBufferSPSC<CameraState>::create("TestTracking");
    auto consumer = SharedBufferSPSC<CameraState>::open("TestTracking");
    
    if (!producer || !consumer) {
        printf("ERROR: Failed to create/open buffer\n");
        return;
    }
    
    // Initially no update (version = 0)
    printf("Initial version: %llu\n", consumer->version());
    printf("has_update (before first write): %s\n", 
           consumer->has_update() ? "true" : "false");
    
    // Write first data
    CameraState state{};
    state.frame_id = 100;
    producer->write(state);
    
    printf("After first write - version: %llu, has_update: %s\n",
           consumer->version(), consumer->has_update() ? "true" : "false");
    
    // Read with try_read
    CameraState out{};
    bool got_update = consumer->try_read(out);
    printf("try_read returned: %s, frame_id: %u\n", 
           got_update ? "true" : "false", out.frame_id);
    
    // Now should not have update
    printf("After try_read - has_update: %s\n", 
           consumer->has_update() ? "true" : "false");
    
    // Try again - should return false
    got_update = consumer->try_read(out);
    printf("Second try_read (no new data): %s\n", 
           got_update ? "true" : "false");
    
    // Write new data
    state.frame_id = 200;
    producer->write(state);
    
    printf("After second write - has_update: %s\n", 
           consumer->has_update() ? "true" : "false");
    
    got_update = consumer->try_read(out);
    printf("try_read after second write: %s, frame_id: %u\n",
           got_update ? "true" : "false", out.frame_id);
    
    if (out.frame_id == 200) {
        printf("✓ Update tracking PASSED\n");
    } else {
        printf("✗ Update tracking FAILED\n");
    }
}

void test_performance() {
    printf("\n=== Test 3: Performance Benchmark ===\n");
    
    auto producer = SharedBufferSPSC<LargeData>::create("TestPerf");
    auto consumer = SharedBufferSPSC<LargeData>::open("TestPerf");
    
    if (!producer || !consumer) {
        printf("ERROR: Failed to create/open buffer\n");
        return;
    }
    
    constexpr int ITERATIONS = 1'000'000;
    
    //--- Benchmark: write() ---
    LargeData data{};
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; ++i) {
        data.sequence = i;
        producer->write(data);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto write_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    printf("write() x %d:\n", ITERATIONS);
    printf("  Total: %.2f ms\n", write_ns / 1e6);
    printf("  Per-op: %.1f ns\n", (double)write_ns / ITERATIONS);
    printf("  Throughput: %.2f M ops/sec\n", ITERATIONS / (write_ns / 1e9) / 1e6);
    printf("  Bandwidth: %.2f GB/s\n", (double)ITERATIONS * sizeof(LargeData) / write_ns);
    
    //--- Benchmark: read() ---
    start = std::chrono::high_resolution_clock::now();
    
    volatile uint64_t sum = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        sum += consumer->read().sequence;
    }
    (void)sum;  // Prevent optimization
    
    end = std::chrono::high_resolution_clock::now();
    auto read_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    printf("\nread() x %d:\n", ITERATIONS);
    printf("  Total: %.2f ms\n", read_ns / 1e6);
    printf("  Per-op: %.1f ns\n", (double)read_ns / ITERATIONS);
    printf("  Throughput: %.2f M ops/sec\n", ITERATIONS / (read_ns / 1e9) / 1e6);
    
    //--- Benchmark: has_update() (fast path) ---
    start = std::chrono::high_resolution_clock::now();
    
    volatile int updates = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        if (consumer->has_update()) updates++;
    }
    (void)updates;
    
    end = std::chrono::high_resolution_clock::now();
    auto check_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    printf("\nhas_update() x %d:\n", ITERATIONS);
    printf("  Total: %.2f ms\n", check_ns / 1e6);
    printf("  Per-op: %.1f ns\n", (double)check_ns / ITERATIONS);
    printf("  Throughput: %.2f M ops/sec\n", ITERATIONS / (check_ns / 1e9) / 1e6);
    
    //--- Benchmark: write_guard() (zero-copy) ---
    start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; ++i) {
        auto guard = producer->write_guard();
        guard->sequence = i;
        // Only modify one field - demonstrates zero-copy advantage
    }
    
    end = std::chrono::high_resolution_clock::now();
    auto guard_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    printf("\nwrite_guard() x %d (partial update):\n", ITERATIONS);
    printf("  Total: %.2f ms\n", guard_ns / 1e6);
    printf("  Per-op: %.1f ns\n", (double)guard_ns / ITERATIONS);
    printf("  Throughput: %.2f M ops/sec\n", ITERATIONS / (guard_ns / 1e9) / 1e6);
    
    printf("\n✓ Performance test completed\n");
}

void test_version_consistency() {
    printf("\n=== Test 4: Version Consistency ===\n");
    
    auto producer = SharedBufferSPSC<CameraState>::create("TestVersion");
    auto consumer = SharedBufferSPSC<CameraState>::open("TestVersion");
    
    if (!producer || !consumer) {
        printf("ERROR: Failed to create/open buffer\n");
        return;
    }
    
    printf("Note: version = (internal_state >> 1), starts at 0\n");
    printf("After N writes, version = N/2 (due to state encoding)\n\n");
    
    // Write multiple times and check version increases
    CameraState state{};
    uint64_t prev_version = consumer->version();
    bool all_passed = true;
    
    for (int i = 0; i < 10; ++i) {
        state.frame_id = i;
        producer->write(state);
        
        uint64_t ver = consumer->version();
        const auto& read_state = consumer->read();
        
        printf("Write %d: version=%llu, frame_id=%u\n", 
               i, ver, read_state.frame_id);
        
        // Each write should increase version or keep it same (due to floor division)
        // Actually: state goes 0->1->2->3..., version = state>>1 = 0,0,1,1,2,2...
        // So after odd writes version increases
        if (i > 0 && ver < prev_version) {
            printf("✗ Version decreased! prev=%llu, now=%llu\n", prev_version, ver);
            all_passed = false;
        }
        
        // Verify data is consistent
        if (read_state.frame_id != static_cast<uint32_t>(i)) {
            printf("✗ Data mismatch! Expected frame_id=%d, got=%u\n", i, read_state.frame_id);
            all_passed = false;
        }
        
        prev_version = ver;
    }
    
    if (all_passed) {
        printf("✓ Version consistency PASSED\n");
    } else {
        printf("✗ Version consistency FAILED\n");
    }
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("===============================================\n");
    printf("  SharedBufferSPSC Demo & Test\n");
    printf("===============================================\n");
    printf("CameraState size: %zu bytes\n", sizeof(CameraState));
    printf("LargeData size: %zu bytes\n", sizeof(LargeData));
    
    test_basic_operations();
    test_update_tracking();
    test_version_consistency();
    test_performance();
    
    printf("\n===============================================\n");
    printf("  All tests completed!\n");
    printf("===============================================\n");
    
    return 0;
}
