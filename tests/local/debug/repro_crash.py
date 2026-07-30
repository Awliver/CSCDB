#!/usr/bin/env python3
import os, sys, time, threading, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import random
import tpcc_common as tc
from tpcc_transactions import run_neworder
from tpcc_scale import scale_profile
SCALE = scale_profile("mini")

DB = "repro_db"
LOG = os.path.join(tc.BUILD, "repro_server.log")

# start server with logging
proc, dbpath = tc.start_rmdb(DB, log_path=LOG)
cli = tc.RmdbClient(timeout=120)
for sql in tc.SCHEMA:
    ok, r = cli.query_ok(sql)
    assert ok, ("schema", sql, r)
for tab, path, _ in tc.LOADS:
    ok, r = cli.query_ok("load " + path + " into " + tab + ";")
    assert ok, ("load", tab, r)
for sql in tc.INDEXES:
    ok, r = cli.query_ok(sql)
    assert ok, ("index", sql, r)
cli.query("set transaction isolation level snapshot isolation")
cli.query("set output_file off")
cli.close()
print("bootstrap OK, server pid", proc.pid, "alive=", proc.poll() is None)

stop = False
counts = {"ok": 0, "fail": 0}
lock = threading.Lock()
first_err = [None]

def worker(wid):
    c = tc.RmdbClient(timeout=30)
    c.query("set transaction isolation level snapshot isolation")
    rng = random.Random(wid * 7919 + 1)
    while not stop:
        try:
            ok, err = run_neworder(c, rng, SCALE)
            with lock:
                counts["ok" if ok else "fail"] += 1
        except Exception as e:
            with lock:
                counts["fail"] += 1
                if first_err[0] is None:
                    first_err[0] = repr(e)
            break
    try: c.close()
    except: pass

ths = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
for t in ths: t.start()
t0 = time.time()
while time.time() - t0 < 12:
    time.sleep(0.5)
    if proc.poll() is not None:
        print("!!! SERVER DIED, returncode=", proc.returncode, "after", round(time.time()-t0,2), "s")
        break
stop = True
for t in ths: t.join(timeout=3)
print("counts", counts, "first_err", first_err[0], "server_alive", proc.poll() is None)
try: proc.kill()
except: pass
