#!/usr/bin/env python3
"""RMDB Performance Benchmark Suite"""
import socket, time, argparse, json, statistics, sys

PORT, HOST = 8765, 'localhost'

def send_sql(s, sql):
    s.send((sql + '\0').encode())
    return s.recv(4 * 1024 * 1024).decode('utf-8', errors='replace')

def time_one(s, sql):
    t0 = time.perf_counter_ns()
    send_sql(s, sql)
    return (time.perf_counter_ns() - t0) / 1e6  # ms

def setup_table(s, name, schema, n, gen):
    send_sql(s, f"create table {name} {schema};")
    for i in range(n):
        send_sql(s, gen(i))

def drop(s, name):
    send_sql(s, f"drop table {name};")

# === 基准函数 ===

def bench_insert(s, n):
    send_sql(s, "create table bm_ins (id int, val int);")
    t0 = time.perf_counter_ns()
    for i in range(n):
        send_sql(s, f"insert into bm_ins values ({i}, {i*7});")
    t = (time.perf_counter_ns() - t0) / 1e6
    drop(s, "bm_ins")
    return t

def bench_select(s, n, where, repeat=5):
    setup_table(s, "bm_sel", "(id int, val int)", n,
                lambda i: f"insert into bm_sel values ({i}, {i*7});")
    times = [time_one(s, f"select * from bm_sel{where};") for _ in range(repeat)]
    drop(s, "bm_sel")
    return statistics.median(times)

def bench_select_projection(s, n, repeat=5):
    setup_table(s, "bm_proj", "(id int, val int, name char(30))", n,
                lambda i: f"insert into bm_proj values ({i}, {i*7}, 'data_{i%100}');")
    times = [time_one(s, "select id from bm_proj;") for _ in range(repeat)]
    drop(s, "bm_proj")
    return statistics.median(times)

def bench_update(s, n):
    setup_table(s, "bm_upd", "(id int, val int)", n,
                lambda i: f"insert into bm_upd values ({i}, {i*7});")
    t = time_one(s, "update bm_upd set val = 0;")
    drop(s, "bm_upd")
    return t

def bench_delete(s, n):
    setup_table(s, "bm_del", "(id int, val int)", n,
                lambda i: f"insert into bm_del values ({i}, {i*7});")
    t = time_one(s, "delete from bm_del;")
    drop(s, "bm_del")
    return t

def bench_join(s, ln, rn, repeat=3):
    send_sql(s, "create table bm_jl (id int, val int);")
    send_sql(s, "create table bm_jr (id int, name char(10));")
    for i in range(ln):
        send_sql(s, f"insert into bm_jl values ({i}, {i*7});")
    for j in range(rn):
        send_sql(s, f"insert into bm_jr values ({j%ln}, 'r_{j%100}');")
    cart = statistics.median([time_one(s, "select * from bm_jl, bm_jr;") for _ in range(repeat)])
    equi = statistics.median([time_one(s, "select * from bm_jl, bm_jr where bm_jl.id = bm_jr.id;") for _ in range(repeat)])
    drop(s, "bm_jl"); drop(s, "bm_jr")
    return cart, equi

# === main ===

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--label", default="run")
    p.add_argument("--scale", type=int, default=1000)
    p.add_argument("--join-scale", type=int, default=100)
    p.add_argument("--out")
    args = p.parse_args()

    N, JN = args.scale, args.join_scale
    print(f"\n=== RMDB Benchmark [{args.label}] N={N} JOIN={JN}x{JN} ===\n")
    s = socket.socket(); s.connect((HOST, PORT))
    results = {"label": args.label, "scale": N, "join_scale": JN, "benchmarks": {}}

    def run(name, fn):
        print(f"  {name:<28}", end=' ', flush=True)
        try:
            t = fn()
            if isinstance(t, tuple):
                print(f"cart={t[0]:8.2f} ms  equi={t[1]:8.2f} ms")
                results["benchmarks"][f"{name}_cart"] = t[0]
                results["benchmarks"][f"{name}_equi"] = t[1]
            else:
                print(f"{t:8.2f} ms")
                results["benchmarks"][name] = t
        except Exception as e:
            print(f"FAILED: {e}")

    run(f"insert_{N}",            lambda: bench_insert(s, N))
    run(f"select_all_{N}",        lambda: bench_select(s, N, "", 5))
    run(f"select_filter_50p",     lambda: bench_select(s, N, f" where val < {N*7//2}", 5))
    run(f"select_filter_10p",     lambda: bench_select(s, N, f" where val < {N*7//10}", 5))
    run(f"select_filter_1p",      lambda: bench_select(s, N, f" where val < {N*7//100}", 5))
    run(f"select_projection_{N}", lambda: bench_select_projection(s, N, 5))
    run(f"update_all_{N}",        lambda: bench_update(s, N))
    run(f"delete_all_{N}",        lambda: bench_delete(s, N))
    run(f"join_{JN}x{JN}",        lambda: bench_join(s, JN, JN, 3))
    run(f"join_50x500",           lambda: bench_join(s, 50, 500, 3))

    s.close()
    if args.out:
        with open(args.out, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\n→ Saved to {args.out}")

if __name__ == "__main__":
    main()
