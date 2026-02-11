#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>

// Moodycamel SPSC queue
#include "readerwriterqueue.h"

// Our SPSC queue (C wrapper)
#include "zend_wrapper.h"

using namespace std;
using namespace std::chrono;

// Benchmark configuration
const size_t ITERATIONS = 10000000;
const size_t WARMUP_ITERATIONS = 10000;

struct BenchmarkResult {
    string name;
    double ops_per_sec;
    double ns_per_op;
    double duration_ms;
};

// Benchmark moodycamel queue
BenchmarkResult benchmark_moodycamel() {
    moodycamel::ReaderWriterQueue<void*> queue(ITERATIONS);  // Same size as number of items
    atomic<bool> done{false};

    auto start = high_resolution_clock::now();

    // Writer thread
    thread writer([&]() {
        for (size_t i = 0; i < ITERATIONS; i++) {
            void* item = (void*)(uintptr_t)(i + 1);
            while (!queue.try_enqueue(item)) {
                // Spin
            }
        }
        done = true;
    });

    // Reader thread
    thread reader([&]() {
        size_t count = 0;
        void* item;
        while (count < ITERATIONS) {
            if (queue.try_dequeue(item)) {
                count++;
            }
        }
    });

    writer.join();
    reader.join();

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);

    double ms = duration.count() / 1e6;
    double ops_per_sec = (ITERATIONS * 2.0) / (duration.count() / 1e9);
    double ns_per_op = duration.count() / (ITERATIONS * 2.0);

    return {"moodycamel::ReaderWriterQueue", ops_per_sec, ns_per_op, ms};
}

// Benchmark our SPSC queue
BenchmarkResult benchmark_zend_spsc() {
    void* queue = zend_queue_create(8);  // Default small size, will auto-resize

    atomic<bool> done{false};

    auto start = high_resolution_clock::now();

    // Writer thread
    thread writer([&]() {
        for (size_t i = 0; i < ITERATIONS; i++) {
            void* item = (void*)(uintptr_t)(i + 1);
            while (!zend_queue_push(queue, item)) {
                // Spin
            }
        }
        done = true;
    });

    // Reader thread
    thread reader([&]() {
        size_t count = 0;
        void* item;
        while (count < ITERATIONS) {
            if (zend_queue_pop(queue, &item)) {
                count++;
            }
        }
    });

    writer.join();
    reader.join();

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);

    zend_queue_destroy(queue);

    double ms = duration.count() / 1e6;
    double ops_per_sec = (ITERATIONS * 2.0) / (duration.count() / 1e9);
    double ns_per_op = duration.count() / (ITERATIONS * 2.0);

    return {"zend_spsc_queue (C)", ops_per_sec, ns_per_op, ms};
}

void print_results(const vector<BenchmarkResult>& results) {
    cout << "\n=== SPSC Queue Benchmark ===" << endl;
    cout << "Iterations: " << ITERATIONS << " (push + pop = " << (ITERATIONS * 2) << " ops)" << endl;
    cout << "\nResults:\n" << endl;

    // Find fastest for comparison
    double fastest_ops = 0;
    for (const auto& r : results) {
        if (r.ops_per_sec > fastest_ops) {
            fastest_ops = r.ops_per_sec;
        }
    }

    for (const auto& r : results) {
        double relative = r.ops_per_sec / fastest_ops;
        cout << r.name << ":" << endl;
        cout << "  Throughput: " << (r.ops_per_sec / 1e6) << " M ops/sec" << endl;
        cout << "  Latency:    " << r.ns_per_op << " ns/op" << endl;
        cout << "  Duration:   " << r.duration_ms << " ms" << endl;
        cout << "  Relative:   " << (relative * 100.0) << "% of fastest";
        if (relative == 1.0) {
            cout << " (FASTEST)";
        }
        cout << endl << endl;
    }
}

int main() {
    cout << "Warming up..." << endl;

    // Warmup
    {
        moodycamel::ReaderWriterQueue<void*> q(64);
        for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
            void* item = (void*)(i + 1);
            q.try_enqueue(item);
            q.try_dequeue(item);
        }
    }

    {
        void* q = zend_queue_create(64);
        for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
            void* item = (void*)(i + 1);
            zend_queue_push(q, item);
            zend_queue_pop(q, &item);
        }
        zend_queue_destroy(q);
    }

    cout << "Running benchmarks..." << endl << endl;

    vector<BenchmarkResult> results;

    // Run benchmarks
    results.push_back(benchmark_moodycamel());
    this_thread::sleep_for(milliseconds(100)); // Cool down

    results.push_back(benchmark_zend_spsc());

    print_results(results);

    return 0;
}
