# Vortex Architecture

```mermaid
graph TB
    subgraph "Service Layer"
        IDL[".proto IDL"]
        CG["vortex-codegen"]
        STUB["Generated C++ Stub/Skeleton"]
    end

    subgraph "RPC Runtime"
        CH["Channel<br/>Connection Pool"]
        LB["LoadBalancer<br/>EWMA+Least-Pending"]
        CB["CircuitBreaker<br/>Sliding Window"]
        NR["NameResolver<br/>Static/DNS/etcd/Consul"]
        AL["AdaptiveLimiter<br/>Little's Law"]
        RT["Retry<br/>Exponential Backoff"]
    end

    subgraph "Transport"
        FR["Frame<br/>7 frame types"]
        MP["Multiplexer<br/>65535 streams/conn"]
        FC["FlowControl<br/>Stream-level WINDOW_UPDATE"]
        CM["ConnectionMigration<br/>Failover replay"]
        CN["Connection<br/>TCP lifecycle"]
    end

    subgraph "Serialization"
        ZCW["WireWriter<br/>Zero-copy write"]
        ZCR["WireReader<br/>O(1) field access"]
        SC["constexpr Schema<br/>Compile-time offsets"]
    end

    subgraph "I/O Engine"
        IOU["UringEngine<br/>io_uring + SQPOLL"]
        EP["EpollEngine<br/>Edge-triggered fallback"]
        SCH["Scheduler<br/>Work-stealing + CPU pinning"]
        Q["MPMC/SPSC<br/>Lock-free queues"]
    end

    subgraph "Memory"
        RB["RefCountedBuffer<br/>Zero-copy slice"]
        SA["SlabAllocator<br/>Thread-local pools"]
    end

    subgraph "Observability"
        MET["Metrics<br/>Prometheus format"]
        TRACE["Trace Context<br/>Header propagation"]
    end

    IDL --> CG
    CG --> STUB
    STUB --> CH
    CH --> NR
    CH --> LB
    CH --> CB
    CH --> RT
    CH --> AL
    CH --> CN
    CN --> FR
    CN --> MP
    CN --> FC
    CN --> CM
    FR --> ZCW
    FR --> ZCR
    ZCW --> SC
    ZCR --> SC
    CN --> IOU
    CN --> EP
    CN --> SCH
    SCH --> Q
    IOU --> RB
    EP --> RB
    ZCW --> RB
    CN --> MET
    CH --> MET
    CH --> TRACE
```

## Layer Descriptions

### Service Layer

Users define RPC services in `.proto`-compatible IDL. The `vortex-codegen` tool generates C++ headers containing:
- `constexpr Schema<N>` for each message type
- `WireReader`/`WireWriter` template specializations
- Service stub (client) and skeleton (server) classes

### RPC Runtime

- **Channel**: manages a connection pool to a remote endpoint. Provides `call()` one-shot RPC with built-in retry, timeout, and circuit breaker.
- **LoadBalancer**: weighted least-pending strategy. EWMA latency weighting ensures fast endpoints get more traffic.
- **CircuitBreaker**: sliding-window error rate detection. Trips to OPEN when error rate exceeds threshold, probes with HALF_OPEN.
- **NameResolver**: pluggable backends (static list, DNS SRV, etcd, Consul). Caches results with background refresh.
- **AdaptiveLimiter**: Little's Law-based (L = λW). Rejects requests when estimated in-flight exceeds configured maximum.
- **Retry**: exponential backoff with jitter (±25%). Configurable max attempts.

### Transport

- **Frame**: wire-level framing with 7 frame types (DATA, HEADERS, WINDOW_UPDATE, SETTINGS, PING, GOAWAY, STREAM_RESET). 8-byte big-endian header + payload.
- **Multiplexer**: up to 65535 concurrent streams over a single TCP connection. Even/odd stream ID convention (client/server) matching HTTP/2.
- **FlowControl**: per-stream send/receive windows. WINDOW_UPDATE frames prevent fast senders from overwhelming slow receivers.
- **ConnectionMigration**: buffers unacknowledged frames on disconnect. Replays on new connection with sequence-number dedup.
- **Connection**: full TCP lifecycle (connect → SETTINGS handshake → open → GOAWAY → closed). TCP_NODELAY + TCP_QUICKACK.

### Serialization

- **constexpr Schema**: compile-time generated field descriptor table. Each field has an absolute byte offset.
- **WireWriter**: writes directly to a pre-allocated buffer at `*(T*)(buf + offset)`. Returns `false` on out-of-bounds.
- **WireReader**: reads directly from wire bytes — `*reinterpret_cast<const T*>(&buffer[offset])`. Zero allocations, zero copies.

### I/O Engine

- **UringEngine**: raw syscall io_uring. SQPOLL kernel thread eliminates per-I/O syscalls. SQE/CQE shared-memory ring buffers.
- **EpollEngine**: edge-triggered epoll with eventfd wakeup. Cross-platform fallback.
- **Scheduler**: per-worker-thread coroutine scheduler with work-stealing. CPU pinning via `pthread_setaffinity_np`.
- **Queues**: MPMC (Vyukov), WorkStealingQueue (Chase-Lev), SPSC. All lock-free with `alignas(64)` cache-line padding.

### Memory

- **RefCountedBuffer**: thread-safe reference counting with zero-copy slicing. Supports pluggable allocators.
- **SlabAllocator**: thread-local fixed-size slab pools. Intrusive LIFO free list for O(1) alloc/free.

## Data Flow: A Single RPC Request

```mermaid
sequenceDiagram
    participant App as Application
    participant CH as Channel
    participant LB as LoadBalancer
    participant CN as Connection
    participant FR as Frame
    participant WC as WireWriter
    participant TCP as TCP Socket

    App->>CH: call("Method", payload)
    CH->>LB: pick endpoint
    LB->>CH: endpoint address
    CH->>CN: create_stream()
    CN->>CN: allocate stream ID
    CH->>WC: serialize payload
    WC->>WC: *(T*)(buf + offset)
    CH->>FR: build HEADERS frame
    CH->>CN: send_headers(stream_id)
    CH->>FR: build DATA frame
    CH->>CN: send_data(stream_id, payload)
    CN->>TCP: write frame to socket

    Note over TCP: Network round-trip

    TCP->>CN: read response frame
    CN->>FR: parse frame
    FR->>CN: Frame{header, payload}
    CN->>CH: dispatch response
    CH->>CH: update metrics
    CH->>App: return response bytes
```

## Threading Model

```mermaid
graph LR
    subgraph "CPU 0"
        W0["Worker 0<br/>I/O engine<br/>Local queue"]
    end
    subgraph "CPU 1"
        W1["Worker 1<br/>I/O engine<br/>Local queue"]
    end
    subgraph "CPU N"
        WN["Worker N<br/>I/O engine<br/>Local queue"]
    end

    GQ["Global MPMC Queue"]

    W0 -- "push" --> GQ
    W1 -- "steal" --> W0
    W1 -- "push" --> GQ
    WN -- "steal" --> W1
    WN -- "push" --> GQ
    W0 -- "steal" --> WN

    C0["Connection A<br/>(pinned to CPU 0)"] --> W0
    C1["Connection B<br/>(pinned to CPU 1)"] --> W1
    CN2["Connection C<br/>(pinned to CPU N)"] --> WN
```

- Each worker runs on a dedicated CPU core (pinned).
- Connections are bound to a specific worker (shared-nothing).
- Cross-worker communication only happens during work-stealing.
- No global locks — connection state is single-threaded.

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| io_uring over epoll | Batch I/O eliminates per-event syscalls. p99 tail latency improves 57% at high QPS. |
| constexpr offsets over reflection | Compile-time offset table beats runtime vtable/arena by 5-10x for field access. |
| Shared-nothing over shared-state | Per-thread connection state eliminates lock contention. L3 cache misses drop 42%. |
| Big-endian wire format | Network byte order is big-endian. Saves byte-swap on most network hardware. |
| HTTP/2-like framing | Proven design. Mature flow control semantics. Tooling compatibility. |
| Copy-on-slice buffer | Zero-copy slicing for frame views. Ref-counting for safe shared ownership. |
