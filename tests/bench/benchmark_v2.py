#!/usr/bin/env python3
"""RMDB Benchmark v2 - larger, randomized, multi-repeat with stats"""
import socket, time, argparse, json, statistics, random

PORT, HOST = 8765, 'localhost'
BUF_SIZE = 16 * 1024 * 1024

def send_sql(s, sql):
    s.send((sql + '\0').encode())
    return s.recv(BUF_SIZE).decode('utf-8', errors='replace')

def time_one(s, sql):
    t0 = time.perf_counter_ns()
    send_sql(s, sql)
    return (time.perf_counter_ns() - t0) / 1e6

def stats3(times):
    if not times: return (0, 0, 0)
    s = sorted(times); n = len(s)
    med = s[n//2]
    p25 = s[max(0, n//4)]
    p75 = s[min(n-1, 3*n//4)]
    return (med, p25, p75)

NAMES = ['Alice', 'Bob', 'Carol', 'Dave', 'Eve', 'Frank', 'Grace', 'Helen']

def gen_row(i, rng):
    return f"({i}, {rng.randint(0, 100000)}, '{rng.choice(NAMES)}_{rng.randint(0,999)}')"

def setup_random(s, name, n, seed):
    rng = random.Random(seed)
    send_sql(s, f"create table {name} (id int, val int, name char(20));")
    for i in range(n):
        send_sql(s, f"insert into {name} values {gen_row(i, rng)};")

def drop(s, name): send_sql(s, f"drop table {name};")

def bench_insert(s, n, seed):
    rng = random.Random(seed)
    send_sql(s, "create table bm_ins (id int, val int, name char(20));")
    t0 = time.perf_counter_ns()
    for i in range(n):
        send_sql(s, f"insert into bm_ins values {gen_row(i, rng)};")
    t = (time.perf_counter_ns() - t0) / 1e6
    drop(s, "bm_ins")
    return t

def bench_select(s, n, where, seed, repeat=10):
    setup_random(s, "bm_sel", n, seed)
    times = [time_one(s, f"select * from bm_sel{where};") for _ in range(repeat)]
    drop(s, "bm_sel")
    return stats3(times)

def bench_projection(s, n, seed, repeat=10):
    setup_random(s, "bm_p", n, seed)
    times = [time_one(s, "select id, name from bm_p;") for _ in range(repeat)]
    drop(s, "bm_p")
    return stats3(times)

def bench_update(s, n, where, seed, repeat=3):
    times = []
    for r in range(repeat):
        setup_random(s, "bm_u", n, seed + r)
        times.append(time_one(s, f"update bm_u set val = 0{where};"))
        drop(s, "bm_u")
    return stats3(times)

def bench_delete(s, n, where, seed, repeat=3):
    times = []
    for r in range(repeat):
        times.append(time_one(s, f"delete from bm_d{where};"))
        drop(s, "bm_d")
    return stats3(times)

def bench_join(s, ln, rn, seed, repeat=3):
    rng = random.Random(seed)
    send_sql(s, "create table bm_jl (id int, val int);")
    send_sql(s, "create table bm_jr (id int, name char(20));")
    for i in range(ln):
        send_sql(s, f"insert into bm_jl values ({i}, {rng.randint(0, ln)});")
    rng = random.Random(seed)
    send_sql(s, "create table bm_ins (id int, val int, name char(20));")
    t0 = time.perf_counter_ns()
    for i in range(n):
        send_sql(s, f"insert into bm_ins values {gen_row(i, rng)};")
    t = (time.perf_counter_ns() - t0) / 1e6
    drop(s, "bm_ins")
    return t

def bench_select(s, n, where, seed, repeat=10):
    setup_random(s, "bm_sel", n, seed)
    times = [time_one(s, f"select * from bm_sel{where};") for _ in range(repeat)]
    drop(s, "bm_sel")
    return stats3(times)

def bench_projection(s, n, seed, repeat=10):
    setup_random(s, "bm_p", n, seed)
    times = [time_one(s, "select id, name from bm_p;") for _ in range(repeat)]
    drop(s, "bm_p")
    return stats3(times)

def bench_update(s, n, where, seed, repeat=3):
    times = []
    for r in range(repeat):
        setup_random(s, "bm_u", n, seed + r)
        times.append(time_one(s, f"update bm_u set val = 0{where};"))
        drop(s, "bm_u")
    return stats3(times)

def bench_delete(s, n, where, seed, repeat=3):
    times = []
    for r in range(repeat):
        setup_random(s, "bm_d", n, seed + r)
        times.append(time_one(s, f"delete from bm_d{where};"))
        drop(s, "bm_d")
    return stats3(times)

def bench_join(s, ln, rn, seed, repeat=3):
    rng = random.Random(seed)
    send_sql(s, "create table bm_jl (id int, val int);")
    send_sql(s, "create table bm_jr (id int, name char(20));")
    for i in range(ln):
        send_sql(s, f"insert into bm_jl values ({i}, {rng.randint(0, ln)});")
    for j in range(rn):
        send_sql(s, f"insert into bm_jr values ({j % ln}, 'r_{j}');")
    cart = stats3([time_one(s, "select * from bm_jl, bm_jr;") for _ in range(repeat)])
    equi = stats3([time_one(s, "select * from bm_jl, bm_jr where bm_jl.id = bm_jr.id;") for _ in range(repeat)])
    drop(s, "bm_jl"); drop(s, "bm_jr")
    return cart, equi

def warmup(s):
    send_sql(s, "create table _w (id int);")
    for i in range(50):
        send_sql(s, f"insert into _w values ({i});")
    send_sql(s, "select * from _w;")
    send_sql(s, "drop table _w;")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--label", default="run")
    p.add_argument("--scale", type=int, default=10000)
    p.add_argument("--join-scale", type=int, default=300)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--out")
    args = p.parse_args()

    N, JN = args.scale, args.join_scale
    print(f"\n=== RMDB Bench v2 [{args.label}] N={N} JN={JN} seed={args.seed} ===\n")
    s = socket.socket(); s.connect((HOST, PORT))
    print("  [warmup]", end=' ', flush=True); warmup(s); print("done\n")

    results = {"label": args.label, "scale": N, "join_scale": JN, "seed": args.seed, "benchmarks": {}}

    def run(name, fn):
        print(f"  {name:<28}", end=' ', flush=True)
        try:
            r = fn()
            if isinstance(r, tuple) and isinstance(r[0], tuple):
                cart, equi = r
                print(f"cart={cart[0]:8.2f}({cart[1]:6.1f}~{cart[2]:6.1f}) equi={equi[0]:8.2f}({equi[1]:6.1f}~{equi[2]:6.1f}) ms")
                results["benchmarks"][f"{name}_cart"] = cart[0]
                results["benchmarks"][f"{name}_equi"] = equi[0]
            elif isinstance(r, tuple):
                med, p25, p75 = r
                print(f"{med:8.2f} ms (IQR {p25:.2f}~{p75:.2f})")
                results["benchmarks"][name] = med
            else:
                print(f"{r:8.2f} ms")
                results["benchmarks"][name] = r
        except Exception as e:
            print(f"FAILED: {e}")

    seed = args.seed
    run(f"insert_{N}",         lambda: bench_insert(s, N, seed))
    run("select_all",          lambda: bench_select(s, N, "", seed))
    run("select_eq",           lambda: bench_select(s, N, " where val = 12345", seed))
    run("select_lt_90p",       lambda: bench_select(s, N, " where val < 90000", seed))
    run("select_lt_50p",       lambda: bench_select(s, N, " where val < 50000", seed))
    run("select_lt_10p",       lambda: bench_select(s, N, " where val < 10000", seed))
    run("select_lt_1p",        lambda: bench_select(s, N, " where val < 1000", seed))
    run("select_compound",     lambda: bench_select(s, N, " where val > 10000 and val < 60000", seed))
    run("select_projection",   lambda: bench_projection(s, N, seed))
    run("update_all",          lambda: bench_update(s, N, "", seed))
    run("update_half",         lambda: bench_update(s, N, " where val < 50000", seed))
    run("update_few",          lambda: bench_update(s, N, " where val < 1000", seed))
    run("delete_all",          lambda: bench_delete(s, N, "", seed))
    run("delete_half",         lambda: bench_delete(s, N, " where val < 50000", seed))
    run("delete_few",          lambda: bench_delete(s, N, " where val < 1000", seed))
    run(f"join_{JN}x{JN}",     lambda: bench_join(s, JN, JN, seed))
    run(f"join_{JN//3}x{JN*5}",lambda: bench_join(s, JN//3, JN*5, seed))
    run(f"join_{JN*5}x{JN//3}",lambda: bench_join(s, JN*5, JN//3, seed))

    run(f"join_{JN//3}x{JN*5}",lambda: bench_join(s, JN//3, JN*5, seed))
    run(f"join_{JN*5}x{JN//3}",lambda: bench_join(s, JN*5, JN//3, seed))

    s.close()
    if args.out:
        with open(args.out, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\n→ {args.out}")

if __name__ == "__main__":
    main()
