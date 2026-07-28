# TPC-C 性能剖析（本地）

与 `tests/local/bench_tpcc.py` 配套，在压测期间采集 perf 火焰图与 ebpf 指标。

## 依赖安装

```bash
bash tests/prof/install_deps.sh
cp tests/prof/config.env.example tests/prof/config.env   # 可选
```

建议用 **RelWithDebInfo + `-fno-omit-frame-pointer`** 编译，火焰图栈才完整：

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
make rmdb -j$(nproc)
```

## 火焰图（perf record）

```bash
# 默认：5s 预热 + 60s 测量，16 线程，W=5 full 数据
bash tests/prof/profile_tpcc.sh

# 快速冒烟
bash tests/prof/profile_tpcc.sh --quick

# 自定义
bash tests/prof/profile_tpcc.sh --measure 120 --threads 16 --scale full
```

输出目录：`build/prof_out/<timestamp>/`

| 文件 | 说明 |
|------|------|
| `flamegraph.svg` | 可浏览器打开的火焰图 |
| `perf.data` | 原始 perf 采样 |
| `bench.log` | 压测 stdout |

## 宏观计数（perf stat）

```bash
bash tests/prof/perf_stat_tpcc.sh --quick
```

关注：`cycles`, `instructions`, `cache-misses`, `context-switches`, `page-faults`。

## eBPF（bpftrace）

```bash
# 跟踪 rmdb 进程写盘/刷盘 syscall（需 root）
sudo bpftrace tests/prof/bpftrace_tpcc.bt -c \
  "python3 tests/local/bench_tpcc.py --quick --skip-crash"
```

或附加到已启动的 rmdb：

```bash
sudo bpftrace tests/prof/bpftrace_tpcc.bt -p $(pgrep -f 'bin/rmdb')
```

## 与 OJ 正式测试对齐

正式性能测试请直接跑（详见 [`tests/README.md` →「OJ 性能测试对齐」](../tests/README.md)）：

```bash
# 首次需生成 W=5 全量数据（约数分钟）
python3 tests/local/generate_tpcc_data.py --scale full

# 最接近线上 OJ 的本地验收
python3 tests/local/run_oj_perf_test.py --strict

# 快速验证（mini 数据，15s，不能代表排名 tpmC）
python3 tests/local/bench_tpcc.py --scale mini --quick
```
