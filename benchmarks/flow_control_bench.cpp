#include <benchmark/benchmark.h>
#include <vortex/transport/flow_control.h>

using namespace vortex::transport;

static void BM_FlowControlWindowUpdate(benchmark::State& state) {
    StreamFlowControl fc(65535);
    for (auto _ : state) {
        fc.on_window_update(1024);
        fc.consume_send_window(1024);
    }
}
BENCHMARK(BM_FlowControlWindowUpdate);

static void BM_FlowControlReceive(benchmark::State& state) {
    StreamFlowControl fc(1 << 24);
    for (auto _ : state) {
        auto remaining = fc.on_data_received(256);
        benchmark::DoNotOptimize(remaining);
        fc.send_window_update(256);
    }
}
BENCHMARK(BM_FlowControlReceive);

BENCHMARK_MAIN();
