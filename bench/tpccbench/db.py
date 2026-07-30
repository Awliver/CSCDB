"""RMDB socket client + server lifecycle.

Wire protocol: send SQL text terminated by '\\0'; receive response terminated by '\\0'.
Result tables are ASCII-art: | cell | cell | rows with a header row.
"""

import os
import shutil
import signal
import socket
import subprocess
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../.."))
BUILD = os.path.join(ROOT, "build")
RMDB_BIN = os.path.join(BUILD, "bin/rmdb")
HOST = "127.0.0.1"
PORT = 8765


class ServerDied(RuntimeError):
    """Connection dropped mid-query: the server process is gone or reset us."""


class StmtTimeout(RuntimeError):
    """Single statement exceeded the client socket timeout."""


class Client:
    def __init__(self, host=HOST, port=PORT, timeout=120):
        self.sock = socket.socket()
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if timeout:
            self.sock.settimeout(timeout)
        self.sock.connect((host, port))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def query(self, sql):
        """Send one statement, return raw response text (without trailing NUL)."""
        try:
            self.sock.sendall((sql + "\x00").encode())
            buf = b""
            while b"\x00" not in buf:
                chunk = self.sock.recv(1 << 16)
                if not chunk:
                    raise ServerDied("connection closed on: " + sql[:120])
                buf += chunk
        except socket.timeout:
            raise StmtTimeout("timeout on: " + sql[:120])
        except (ConnectionResetError, BrokenPipeError) as e:
            raise ServerDied("%s on: %s" % (type(e).__name__, sql[:120]))
        return buf.split(b"\x00")[0].decode(errors="replace")

    # ---- classified execution -------------------------------------------------
    # RMDB responses: result table / empty / "abort\n" (txn killed, e.g. deadlock
    # prevention) / "Error: ..." / "failure".  Distinguish system aborts (retryable,
    # expected under contention) from genuine SQL errors (a bug in query or engine).

    def exec(self, sql):
        """Return (status, resp): status in {'ok','abort','error'}."""
        r = self.query(sql)
        low = r.lower()
        if "abort" in low:
            return "abort", r
        if "error" in low or "failure" in low:
            return "error", r
        return "ok", r

    def must(self, sql):
        """Execute; raise on any non-ok status. For setup/DDL paths."""
        st, r = self.exec(sql)
        if st != "ok":
            raise RuntimeError("[%s] %s -> %s" % (st, sql[:120], r[:200]))
        return r

    def rows(self, sql):
        """Execute a SELECT; return (status, parsed data rows)."""
        st, r = self.exec(sql)
        if st != "ok":
            return st, []
        return "ok", parse_rows(r)

    def rollback_quiet(self):
        """Best-effort abort of current txn; swallow every outcome."""
        try:
            self.query("abort;")
        except (ServerDied, StmtTimeout):
            pass


# ---- response parsing ----------------------------------------------------------

def _is_header(cells):
    """A header row contains no numeric cell."""
    seen = False
    for c in cells:
        c = c.strip()
        if not c:
            continue
        seen = True
        try:
            float(c)
            return False
        except ValueError:
            pass
    return seen


def parse_rows(resp):
    """Extract data rows (lists of stripped cell strings) from table output."""
    rows = []
    for line in resp.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if cells and not _is_header(cells):
            rows.append(cells)
    return rows


def one_int(rows, col=0, default=None):
    if not rows:
        return default
    return int(float(rows[0][col]))


def one_float(rows, col=0, default=None):
    if not rows:
        return default
    return float(rows[0][col])


# ---- server lifecycle ------------------------------------------------------------

class Server:
    """Owns an rmdb server process bound to a db directory under build/."""

    def __init__(self, db_name, log_path=None, fresh=True):
        self.db_name = db_name
        self.db_path = os.path.join(BUILD, db_name)
        self.log_path = log_path or os.path.join(BUILD, db_name + ".server.log")
        self.fresh = fresh
        self.proc = None

    def start(self, wait_sec=30):
        if not os.path.isfile(RMDB_BIN):
            raise FileNotFoundError(RMDB_BIN + " not built; run: cd build && make rmdb -j$(nproc)")
        kill_all_rmdb()
        if self.fresh and os.path.isdir(self.db_path):
            shutil.rmtree(self.db_path)
        os.makedirs(self.db_path, exist_ok=True)
        logf = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [RMDB_BIN, self.db_name], cwd=BUILD, stdout=logf, stderr=subprocess.STDOUT)
        deadline = time.time() + wait_sec
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("rmdb exited at startup, rc=%s, see %s"
                                   % (self.proc.returncode, self.log_path))
            try:
                Client(timeout=2).close()
                return self
            except OSError:
                time.sleep(0.3)
        raise RuntimeError("rmdb did not accept connections within %ss" % wait_sec)

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        kill_all_rmdb()

    def kill9(self):
        """Simulate a crash (durability tests)."""
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    def restart_keep_data(self, wait_sec=60):
        """Restart on the same db dir (recovery path)."""
        self.fresh = False
        return self.start(wait_sec=wait_sec)


def kill_all_rmdb():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.4)
