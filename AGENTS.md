# vortex

Vortex RPC **High-performance C++23 RPC framework** — io_uring · zero-copy codec · lock-free concurrency · distributed governance Vortex is a from-scratch async RPC framework targeting <100μs p99 tail latency for distributed systems: trading gateways, real-time game backends, DB middleware, and ad bidding engines. It layers io_uring, zero-copy serialization, and shared-nothing coroutine scheduling

## 用途与 Target 判定

开发者工具（已归档）；一般不做穿戴双 Target

- 移动端：不适用
- 智能硬件：不适用

## 目标平台

不按消费级「手机+智能硬件」双 Target 强求。

## 命令

```bash
# 见 README；归档工程以只读/小修为主
```

## 状态

已归档。默认只做必要修复与知识库同步，不新开大范围需求。

## 助手约束

- Loop：先定可验证 Done 与停止条件；每切片过验证环；失败回灌；详见 docs/LOOP_ENGINEERING.md
- Harness：遵循根 AGENTS「Harness Engineering」；Guide=本文件，Sensor=测试/CI；同类失误二次须沉淀规则或测试（棘轮）。详见 docs/HARNESS_ENGINEERING.md
- 遵循仓库根 `/Users/skat/AGENTS.md` 与本文件的用途判定
- 可选 Target 可搁置；不适用形态不要硬加
- 改动聚焦；公共逻辑抽离复用
