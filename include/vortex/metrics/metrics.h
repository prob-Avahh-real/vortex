#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vortex::metrics {

// ==========================================================================
// Prometheus-compatible metrics for RPC observability
// ==========================================================================
//
// Four metric types, matching the Prometheus data model:
//   - Counter: monotonically increasing count (requests, errors, bytes)
//   - Gauge: up/down value (in-flight, connections, memory)
//   - Histogram: distribution with configurable buckets (latency, size)
//   - Summary: quantile estimation (p50/p90/p99)
//
// Labels are key-value pairs for dimensional filtering.
// Thread-safe — all operations use std::atomic.

struct Label {
    std::string key;
    std::string value;
};

// ==========================================================================
// Counter
// ==========================================================================

class Counter {
public:
    explicit Counter(std::string name, std::string help,
                     std::vector<Label> labels = {});
    ~Counter();

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;

    auto inc(uint64_t delta = 1) -> void;
    [[nodiscard]] auto value() const -> uint64_t;

    // Prometheus exposition format
    [[nodiscard]] auto to_prometheus() const -> std::string;

private:
    std::string name_;
    std::string help_;
    std::vector<Label> labels_;
    std::atomic<uint64_t> value_{0};
};

// ==========================================================================
// Gauge
// ==========================================================================

class Gauge {
public:
    explicit Gauge(std::string name, std::string help,
                   std::vector<Label> labels = {});
    ~Gauge();

    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;

    auto set(double value) -> void;
    auto inc(double delta = 1.0) -> void;
    auto dec(double delta = 1.0) -> void;
    [[nodiscard]] auto value() const -> double;

    [[nodiscard]] auto to_prometheus() const -> std::string;

private:
    std::string name_;
    std::string help_;
    std::vector<Label> labels_;
    std::atomic<double> value_{0.0};
};

// ==========================================================================
// Histogram
// ==========================================================================

class Histogram {
public:
    // Buckets are upper boundaries in seconds (or bytes, for size histograms).
    // Default latency buckets: 100μs, 500μs, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s
    explicit Histogram(std::string name, std::string help,
                       std::vector<Label> labels = {},
                       std::vector<double> buckets = {
                           0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05,
                           0.1, 0.5, 1.0, 5.0
                       });
    ~Histogram();

    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

    // Observe a value (in seconds).
    auto observe(double value) -> void;

    [[nodiscard]] auto count() const -> uint64_t;
    [[nodiscard]] auto sum() const -> double;

    [[nodiscard]] auto to_prometheus() const -> std::string;

private:
    std::string name_;
    std::string help_;
    std::vector<Label> labels_;
    std::vector<double> buckets_;
    std::vector<std::atomic<uint64_t>> bucket_counts_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> sum_{0.0};
};

// ==========================================================================
// Summary
// ==========================================================================

class Summary {
public:
    // Quantiles to estimate: e.g., {0.5, 0.9, 0.99}
    explicit Summary(std::string name, std::string help,
                     std::vector<double> quantiles = {0.5, 0.9, 0.99},
                     std::vector<Label> labels = {},
                     std::chrono::seconds window = std::chrono::seconds(60));
    ~Summary();

    Summary(const Summary&) = delete;
    Summary& operator=(const Summary&) = delete;

    auto observe(double value) -> void;

    [[nodiscard]] auto count() const -> uint64_t;
    [[nodiscard]] auto sum() const -> double;
    [[nodiscard]] auto quantile(double q) const -> double;

    [[nodiscard]] auto to_prometheus() const -> std::string;

private:
    std::string name_;
    std::string help_;
    std::vector<double> quantiles_;
    std::vector<Label> labels_;
    std::chrono::seconds window_;

    // Simplified quantile: time-decaying EWMA + snapshot buffer
    // In production, use t-digest or HdrHistogram.
    mutable std::mutex mutex_;
    std::vector<double> samples_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> sum_{0.0};
};

// ==========================================================================
// MetricsRegistry — central registry for exposition
// ==========================================================================

class MetricsRegistry {
public:
    static auto instance() -> MetricsRegistry&;

    auto register_counter(std::shared_ptr<Counter> c) -> void;
    auto register_gauge(std::shared_ptr<Gauge> g) -> void;
    auto register_histogram(std::shared_ptr<Histogram> h) -> void;
    auto register_summary(std::shared_ptr<Summary> s) -> void;

    // Dump all metrics in Prometheus exposition format.
    [[nodiscard]] auto to_prometheus() const -> std::string;

private:
    MetricsRegistry() = default;

    std::mutex mutex_;
    std::vector<std::shared_ptr<Counter>> counters_;
    std::vector<std::shared_ptr<Gauge>> gauges_;
    std::vector<std::shared_ptr<Histogram>> histograms_;
    std::vector<std::shared_ptr<Summary>> summaries_;
};

}  // namespace vortex::metrics
