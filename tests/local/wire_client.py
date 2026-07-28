"""决赛 RMDB Wire Protocol v3 客户端（附件 A）。

供本地功能 / 一致性 / TPC-C 脚本复用。
- 功能 / 恢复 / 装载：EXEC_STREAM（query() 格式化为 `| cell |` 兼容旧期望）
- TPC-C 排名路径：PREPARE_SET + EXEC_BATCH（见 tpcc_batch.py）
"""

from __future__ import annotations

import socket
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Sequence, Tuple, Union

HOST = "127.0.0.1"
PORT = 8765

TAG_EXEC_STREAM = 0x20
TAG_PREPARE_SET = 0x21
TAG_EXEC_BATCH = 0x22
TAG_META = 0x01
TAG_ROW = 0x02
TAG_COMMAND_OK = 0x10
TAG_RESULT_END = 0x11
TAG_TRANSACTION_ABORT = 0x12
TAG_ERROR = 0x13
TAG_PREPARE_OK = 0x14
TAG_BATCH_RESULT = 0x15

SQLTYPE_INT32 = 0x01
SQLTYPE_FLOAT32 = 0x02
SQLTYPE_CHAR = 0x03

Cell = Union[int, float, str, None]


class WireError(RuntimeError):
    """协议层或服务端 ERROR 终结。"""


class WireAbort(RuntimeError):
    """服务端 TRANSACTION_ABORT（已回滚，可重试）。"""


@dataclass
class ExecResult:
    ok: bool = True
    aborted: bool = False
    error: bool = False
    diagnostic: str = ""
    columns: List[Tuple[str, int]] = field(default_factory=list)  # (name, sqltype)
    rows: List[List[Cell]] = field(default_factory=list)

    @property
    def is_query(self) -> bool:
        return bool(self.columns) or bool(self.rows)


def read_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("short read %d/%d" % (len(buf), n))
        buf.extend(chunk)
    return bytes(buf)


def send_frame(sock: socket.socket, tag: int, payload: bytes = b"", flags: int = 0) -> None:
    sock.sendall(struct.pack(">IBBH", len(payload), tag, flags, 0) + payload)


def recv_frame(sock: socket.socket) -> Tuple[int, int, bytes]:
    plen, tag, flags, reserved = struct.unpack(">IBBH", read_exact(sock, 8))
    if reserved != 0:
        raise WireError("nonzero reserved=%d" % reserved)
    payload = read_exact(sock, plen) if plen else b""
    return tag, flags, payload


def handshake(sock: socket.socket) -> None:
    hs = b"RMDB" + struct.pack(">HH", 3, 0)
    sock.sendall(hs)
    echo = read_exact(sock, 8)
    if echo != hs:
        raise WireError("handshake mismatch: %r" % (echo,))


def _decode_cell(buf: bytes, off: int, sqltype: int) -> Tuple[Cell, int]:
    if off >= len(buf):
        raise WireError("truncated cell")
    present = buf[off]
    off += 1
    if present == 0:
        return None, off
    if present != 1:
        raise WireError("bad present=%d" % present)
    if sqltype == SQLTYPE_INT32:
        (v,) = struct.unpack_from(">i", buf, off)
        return int(v), off + 4
    if sqltype == SQLTYPE_FLOAT32:
        (bits,) = struct.unpack_from(">I", buf, off)
        (v,) = struct.unpack(">f", struct.pack(">I", bits))
        return float(v), off + 4
    if sqltype == SQLTYPE_CHAR:
        (n,) = struct.unpack_from(">I", buf, off)
        off += 4
        s = buf[off : off + n].decode("utf-8", errors="replace")
        return s, off + n
    raise WireError("unknown sqltype=%d" % sqltype)


def _format_cell(cell: Cell, sqltype: int) -> str:
    if cell is None:
        return "NULL"
    if sqltype == SQLTYPE_FLOAT32 or isinstance(cell, float):
        # 对齐 C++ std::to_string(float) / output.txt：固定 6 位小数
        return "%.6f" % float(cell)
    return str(cell)


def format_table(columns: Sequence[Tuple[str, int]], rows: Sequence[Sequence[Cell]]) -> str:
    """与历史 output.txt 紧凑格式一致：`| col |` / `| val |`。"""
    if not columns and not rows:
        return ""
    lines = []
    if columns:
        lines.append("| " + " | ".join(name for name, _ in columns) + " |")
    for row in rows:
        cells = []
        for i, cell in enumerate(row):
            sqltype = columns[i][1] if i < len(columns) else SQLTYPE_CHAR
            cells.append(_format_cell(cell, sqltype))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + ("\n" if lines else "")


def result_to_text(res: ExecResult) -> str:
    if res.aborted:
        return "abort\n" + (res.diagnostic or "")
    if res.error:
        diag = res.diagnostic or "error"
        if not diag.lower().startswith("error"):
            return "Error: " + diag
        return diag
    if res.columns or res.rows:
        return format_table(res.columns, res.rows)
    return ""


def encode_cell(sqltype: int, value: Cell) -> bytes:
    """Typed bind cell（present=1）；正式 TPC-C 不绑 NULL。"""
    if value is None:
        return bytes([0])
    if sqltype == SQLTYPE_INT32:
        return struct.pack(">Bi", 1, int(value))
    if sqltype == SQLTYPE_FLOAT32:
        return b"\x01" + struct.pack(">f", float(value))
    raw = str(value).encode("utf-8")
    return struct.pack(">BI", 1, len(raw)) + raw


BATCH_STATUS_OK = 0
BATCH_STATUS_ABORT = 1
BATCH_STATUS_ERROR = 2


@dataclass
class PreparedInfo:
    stmt_id: int
    is_query: bool
    param_types: List[int]
    columns: List[Tuple[str, int]] = field(default_factory=list)  # query schema from PREPARE_OK


@dataclass
class BatchResult:
    ok: bool = True
    aborted: bool = False
    error: bool = False
    diagnostic: str = ""
    executed: int = 0
    failed_op: int = 0xFFFF
    # op_index -> rows (only query ops)
    results: dict = field(default_factory=dict)


class WireClient:
    """单连接 Wire v3 客户端；同一连接 outstanding request = 1。"""

    def __init__(self, host: str = HOST, port: int = PORT, timeout: Optional[float] = 120):
        self.sock = socket.socket()
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if timeout is not None and timeout > 0:
            self.sock.settimeout(timeout)
        self.sock.connect((host, port))
        handshake(self.sock)
        self.prepared: dict = {}  # stmt_id -> PreparedInfo

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def prepare_set(self, stmts: Sequence[Tuple[int, bool, Sequence[int], str]]) -> None:
        """安装连接级语句字典。

        stmts: [(stmt_id, is_query, param_sqltypes, sql_template), ...]
        sql_template 使用 $1..$n 占位。
        """
        payload = bytearray()
        payload += struct.pack(">H", len(stmts))
        for stmt_id, is_query, param_types, sql in stmts:
            if stmt_id == 0:
                raise WireError("statement id must be nonzero")
            sql = sql.strip()
            if not sql.endswith(";"):
                sql += ";"
            raw = sql.encode("utf-8")
            payload += struct.pack(">HBH", stmt_id, 1 if is_query else 0, len(param_types))
            for t in param_types:
                payload.append(int(t))
            payload += struct.pack(">I", len(raw))
            payload += raw
        send_frame(self.sock, TAG_PREPARE_SET, bytes(payload))
        tag, _flags, body = recv_frame(self.sock)
        if tag == TAG_ERROR:
            raise WireError(body.decode("utf-8", errors="replace"))
        if tag != TAG_PREPARE_OK:
            raise WireError("expected PREPARE_OK, got 0x%02x" % tag)
        off = 0
        (count,) = struct.unpack_from(">H", body, off)
        off += 2
        fresh = {}
        for i in range(count):
            stmt_id, is_query, param_types, sql = stmts[i]
            sql = sql.strip()
            if not sql.endswith(";"):
                sql += ";"
            (sid,) = struct.unpack_from(">H", body, off)
            off += 2
            (ncol,) = struct.unpack_from(">H", body, off)
            off += 2
            cols = []
            for _ in range(ncol):
                (nlen,) = struct.unpack_from(">H", body, off)
                off += 2
                name = body[off : off + nlen].decode("utf-8", errors="replace")
                off += nlen
                sqltype = body[off]
                off += 1
                cols.append((name, sqltype))
            if sid != stmt_id:
                raise WireError("PREPARE_OK id mismatch")
            fresh[sid] = PreparedInfo(sid, is_query, list(param_types), cols)
        self.prepared = fresh

    def exec_batch(self, ops: Sequence[Tuple[int, Sequence[Cell]]]) -> BatchResult:
        """AUTO_ABORT batch：ops = [(stmt_id, [param values...]), ...]。"""
        payload = bytearray()
        payload += struct.pack(">H", len(ops))
        for stmt_id, params in ops:
            info = self.prepared.get(stmt_id)
            if info is None:
                raise WireError("unknown statement id %d" % stmt_id)
            if len(params) != len(info.param_types):
                raise WireError("param count mismatch for stmt %d" % stmt_id)
            payload += struct.pack(">H", stmt_id)
            for t, v in zip(info.param_types, params):
                payload += encode_cell(t, v)
        send_frame(self.sock, TAG_EXEC_BATCH, bytes(payload), flags=0x01)
        tag, _flags, body = recv_frame(self.sock)
        if tag == TAG_ERROR:
            return BatchResult(ok=False, error=True, diagnostic=body.decode("utf-8", errors="replace"))
        if tag != TAG_BATCH_RESULT:
            raise WireError("expected BATCH_RESULT, got 0x%02x" % tag)

        off = 0
        (executed,) = struct.unpack_from(">H", body, off)
        off += 2
        status = body[off]
        off += 1
        (failed_op,) = struct.unpack_from(">H", body, off)
        off += 2
        (diag_len,) = struct.unpack_from(">I", body, off)
        off += 4
        diag = body[off : off + diag_len].decode("utf-8", errors="replace")
        off += diag_len
        out = BatchResult(
            ok=(status == BATCH_STATUS_OK),
            aborted=(status == BATCH_STATUS_ABORT),
            error=(status == BATCH_STATUS_ERROR),
            diagnostic=diag,
            executed=executed,
            failed_op=failed_op,
        )
        if status != BATCH_STATUS_OK:
            return out
        (result_count,) = struct.unpack_from(">H", body, off)
        off += 2
        for _ in range(result_count):
            (op_index,) = struct.unpack_from(">H", body, off)
            off += 2
            (row_count,) = struct.unpack_from(">I", body, off)
            off += 4
            stmt_id = ops[op_index][0]
            cols = self.prepared[stmt_id].columns
            rows = []
            for _r in range(row_count):
                row = []
                for _, sqltype in cols:
                    cell, off = _decode_cell(body, off, sqltype)
                    row.append(cell)
                rows.append(row)
            out.results[op_index] = rows
        return out

    def exec_stream(self, sql: str) -> ExecResult:
        sql = sql.strip()
        if not sql.endswith(";"):
            sql += ";"
        send_frame(self.sock, TAG_EXEC_STREAM, sql.encode("utf-8"))

        res = ExecResult()
        columns: List[Tuple[str, int]] = []
        while True:
            tag, _flags, payload = recv_frame(self.sock)
            if tag == TAG_META:
                if len(payload) < 2:
                    raise WireError("META too short")
                (ncol,) = struct.unpack_from(">H", payload, 0)
                off = 2
                columns = []
                for _ in range(ncol):
                    if off + 2 > len(payload):
                        raise WireError("META truncated name len")
                    (nlen,) = struct.unpack_from(">H", payload, off)
                    off += 2
                    name = payload[off : off + nlen].decode("utf-8", errors="replace")
                    off += nlen
                    if off >= len(payload):
                        raise WireError("META truncated type")
                    sqltype = payload[off]
                    off += 1
                    columns.append((name, sqltype))
                res.columns = columns
            elif tag == TAG_ROW:
                if not columns:
                    raise WireError("ROW before META")
                off = 0
                row: List[Cell] = []
                for _, sqltype in columns:
                    cell, off = _decode_cell(payload, off, sqltype)
                    row.append(cell)
                res.rows.append(row)
            elif tag == TAG_RESULT_END:
                return res
            elif tag == TAG_COMMAND_OK:
                return res
            elif tag == TAG_TRANSACTION_ABORT:
                res.ok = False
                res.aborted = True
                res.diagnostic = payload.decode("utf-8", errors="replace")
                return res
            elif tag == TAG_ERROR:
                res.ok = False
                res.error = True
                res.diagnostic = payload.decode("utf-8", errors="replace")
                return res
            else:
                raise WireError("unexpected tag 0x%02x" % tag)

    def query(self, sql: str) -> str:
        """兼容旧 RmdbClient.query：返回可被 parse_table_rows 解析的文本。"""
        return result_to_text(self.exec_stream(sql))

    def query_ok(self, sql: str) -> Tuple[bool, str]:
        res = self.exec_stream(sql)
        text = result_to_text(res)
        return (res.ok and not res.aborted and not res.error), text

    def query_rows(self, sql: str) -> Tuple[bool, List[List[Cell]], str]:
        res = self.exec_stream(sql)
        return res.ok and not res.error and not res.aborted, res.rows, result_to_text(res)


def wait_ready(host: str = HOST, port: int = PORT, timeout: float = 30.0) -> None:
    """OJ 同款测活：握手 + show tables; 收到 COMMAND_OK 或 META…RESULT_END。"""
    import time

    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            cli = WireClient(host=host, port=port, timeout=min(5.0, timeout))
            res = cli.exec_stream("show tables;")
            cli.close()
            if res.ok and not res.error and not res.aborted:
                return
            last = res.diagnostic or "show tables not ok"
        except Exception as exc:  # noqa: BLE001 — 探测阶段吞掉，直到超时
            last = exc
            time.sleep(0.15)
    raise RuntimeError("rmdb wire readiness failed within %.1fs: %s" % (timeout, last))
