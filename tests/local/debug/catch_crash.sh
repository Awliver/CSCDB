#!/bin/bash
# Launch release rmdb under gdb, drive 8-thread load, capture crash backtrace.
set -u
ROOT=/home/neo/CSC_DB/db2026
cd "$ROOT/build"
pkill -9 -f "bin/rmdb" 2>/dev/null
sleep 1
rm -rf tpcc_crash_gdb && mkdir -p tpcc_crash_gdb
GDBLOG=/tmp/gdb_rmdb.log
rm -f "$GDBLOG"

gdb -batch \
    -ex "set pagination off" \
    -ex "handle SIGSEGV stop" \
    -ex "handle SIGABRT stop" \
    -ex "run" \
    -ex "echo \n===CRASH SIGNAL CAUGHT===\n" \
    -ex "thread apply all bt" \
    --args ./bin/rmdb tpcc_crash_gdb > "$GDBLOG" 2>&1 &
GPID=$!

# wait for server to listen
for i in $(seq 1 30); do
    if ss -ltn 2>/dev/null | grep -q ':8765'; then break; fi
    sleep 0.3
done
echo "server listening check done"

cd "$ROOT"
timeout 130 python3 tests/local/debug/stress8_client.py > /tmp/stress_client.log 2>&1
echo "=== client log tail ==="
tail -15 /tmp/stress_client.log

# give gdb a moment to dump if crashed
sleep 2
if kill -0 $GPID 2>/dev/null; then
    echo "=== server still alive (no crash). killing gdb ==="
    kill -9 $GPID 2>/dev/null
    pkill -9 -f "bin/rmdb" 2>/dev/null
else
    echo "=== gdb exited (server crashed) ==="
fi
echo "=== gdb log ==="
cat "$GDBLOG"
