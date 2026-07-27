/* 决赛 Wire Protocol v3（附件 A）字节级编解码工具。
 * 纯 header-only：不新增编译单元，避免触碰 CMakeLists.txt（比赛规则禁止）。
 * 本文件只做协议字节的编解码与 socket 帮助函数，不涉及 SQL 语义。 */
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "defs.h"

namespace wire {

// ---- frame tag ----
constexpr uint8_t TAG_EXEC_STREAM        = 0x20;  // C -> S
constexpr uint8_t TAG_PREPARE_SET        = 0x21;  // C -> S
constexpr uint8_t TAG_EXEC_BATCH         = 0x22;  // C -> S
constexpr uint8_t TAG_META               = 0x01;  // S -> C
constexpr uint8_t TAG_ROW                = 0x02;  // S -> C
constexpr uint8_t TAG_COMMAND_OK         = 0x10;  // S -> C
constexpr uint8_t TAG_RESULT_END         = 0x11;  // S -> C
constexpr uint8_t TAG_TRANSACTION_ABORT  = 0x12;  // S -> C
constexpr uint8_t TAG_ERROR              = 0x13;  // S -> C
constexpr uint8_t TAG_PREPARE_OK         = 0x14;  // S -> C
constexpr uint8_t TAG_BATCH_RESULT       = 0x15;  // S -> C

constexpr uint8_t EXEC_BATCH_FLAG_AUTO_ABORT = 0x01;

// ---- SQL type tag（wire 上的类型编码，与 ColType 语义一一对应）----
constexpr uint8_t SQLTYPE_INT32  = 0x01;
constexpr uint8_t SQLTYPE_FLOAT32 = 0x02;
constexpr uint8_t SQLTYPE_CHAR   = 0x03;

constexpr uint8_t BATCH_STATUS_OK                = 0;
constexpr uint8_t BATCH_STATUS_TRANSACTION_ABORT = 1;
constexpr uint8_t BATCH_STATUS_ERROR             = 2;

constexpr size_t MAX_PAYLOAD_BYTES = 1u << 20;  // 1 MiB
constexpr size_t MAX_DIAGNOSTIC_BYTES = 64u << 10;  // 64 KiB

struct WireProtocolError : public std::runtime_error {
    explicit WireProtocolError(const std::string &msg) : std::runtime_error(msg) {}
};

inline uint8_t coltype_to_sqltype(ColType t) {
    switch (t) {
        case TYPE_INT: return SQLTYPE_INT32;
        case TYPE_FLOAT: return SQLTYPE_FLOAT32;
        case TYPE_STRING: return SQLTYPE_CHAR;
    }
    return SQLTYPE_CHAR;
}

inline ColType sqltype_to_coltype(uint8_t t) {
    switch (t) {
        case SQLTYPE_INT32: return TYPE_INT;
        case SQLTYPE_FLOAT32: return TYPE_FLOAT;
        case SQLTYPE_CHAR: return TYPE_STRING;
        default: throw WireProtocolError("unknown SQL type tag");
    }
}

// ---- 大端编码：追加写入 std::string buffer ----
inline void put_u8(std::string &buf, uint8_t v) { buf.push_back(static_cast<char>(v)); }
inline void put_u16(std::string &buf, uint16_t v) {
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
    buf.push_back(static_cast<char>(v & 0xff));
}
inline void put_u32(std::string &buf, uint32_t v) {
    for (int i = 3; i >= 0; --i) buf.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}
inline void put_u64(std::string &buf, uint64_t v) {
    for (int i = 7; i >= 0; --i) buf.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}
inline void put_i32(std::string &buf, int32_t v) { put_u32(buf, static_cast<uint32_t>(v)); }
inline void put_bytes(std::string &buf, const char *p, size_t n) { buf.append(p, n); }
inline void put_str(std::string &buf, const std::string &s) { buf.append(s); }

// ---- 大端解码：从固定 buffer 读取，越界抛异常 ----
class Reader {
public:
    Reader(const char *data, size_t len) : data_(data), len_(len) {}
    uint8_t u8() { need(1); return static_cast<uint8_t>(data_[pos_++]); }
    uint16_t u16() {
        need(2);
        uint16_t v = (static_cast<uint8_t>(data_[pos_]) << 8) | static_cast<uint8_t>(data_[pos_ + 1]);
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<uint8_t>(data_[pos_ + i]);
        pos_ += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    const char *bytes(size_t n) {
        need(n);
        const char *p = data_ + pos_;
        pos_ += n;
        return p;
    }
    std::string str(size_t n) { return std::string(bytes(n), n); }
    bool at_end() const { return pos_ == len_; }
    size_t remaining() const { return len_ - pos_; }

private:
    void need(size_t n) const {
        if (pos_ + n > len_) throw WireProtocolError("truncated frame payload");
    }
    const char *data_;
    size_t len_;
    size_t pos_ = 0;
};

// ---- socket 循环读写：容忍短读/短写/EINTR，等价 read_exact/write_all ----
inline bool read_exact(int fd, char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, buf + off, n - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;  // 对端关闭
        off += static_cast<size_t>(r);
    }
    return true;
}

inline bool write_all_bytes(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}

struct FrameHeader {
    uint32_t payload_bytes = 0;
    uint8_t tag = 0;
    uint8_t flags = 0;
};

// 读取 8 字节通用 frame header；连接正常关闭返回 false，格式错误抛异常
inline bool read_frame_header(int fd, FrameHeader &out) {
    char hdr[8];
    if (!read_exact(fd, hdr, 8)) return false;
    Reader r(hdr, 8);
    out.payload_bytes = r.u32();
    out.tag = r.u8();
    out.flags = r.u8();
    uint16_t reserved = r.u16();
    if (reserved != 0) throw WireProtocolError("nonzero reserved field");
    return true;
}

inline bool read_payload(int fd, uint32_t payload_bytes, std::string &out) {
    if (payload_bytes > MAX_PAYLOAD_BYTES) throw WireProtocolError("payload exceeds 1 MiB");
    out.resize(payload_bytes);
    if (payload_bytes == 0) return true;
    return read_exact(fd, &out[0], payload_bytes);
}

// 服务器响应 frame：flags/reserved 必须为 0
inline bool send_frame(int fd, uint8_t tag, const std::string &payload) {
    std::string hdr;
    hdr.reserve(8);
    put_u32(hdr, static_cast<uint32_t>(payload.size()));
    put_u8(hdr, tag);
    put_u8(hdr, 0);
    put_u16(hdr, 0);
    if (!write_all_bytes(fd, hdr.data(), hdr.size())) return false;
    if (!payload.empty() && !write_all_bytes(fd, payload.data(), payload.size())) return false;
    return true;
}

inline std::string truncate_diag(const std::string &s) {
    if (s.size() <= MAX_DIAGNOSTIC_BYTES) return s;
    return s.substr(0, MAX_DIAGNOSTIC_BYTES);
}

inline void put_column_def(std::string &buf, const std::string &name, ColType type) {
    put_u16(buf, static_cast<uint16_t>(name.size()));
    put_bytes(buf, name.data(), name.size());
    put_u8(buf, coltype_to_sqltype(type));
}

}  // namespace wire
