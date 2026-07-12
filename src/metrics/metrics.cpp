#include <vortex/metrics/metrics.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vortex::metrics {

// ==========================================================================
// Utilities
// ==========================================================================

static auto format_labels(const std::vector<Label>& labels) -> std::string {
    if (labels.empty()) return "";
    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) ss << ",";
        ss << labels[i].key << "=\"" << labels[i].value << "\"";
    }
    ss << "}";
    return ss.str();
}

static auto escape_string(const std::string& s) -> std::string {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

// ==========================================================================
// Counter
// ==========================================================================

Counter::Counter(std::string name, std::string help, std::vector<Label> labels)
    : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)) {}
Counter::~Counter() = default;

auto Counter::inc(uint64_t delta) -> void {
    value_.fetch_add(delta, std::memory_order_relaxed);
}

auto Counter::value() const -> uint64_t {
    return value_.load(std::memory_order_relaxed);
}

auto Counter::to_prometheus() const -> std::string {
    std::stringstream ss;
    ss << "# HELP " << name_ << " " << help_ << "\n";
    ss << "# TYPE " << name_ << " counter\n";
    ss << name_ << format_labels(labels_) << " " << value() << "\n";
    return ss.str();
}

// ==========================================================================
// Gauge
// ==========================================================================

Gauge::Gauge(std::string name, std::string help, std::vector<Label> labels)
    : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)) {}
Gauge::~Gauge() = default;

auto Gauge::set(double value) -> void {
    value_.store(value, std::memory_order_relaxed);
}

auto Gauge::inc(double delta) -> void {
    value_.fetch_add(delta, std::memory_order_relaxed);
}

auto Gauge::dec(double delta) -> void {
    value_.fetch_sub(delta, std::memory_order_relaxed);
}

auto Gauge::value() const -> double {
    return value_.load(std::memory_order_relaxed);
}

auto Gauge::to_prometheus() const -> std::string {
    std::stringstream ss;
    ss << "# HELP " << name_ << " " << help_ << "\n";
    ss << "# TYPE " << name_ << " gauge\n";
    ss << name_ << format_labels(labels_) << " " << value() << "\n";
    return ss.str();
}

// ==========================================================================
// Histogram
// ==========================================================================

Histogram::Histogram(std::string name, std::string help,
                      std::vector<Label> labels,
                      std::vector<double> buckets)
    : name_(std::move(name))
    , help_(std::move(help))
    , labels_(std::move(labels))
    , buckets_(std::move(buckets))
    , bucket_counts_(buckets_.size() + 1) {  // +1 for +Inf
    // Buckets must be sorted
    std::sort(buckets_.begin(), buckets_.end());
}
Histogram::~Histogram() = default;

auto Histogram::observe(double value) -> void {
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(value, std::memory_order_relaxed);

    // Find the first bucket >= value
    for (size_t i = 0; i < buckets_.size(); ++i) {
        if (value <= buckets_[i]) {
            bucket_counts_[i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    // Falls into +Inf bucket
    bucket_counts_.back().fetch_add(1, std::memory_order_relaxed);
}

auto Histogram::count() const -> uint64_t {
    return count_.load(std::memory_order_relaxed);
}

auto Histogram::sum() const -> double {
    return sum_.load(std::memory_order_relaxed);
}

auto Histogram::to_prometheus() const -> std::string {
    std::stringstream ss;
    ss << "# HELP " << name_ << " " << help_ << "\n";
    ss << "# TYPE " << name_ << " histogram\n";

    auto labels_str = format_labels(labels_);
    uint64_t cumulative = 0;

    for (size_t i = 0; i < buckets_.size(); ++i) {
        cumulative += bucket_counts_[i].load(std::memory_order_relaxed);
        ss << name_ << "_bucket" << labels_str
           << "{le=\"" << buckets_[i] << "\"} "
           << cumulative << "\n";
    }
    // +Inf bucket
    cumulative += bucket_counts_.back().load(std::memory_order_relaxed);
    ss << name_ << "_bucket" << labels_str
       << "{le=\"+Inf\"} " << cumulative << "\n";

    ss << name_ << "_count" << labels_str << " " << count() << "\n";
    ss << name_ << "_sum" << labels_str << " " << sum() << "\n";
    return ss.str();
}

// ==========================================================================
// Summary
// ==========================================================================

Summary::Summary(std::string name, std::string help,
                  std::vector<double> quantiles,
                  std::vector<Label> labels,
                  std::chrono::seconds window)
    : name_(std::move(name))
    , help_(std::move(help))
    , quantiles_(std::move(quantiles))
    , labels_(std::move(labels))
    , window_(window)
    , samples_(1024) {  // Reserve space
    std::sort(quantiles_.begin(), quantiles_.end());
}
Summary::~Summary() = default;

auto Summary::observe(double value) -> void {
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(value, std::memory_order_relaxed);

    std::lock_guard lock(mutex_);
    if (samples_.size() >= 1024) {
        samples_.erase(samples_.begin());  // Simple ring buffer
    }
    samples_.push_back(value);
}

auto Summary::count() const -> uint64_t {
    return count_.load(std::memory_order_relaxed);
}

auto Summary::sum() const -> double {
    return sum_.load(std::memory_order_relaxed);
}

auto Summary::quantile(double q) const -> double {
    std::lock_guard lock(mutex_);
    if (samples_.empty()) return 0.0;

    std::vector<double> sorted(samples_.begin(), samples_.end());
    std::sort(sorted.begin(), sorted.end());

    auto pos = static_cast<size_t>(q * sorted.size());
    if (pos >= sorted.size()) pos = sorted.size() - 1;
    return sorted[pos];
}

auto Summary::to_prometheus() const -> std::string {
    std::stringstream ss;
    ss << "# HELP " << name_ << " " << help_ << "\n";
    ss << "# TYPE " << name_ << " summary\n";

    auto labels_str = format_labels(labels_);

    for (auto q : quantiles_) {
        ss << name_ << labels_str
           << "{quantile=\"" << q << "\"} "
           << quantile(q) << "\n";
    }
    ss << name_ << "_count" << labels_str << " " << count() << "\n";
    ss << name_ << "_sum" << labels_str << " " << sum() << "\n";
    return ss.str();
}

// ==========================================================================
// MetricsRegistry
// ==========================================================================

auto MetricsRegistry::instance() -> MetricsRegistry& {
    static MetricsRegistry reg;
    return reg;
}

auto MetricsRegistry::register_counter(std::shared_ptr<Counter> c) -> void {
    std::lock_guard lock(mutex_);
    counters_.push_back(std::move(c));
}

auto MetricsRegistry::register_gauge(std::shared_ptr<Gauge> g) -> void {
    std::lock_guard lock(mutex_);
    gauges_.push_back(std::move(g));
}

auto MetricsRegistry::register_histogram(std::shared_ptr<Histogram> h) -> void {
    std::lock_guard lock(mutex_);
    histograms_.push_back(std::move(h));
}

auto MetricsRegistry::register_summary(std::shared_ptr<Summary> s) -> void {
    std::lock_guard lock(mutex_);
    summaries_.push_back(std::move(s));
}

auto MetricsRegistry::to_prometheus() const -> std::string {
    std::stringstream ss;
    std::lock_guard lock(mutex_);

    for (auto& c : counters_) ss << c->to_prometheus();
    for (auto& g : gauges_) ss << g->to_prometheus();
    for (auto& h : histograms_) ss << h->to_prometheus();
    for (auto& s : summaries_) ss << s->to_prometheus();

    return ss.str();
}

}  // namespace vortex::metrics
