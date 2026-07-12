#include <gtest/gtest.h>
#include <vortex/rpc/circuit_breaker.h>

#include <thread>

using namespace vortex::rpc;

TEST(CircuitBreaker, InitiallyClosed) {
    CircuitBreaker cb({});
    EXPECT_EQ(cb.state(), CircuitBreaker::State::kClosed);
    EXPECT_TRUE(cb.allow_request());
}

TEST(CircuitBreaker, TripOnHighErrorRate) {
    CircuitBreaker::Config cfg;
    cfg.error_threshold = 0.5;
    CircuitBreaker cb(cfg);

    // Feed failures above threshold
    for (int i = 0; i < 10; ++i) {
        cb.report_result(false);
    }
    // Feed one success
    cb.report_result(true);

    // Error rate = ~90% > 50% → should trip
    // Note: circuit opens only when allow_request() is called after threshold crossed
    cb.allow_request();  // trigger state evaluation
    cb.report_result(false);  // pushes error

    // After enough failures, circuit should open
    auto state = cb.state();
    EXPECT_NE(state, CircuitBreaker::State::kClosed);
}

TEST(CircuitBreaker, StaysClosedUnderThreshold) {
    CircuitBreaker::Config cfg;
    cfg.error_threshold = 0.5;
    CircuitBreaker cb(cfg);

    for (int i = 0; i < 10; ++i) {
        cb.report_result(true);
    }
    for (int i = 0; i < 3; ++i) {
        cb.report_result(false);
    }
    // Error rate = 3/13 ≈ 23% < 50%

    EXPECT_EQ(cb.state(), CircuitBreaker::State::kClosed);
}
