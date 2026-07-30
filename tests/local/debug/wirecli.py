"""Minimal RMDB Wire Protocol v3 client for local repro of OJ functional tests."""
import socket, struct

TAG_EXEC_STREAM = 0x20
TAG_PREPARE_SET = 0x21
TAG_EXEC_BATCH  = 0x22
TAG_META = 0x01
TAG_ROW = 0x02
TAG_COMMAND_OK = 0x10
TAG_RESULT_END = 0x11
TAG_TRANSACTION_ABORT = 0x12
TAG_ERROR = 0x13
TAG_PREPARE_OK = 0x14
TAG_BATCH_RESULT = 0x15

SQL_INT, SQL_FLOAT, SQL_CHAR = 1, 2, 3
TAGNAME = {0x01:'META',0x02:'ROW',0x10:'COMMAND_OK',0x11:'RESULT_END',0x12:'TXN_ABORT',0x13:'ERROR',0x14:'PREPARE_OK',0x15:'BATCH_RESULT'}

class Conn:
    def __init__(self, host='127.0.0.1', port=8765):
        self.s = socket.create_connection((host, port))
        self.s.sendall(b'RMDB\x00\x03\x00\x00')
        echo = self._read_exact(8)
        assert echo[:4] == b'RMDB', echo

    def _read_exact(self, n):
        buf = b''
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise EOFError('connection closed')
            buf += chunk
        return buf

    def send_frame(self, tag, payload, flags=0):
        hdr = struct.pack('>IBBH', len(payload), tag, flags, 0)
        self.s.sendall(hdr + payload)

    def read_frame(self):
        hdr = self._read_exact(8)
        plen, tag, flags, res = struct.unpack('>IBBH', hdr)
        payload = self._read_exact(plen) if plen else b''
        return tag, payload

    # ---- EXEC_STREAM: raw SQL text ----
    def exec_stream(self, sql):
        self.send_frame(TAG_EXEC_STREAM, sql.encode())
        meta, rows = None, []
        while True:
            tag, payload = self.read_frame()
            if tag == TAG_META:
                meta = parse_meta(payload)
            elif tag == TAG_ROW:
                rows.append(parse_row(payload, meta))
            elif tag == TAG_RESULT_END:
                (cnt,) = struct.unpack('>Q', payload)
                return ('RESULT_END', cnt, meta, rows)
            elif tag == TAG_COMMAND_OK:
                return ('COMMAND_OK', None, None, None)
            elif tag == TAG_TRANSACTION_ABORT:
                return ('TXN_ABORT', payload.decode('utf8', 'replace'), None, None)
            elif tag == TAG_ERROR:
                return ('ERROR', payload.decode('utf8', 'replace'), None, None)
            else:
                raise RuntimeError(f'unexpected tag {tag:#x}')

    # ---- PREPARE_SET ----
    def prepare_set(self, stmts):
        """stmts: list of (id, is_query, [param_types], sql_template)"""
        p = struct.pack('>H', len(stmts))
        for sid, is_query, ptypes, sql in stmts:
            p += struct.pack('>HBH', sid, 1 if is_query else 0, len(ptypes))
            p += bytes(ptypes)
            b = sql.encode()
            p += struct.pack('>I', len(b)) + b
        self.send_frame(TAG_PREPARE_SET, p)
        tag, payload = self.read_frame()
        if tag == TAG_ERROR:
            return ('ERROR', payload.decode('utf8', 'replace'))
        assert tag == TAG_PREPARE_OK, hex(tag)
        return ('PREPARE_OK', payload)

    # ---- EXEC_BATCH ----
    def exec_batch(self, ops):
        """ops: list of (stmt_id, [params]) where param = ('i',v)|('f',bits_or_float)|('s',str)|None"""
        p = struct.pack('>H', len(ops))
        for sid, params in ops:
            p += struct.pack('>H', sid)
            for prm in params:
                if prm is None:
                    p += b'\x00'
                    continue
                kind, v = prm
                p += b'\x01'
                if kind == 'i':
                    p += struct.pack('>i', v)
                elif kind == 'f':
                    bits = v if isinstance(v, int) else struct.unpack('>I', struct.pack('>f', v))[0]
                    p += struct.pack('>I', bits)
                elif kind == 's':
                    b = v.encode()
                    p += struct.pack('>I', len(b)) + b
        self.send_frame(TAG_EXEC_BATCH, p, flags=0x01)
        tag, payload = self.read_frame()
        assert tag == TAG_BATCH_RESULT, hex(tag)
        return parse_batch_result(payload)

    def close(self):
        self.s.close()


def parse_meta(payload):
    off = 0
    (n,) = struct.unpack_from('>H', payload, off); off += 2
    cols = []
    for _ in range(n):
        (ln,) = struct.unpack_from('>H', payload, off); off += 2
        name = payload[off:off+ln].decode(); off += ln
        t = payload[off]; off += 1
        cols.append((name, t))
    return cols

def parse_row(payload, meta):
    off = 0
    row = []
    for name, t in meta:
        present = payload[off]; off += 1
        if not present:
            row.append(None); continue
        if t == SQL_INT:
            (v,) = struct.unpack_from('>i', payload, off); off += 4
            row.append(v)
        elif t == SQL_FLOAT:
            (bits,) = struct.unpack_from('>I', payload, off); off += 4
            (f,) = struct.unpack('>f', struct.pack('>I', bits))
            row.append(('f', f, bits))
        else:
            (ln,) = struct.unpack_from('>I', payload, off); off += 4
            row.append(payload[off:off+ln].decode('utf8','replace')); off += ln
    return row

def parse_batch_result(payload):
    off = 0
    executed, status, failed_op = struct.unpack_from('>HBH', payload, off); off += 5
    (dlen,) = struct.unpack_from('>I', payload, off); off += 4
    diag = payload[off:off+dlen].decode('utf8','replace'); off += dlen
    (nres,) = struct.unpack_from('>H', payload, off); off += 2
    # rows can't be parsed generically without column meta; return raw tail
    return {'executed': executed, 'status': status, 'failed_op': failed_op,
            'diag': diag, 'n_results': nres, 'tail': payload[off:]}
