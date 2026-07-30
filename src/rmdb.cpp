/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <chrono>
#include <thread>
#include <readline/history.h>
#include <readline/readline.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <unordered_map>

#include "errors.h"
#include "optimizer/optimizer.h"
#include "recovery/log_recovery.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "portal.h"
#include "analyze/analyze.h"
#include "parser/ast.h"
#include "common/output_control.h"
#include "common/repro_ring.h"
#include "common/wire_protocol.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <malloc.h>

#define SOCK_PORT 8765
#define MAX_CONN_LIMIT 32

static bool should_exit = false;

// 构建全局所需的管理器对象
auto disk_manager = std::make_unique<DiskManager>();
// 池大小 env 覆盖仅供本地诊断（小池强制淘汰压力复现 OJ 数据≫池的条件）；OJ 无此 env 走默认
static size_t effective_pool_size() {
    if (const char *env = std::getenv("RMDB_POOL_FRAMES")) {
        char *end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end != env && v >= 64 && v <= (16L << 20)) return (size_t)v;
    }
    return BUFFER_POOL_SIZE;
}
auto buffer_pool_manager = std::make_unique<BufferPoolManager>(effective_pool_size(), disk_manager.get());
auto rm_manager = std::make_unique<RmManager>(disk_manager.get(), buffer_pool_manager.get());
auto ix_manager = std::make_unique<IxManager>(disk_manager.get(), buffer_pool_manager.get());
auto sm_manager = std::make_unique<SmManager>(disk_manager.get(), buffer_pool_manager.get(), rm_manager.get(), ix_manager.get());
auto lock_manager = std::make_unique<LockManager>();
auto txn_manager = std::make_unique<TransactionManager>(lock_manager.get(), sm_manager.get());
auto planner = std::make_unique<Planner>(sm_manager.get());
auto optimizer = std::make_unique<Optimizer>(sm_manager.get(), planner.get());
auto ql_manager = std::make_unique<QlManager>(sm_manager.get(), txn_manager.get(), nullptr);
auto log_manager = std::make_unique<LogManager>(disk_manager.get());
auto recovery = std::make_unique<RecoveryManager>(disk_manager.get(), buffer_pool_manager.get(), sm_manager.get());
auto portal = std::make_unique<Portal>(sm_manager.get());
auto analyze = std::make_unique<Analyze>(sm_manager.get());
pthread_mutex_t *buffer_mutex;
pthread_mutex_t *sockfd_mutex;

static jmp_buf jmpbuf;
void sigint_handler(int signo) {
    should_exit = true;
    log_manager->flush_log_to_disk();
    std::cout << "The Server receive Crtl+C, will been closed\n";
    longjmp(jmpbuf, 1);
}

// 题9：识别会话级 "SET TRANSACTION ISOLATION LEVEL {SNAPSHOT ISOLATION|SERIALIZABLE}"
// 该语句不进解析器，直接更新会话隔离级别。大小写不敏感。
static bool parse_set_isolation(const char *sql, IsolationLevel *out) {
    while (*sql == ' ' || *sql == '\t' || *sql == '\n' || *sql == '\r') sql++;
    if (strncasecmp(sql, "set", 3) != 0) return false;
    std::string low;
    for (const char *p = sql; *p; ++p) {
        char c = *p;
        low += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    if (low.find("isolation level") == std::string::npos) return false;
    *out = (low.find("snapshot") != std::string::npos) ? IsolationLevel::SNAPSHOT_ISOLATION
                                                        : IsolationLevel::SERIALIZABLE;
    return true;
}

// 判断当前正在执行的是显式事务还是单条SQL语句的事务，并更新事务ID
void SetTransaction(txn_id_t *txn_id, Context *context, IsolationLevel sess_iso) {
    context->txn_ = txn_manager->get_transaction(*txn_id);
    if(context->txn_ == nullptr || context->txn_->get_state() == TransactionState::COMMITTED ||
        context->txn_->get_state() == TransactionState::ABORTED) {
        context->txn_ = txn_manager->begin(nullptr, context->log_mgr_);
        *txn_id = context->txn_->get_transaction_id();
        context->txn_->set_txn_mode(false);
        context->txn_->set_isolation_level(sess_iso);   // 题9：以会话隔离级别开启新事务
    }
}

// 完整写出（容忍短写/EINTR）；失败返回 false
static bool write_all(int fd, const char *buf, int len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        buf += n;
        len -= (int)n;
    }
    return true;
}

/* 常见 DML/事务语句快解析（绕过全局 yacc 锁，失败回退 yacc） */
static const char *fp_skipws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

static bool fp_kw(const char *&p, const char *k) {
    p = fp_skipws(p);
    const char *q = p;
    for (; *k; ++k, ++q)
        if (tolower((unsigned char)*q) != *k) return false;
    p = q;
    return true;
}

static bool fp_ident(const char *&p, std::string &out) {
    p = fp_skipws(p);
    if (!(isalpha((unsigned char)*p) || *p == '_')) return false;
    const char *s = p;
    while (isalnum((unsigned char)*p) || *p == '_') ++p;
    out.assign(s, p - s);
    return true;
}

static bool fp_trailing_ok(const char *&p) {
    p = fp_skipws(p);
    if (*p == ';') { ++p; p = fp_skipws(p); }
    return *p == '\0';
}

static bool fp_parse_number(const char *&p, std::shared_ptr<ast::Value> &val) {
    p = fp_skipws(p);
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    else if (*p == '+') ++p;
    const char *s = p;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) ++p;
    bool is_float = false;
    if (*p == '.') {
        is_float = true;
        ++p;
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) ++p;
    }
    std::string num(s, p - s);
    if (neg) num = "-" + num;
    if (is_float) val = std::make_shared<ast::FloatLit>((float)atof(num.c_str()));
    else val = std::make_shared<ast::IntLit>(atoi(num.c_str()));
    return true;
}

static bool fp_parse_string(const char *&p, std::shared_ptr<ast::Value> &val) {
    p = fp_skipws(p);
    if (*p != '\'') return false;
    ++p;
    const char *s = p;
    while (*p && *p != '\'') ++p;
    if (*p != '\'') return false;
    val = std::make_shared<ast::StringLit>(std::string(s, p - s));
    ++p;
    return true;
}

static bool fp_parse_value(const char *&p, std::shared_ptr<ast::Value> &val) {
    p = fp_skipws(p);
    if (*p == '\'') return fp_parse_string(p, val);
    return fp_parse_number(p, val);
}

static bool fp_parse_uint(const char *&p, int &out) {
    p = fp_skipws(p);
    if (!isdigit((unsigned char)*p)) return false;
    int v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        ++p;
    }
    out = v;
    return true;
}

static bool fp_parse_where_eq(const char *&p, std::vector<std::shared_ptr<ast::BinaryExpr>> &conds) {
    if (!fp_kw(p, "where")) return true;
    while (true) {
        std::string col;
        if (!fp_ident(p, col)) return false;
        if (!fp_kw(p, "=")) return false;
        std::shared_ptr<ast::Value> val;
        if (!fp_parse_value(p, val)) return false;
        auto lhs = std::make_shared<ast::Col>("", col);
        conds.push_back(std::make_shared<ast::BinaryExpr>(lhs, ast::SV_OP_EQ, val));
        p = fp_skipws(p);
        if (fp_kw(p, "and")) continue;
        break;
    }
    return true;
}

static std::shared_ptr<ast::TreeNode> try_fast_parse_txn(const char *s) {
    const char *p = s;
    if (fp_kw(p, "begin") && fp_trailing_ok(p)) return std::make_shared<ast::TxnBegin>();
    p = s;
    if (fp_kw(p, "commit") && fp_trailing_ok(p)) return std::make_shared<ast::TxnCommit>();
    p = s;
    if (fp_kw(p, "abort") && fp_trailing_ok(p)) return std::make_shared<ast::TxnAbort>();
    return nullptr;
}

static std::shared_ptr<ast::TreeNode> try_fast_parse_select(const char *s) {
    const char *p = s;
    if (!fp_kw(p, "select")) return nullptr;

    std::vector<std::shared_ptr<ast::Col>> cols;
    std::vector<std::shared_ptr<ast::AggExpr>> aggs;
    const char *q = fp_skipws(p);
    if (tolower((unsigned char)*q) == 'c' && strncasecmp(q, "count", 5) == 0) {
        p = q + 5;
        p = fp_skipws(p);
        if (*p != '(') return nullptr;
        ++p;
        p = fp_skipws(p);
        if (*p != '*') return nullptr;
        ++p;
        p = fp_skipws(p);
        if (*p != ')') return nullptr;
        ++p;
        aggs.push_back(std::make_shared<ast::AggExpr>(ast::AGG_COUNT, nullptr, "", true));
    } else {
        while (true) {
            std::string col;
            if (!fp_ident(p, col)) return nullptr;
            cols.push_back(std::make_shared<ast::Col>("", col));
            p = fp_skipws(p);
            if (*p == ',') { ++p; continue; }
            break;
        }
    }

    if (!fp_kw(p, "from")) return nullptr;
    std::string tab;
    if (!fp_ident(p, tab)) return nullptr;

    std::vector<std::shared_ptr<ast::BinaryExpr>> conds;
    if (!fp_parse_where_eq(p, conds)) return nullptr;

    std::vector<std::shared_ptr<ast::OrderBy>> orders;
    bool has_limit = false;
    int limit_count = 0;
    if (fp_kw(p, "order")) {
        if (!fp_kw(p, "by")) return nullptr;
        std::string order_col;
        if (!fp_ident(p, order_col)) return nullptr;
        ast::OrderByDir dir = ast::OrderBy_DEFAULT;
        if (fp_kw(p, "asc")) dir = ast::OrderBy_ASC;
        else if (fp_kw(p, "desc")) dir = ast::OrderBy_DESC;
        orders.push_back(std::make_shared<ast::OrderBy>(std::make_shared<ast::Col>("", order_col), dir));
    }
    if (fp_kw(p, "limit")) {
        if (!fp_parse_uint(p, limit_count)) return nullptr;
        has_limit = true;
    }
    if (!fp_trailing_ok(p)) return nullptr;

    return std::make_shared<ast::SelectStmt>(
        cols, aggs, std::vector<std::string>{tab}, conds,
        std::vector<std::shared_ptr<ast::Col>>{}, std::vector<std::shared_ptr<ast::BinaryExpr>>{},
        orders, has_limit, limit_count);
}

static std::shared_ptr<ast::TreeNode> try_fast_parse_update(const char *s) {
    const char *p = s;
    if (!fp_kw(p, "update")) return nullptr;
    std::string tab;
    if (!fp_ident(p, tab)) return nullptr;
    if (!fp_kw(p, "set")) return nullptr;

    std::vector<std::shared_ptr<ast::SetClause>> sets;
    while (true) {
        std::string col;
        if (!fp_ident(p, col)) return nullptr;
        if (!fp_kw(p, "=")) return nullptr;
        std::string rhs_col;
        const char *peek = fp_skipws(p);
        if (isalpha((unsigned char)*peek) || *peek == '_') {
            const char *save = p;
            if (fp_ident(p, rhs_col)) {
                p = fp_skipws(p);
                if (*p == '+' || *p == '-') {
                    bool neg = (*p == '-');
                    ++p;
                    std::shared_ptr<ast::Value> delta;
                    if (!fp_parse_number(p, delta)) return nullptr;
                    sets.push_back(std::make_shared<ast::SetClause>(col, rhs_col, delta, neg));
                    p = fp_skipws(p);
                    if (*p == ',') { ++p; continue; }
                    break;
                }
                if (*p == ',' || *p == ';' || *p == '\0' ||
                    (strncasecmp(p, "where", 5) == 0 && !isalnum((unsigned char)p[5]) && p[5] != '_')) {
                    // 决赛：col = col（自赋值）与 col = 其他列——与 yacc colName '=' colName
                    // 产生式同构（delta-0 表示 + self_copy 标记）。自赋值锁行是决赛词典
                    // 最高频语句，此前无此分支时每条都回退全局 yacc 锁串行解析
                    auto sc = std::make_shared<ast::SetClause>(col, rhs_col,
                                                               std::make_shared<ast::IntLit>(0), false);
                    sc->self_copy = (col == rhs_col);
                    sets.push_back(sc);
                    if (*p == ',') { ++p; continue; }
                    break;
                }
            }
            p = save;
        }
        std::shared_ptr<ast::Value> val;
        if (!fp_parse_value(p, val)) return nullptr;
        sets.push_back(std::make_shared<ast::SetClause>(col, val));
        p = fp_skipws(p);
        if (*p == ',') { ++p; continue; }
        break;
    }

    std::vector<std::shared_ptr<ast::BinaryExpr>> conds;
    if (!fp_parse_where_eq(p, conds)) return nullptr;
    if (!fp_trailing_ok(p)) return nullptr;

    return std::make_shared<ast::UpdateStmt>(tab, sets, conds);
}

static std::shared_ptr<ast::TreeNode> try_fast_parse_delete(const char *s) {
    const char *p = s;
    if (!fp_kw(p, "delete")) return nullptr;
    if (!fp_kw(p, "from")) return nullptr;
    std::string tab;
    if (!fp_ident(p, tab)) return nullptr;
    std::vector<std::shared_ptr<ast::BinaryExpr>> conds;
    if (!fp_parse_where_eq(p, conds)) return nullptr;
    if (!fp_trailing_ok(p)) return nullptr;
    return std::make_shared<ast::DeleteStmt>(tab, conds);
}

static std::shared_ptr<ast::TreeNode> try_fast_parse_insert(const char *s);

static std::shared_ptr<ast::TreeNode> try_fast_parse_sql(const char *s) {
    if (auto t = try_fast_parse_txn(s)) return t;
    if (auto t = try_fast_parse_select(s)) return t;
    if (auto t = try_fast_parse_update(s)) return t;
    if (auto t = try_fast_parse_delete(s)) return t;
    if (auto t = try_fast_parse_insert(s)) return t;
    return nullptr;
}

// 简单单元组 INSERT 的手写快路径解析:仅识别 `insert into <表> values (字面量,...)`，
// 构建与 yacc 完全相同的 AST 交给后续 analyze/optimize/execute；任何偏离都返回 nullptr
// 回退到 flex/bison。词法对齐 lex.l 的基本形式：整数 {sign}?digit+ 用 atoi，浮点
// {sign}?digit+.digit* 用 atof，字符串 '[^']*' 去引号；但不识别 lex.l 额外支持的科学计数法
// 指数后缀——遇到指数会在数字后留下未消费的 'e...'，使尾部校验失败而安全回退到 yacc，不会
// 误解析。绕过 yyparse 削减巨量单行装载的每语句解析开销。
static std::shared_ptr<ast::TreeNode> try_fast_parse_insert(const char *s) {
    const char *p = s;
    auto skipws = [&]() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p; };
    auto kw = [&](const char *k) -> bool {
        skipws();
        const char *q = p;
        for (; *k; ++k, ++q)
            if (tolower((unsigned char)*q) != *k) return false;
        p = q;
        return true;
    };
    if (!kw("insert") || !kw("into")) return nullptr;
    skipws();
    if (!(isalpha((unsigned char)*p) || *p == '_')) return nullptr;
    const char *ts = p;
    while (isalnum((unsigned char)*p) || *p == '_') ++p;
    std::string tab(ts, p - ts);
    skipws();
    if (*p == '(') return nullptr;          // 带列清单形式，回退
    if (!kw("values")) return nullptr;
    skipws();
    if (*p != '(') return nullptr;
    ++p;
    std::vector<std::shared_ptr<ast::Value>> vals;
    while (true) {
        skipws();
        if (*p == '\'') {
            ++p;
            const char *vs = p;
            while (*p && *p != '\'') ++p;
            if (*p != '\'') return nullptr;
            vals.push_back(std::make_shared<ast::StringLit>(std::string(vs, p - vs)));
            ++p;
        } else if (*p == '-' || *p == '+' || isdigit((unsigned char)*p)) {
            const char *vs = p;
            if (*p == '-' || *p == '+') ++p;
            if (!isdigit((unsigned char)*p)) return nullptr;
            while (isdigit((unsigned char)*p)) ++p;
            bool is_float = false;
            if (*p == '.') { is_float = true; ++p; while (isdigit((unsigned char)*p)) ++p; }
            std::string num(vs, p - vs);
            if (is_float) vals.push_back(std::make_shared<ast::FloatLit>((float)atof(num.c_str())));
            else vals.push_back(std::make_shared<ast::IntLit>(atoi(num.c_str())));
        } else {
            return nullptr;                 // NULL、表达式等，回退
        }
        skipws();
        if (*p == ',') { ++p; continue; }
        if (*p == ')') { ++p; break; }
        return nullptr;
    }
    skipws();
    if (*p == ';') { ++p; skipws(); }
    if (*p != '\0') return nullptr;         // 尾部残留，回退
    if (vals.empty()) return nullptr;
    return std::make_shared<ast::InsertStmt>(tab, vals);
}

// load ../../path/file.csv into table_name;
static bool try_parse_load(const char *s, std::string &file_path, std::string &tab_name) {
    const char *p = s;
    auto skipws = [&]() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p; };
    auto kw = [&](const char *k) -> bool {
        skipws();
        const char *q = p;
        for (; *k; ++k, ++q)
            if (tolower((unsigned char)*q) != *k) return false;
        p = q;
        return true;
    };
    if (!kw("load")) return false;
    skipws();
    const char *fs = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') ++p;
    if (p == fs) return false;
    file_path.assign(fs, p - fs);
    if (!kw("into")) return false;
    skipws();
    if (!(isalpha((unsigned char)*p) || *p == '_')) return false;
    const char *ts = p;
    while (isalnum((unsigned char)*p) || *p == '_') ++p;
    tab_name.assign(ts, p - ts);
    skipws();
    if (*p == ';') { ++p; skipws(); }
    return *p == '\0';
}

// ============================================================================
// 决赛 Wire Protocol v3（附件 A）。历史 NUL 协议在 client_handler 中保持原样不变，
// 见其开头的握手探测分支；本节新增代码不改动、不复用历史分支的任何变量或控制流，
// 只共享无副作用的纯函数（try_fast_parse_sql / try_parse_load / parse_set_isolation /
// SetTransaction）。
// ============================================================================

enum class ExecOutcome { OK, ABORT, ERROR };

// 把 sink 的一个 cell 编码为 wire 字节，追加到 buf（present=1：引擎不产生 SQL NULL）
static void wire_put_cell(std::string &buf, const WireCell &c) {
    if (c.is_null) {           // 空集聚合等：present=0，无值字节
        wire::put_u8(buf, 0);
        return;
    }
    wire::put_u8(buf, 1);
    if (c.type == TYPE_INT) {
        wire::put_i32(buf, c.int_val);
    } else if (c.type == TYPE_FLOAT) {
        uint32_t bits;
        float f = c.float_val;
        memcpy(&bits, &f, sizeof(bits));
        wire::put_u32(buf, bits);
    } else {
        wire::put_u32(buf, (uint32_t)c.str_val.size());
        wire::put_bytes(buf, c.str_val.data(), c.str_val.size());
    }
}

// EXEC_STREAM：结果直接流式写 socket（大结果不落地缓冲，呼应附件 A §4）
class StreamSink : public WireResultSink {
public:
    explicit StreamSink(int fd) : fd_(fd) {}

protected:
    void emit_meta(const std::vector<std::pair<std::string, ColType>> &cols) override {
        std::string payload;
        wire::put_u16(payload, (uint16_t)cols.size());
        for (auto &c : cols) wire::put_column_def(payload, c.first, c.second);
        if (!wire::send_frame(fd_, wire::TAG_META, payload)) failed_ = true;
    }
    void emit_row(const std::vector<WireCell> &cells) override {
        std::string payload;
        for (auto &c : cells) wire_put_cell(payload, c);
        if (!wire::send_frame(fd_, wire::TAG_ROW, payload)) failed_ = true;
    }
    void emit_end(uint64_t row_count) override {
        std::string payload;
        wire::put_u64(payload, row_count);
        if (!wire::send_frame(fd_, wire::TAG_RESULT_END, payload)) failed_ = true;
    }

private:
    int fd_;
};

// EXEC_BATCH 内单个 query operation：结果先缓冲进内存，随 BATCH_RESULT 一次性发出
class BufferSink : public WireResultSink {
public:
    uint32_t row_count() const { return row_count_; }
    const std::string &rows_payload() const { return payload_; }

protected:
    void emit_meta(const std::vector<std::pair<std::string, ColType>> &) override {}
    void emit_row(const std::vector<WireCell> &cells) override {
        for (auto &c : cells) wire_put_cell(payload_, c);
        row_count_++;
    }
    void emit_end(uint64_t) override {}

private:
    std::string payload_;
    uint32_t row_count_ = 0;
};

// 压力异常转 ABORT 的 COMMIT 守卫：COMMIT 处理中 WAL 提交记录可能已持久化，
// 此时回 TRANSACTION_ABORT 会诱导驱动重试整个事务（双重生效）。词边界须校验：
// 只匹配独立的 commit 语句（后随空白/分号/串尾）。
static bool sql_is_commit_stmt(const char *s) {
    s = fp_skipws(s);
    if (strncasecmp(s, "commit", 6) != 0) return false;
    const char *p = fp_skipws(s + 6);
    return *p == '\0' || *p == ';';
}

// 单条 SQL 语句的完整生命周期：parse -> analyze -> plan -> portal(start/run) ->
// (autocommit 则 commit) -> reap。与历史 NUL 协议 client_handler 主循环体行为一致
// （包括“RMDBError/std::exception 不主动 abort、仍落入尾部 autocommit”的历史语义），
// 供 EXEC_STREAM 与 EXEC_BATCH 的每个 operation 共用。
static ExecOutcome run_sql_statement(const std::string &sql, txn_id_t *txn_id, IsolationLevel &sess_iso,
                                      WireResultSink *sink, std::string &diag) {
    const auto stmt_start = std::chrono::steady_clock::now();
    std::vector<char> scratch(BUFFER_LENGTH);
    int off = 0;
    Context context_obj(lock_manager.get(), log_manager.get(), nullptr, scratch.data(), &off);
    Context *context = &context_obj;
    context->txn_mgr_ = txn_manager.get();
    context->wire_sink_ = sink;
    SetTransaction(txn_id, context, sess_iso);

    ExecOutcome outcome = ExecOutcome::OK;
    bool finish_analyze = false;
    bool used_yacc = false;
    YY_BUFFER_STATE buf = nullptr;
    std::shared_ptr<ast::TreeNode> fast_tree = try_fast_parse_sql(sql.c_str());

    auto yacc_cleanup_if_needed = [&]() {
        if (used_yacc && !finish_analyze) {
            yy_delete_buffer(buf);
            finish_analyze = true;
            pthread_mutex_unlock(buffer_mutex);
        }
    };

    try {
        if (fast_tree != nullptr) {
            std::shared_ptr<Query> query = analyze->do_analyze(fast_tree);
            finish_analyze = true;
            std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
            std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
            portal->run(portalStmt, ql_manager.get(), txn_id, context);
            portal->drop();
            if (context->txn_->get_txn_mode() &&
                context->txn_->get_state() != TransactionState::COMMITTED &&
                context->txn_->get_state() != TransactionState::ABORTED) {
                txn_manager->release_statement_writes(context->txn_);
            }
        } else {
            // 与历史 NUL 协议一致：buffer_mutex 全程覆盖到 do_analyze 成功返回为止，
            // 期间抛出的异常靠 yacc_cleanup_if_needed() 兜底释放 buffer/mutex。
            used_yacc = true;
            pthread_mutex_lock(buffer_mutex);
            buf = yy_scan_string(sql.c_str());
            if (yyparse() == 0 && ast::parse_tree != nullptr) {
                std::shared_ptr<Query> query = analyze->do_analyze(ast::parse_tree);
                yy_delete_buffer(buf);
                finish_analyze = true;
                pthread_mutex_unlock(buffer_mutex);
                std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
                std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
                portal->run(portalStmt, ql_manager.get(), txn_id, context);
                portal->drop();
                if (context->txn_->get_txn_mode() &&
                    context->txn_->get_state() != TransactionState::COMMITTED &&
                    context->txn_->get_state() != TransactionState::ABORTED) {
                    txn_manager->release_statement_writes(context->txn_);
                }
            } else {
                yy_delete_buffer(buf);
                finish_analyze = true;
                pthread_mutex_unlock(buffer_mutex);
                diag = "parse error";
                outcome = ExecOutcome::ERROR;
            }
        }
    } catch (TransactionAbortException &e) {
        yacc_cleanup_if_needed();
        // abort() 自身可抛（取帧重试耗尽/bad_alloc）；再抛会把 ABORT 升级成断连，
        // 且事务残留待连接收尾兜底。就地吞掉，回 ABORT 语义不变。
        try { txn_manager->abort(context->txn_, log_manager.get()); }
        catch (std::exception &e2) { std::cerr << "[wire] abort failed: " << e2.what() << std::endl; }
        catch (...) { std::cerr << "[wire] abort failed: unknown" << std::endl; }
        diag = e.GetInfo();
        outcome = ExecOutcome::ABORT;
    } catch (BufferPoolPressureError &e) {
        // 瞬时帧耗尽（fetch_page ~2s 放弃）：可重试压力，按写冲突同款处理——回滚后回
        // TRANSACTION_ABORT 让驱动重试。回 ERROR 终结评测一条即判负（07-30 报告）。
        // 例外：COMMIT 语句期间不可当可重试——WAL 提交记录可能已过持久化临界点，
        // 谎报 ABORT 会让驱动重试整个事务造成双重生效，维持原 ERROR 语义。
        yacc_cleanup_if_needed();
        if (sql_is_commit_stmt(sql.c_str())) {
            diag = e.what();
            outcome = ExecOutcome::ERROR;
        } else {
            try { txn_manager->abort(context->txn_, log_manager.get()); }
            catch (std::exception &e2) { std::cerr << "[wire] pressure-abort failed: " << e2.what() << std::endl; }
            catch (...) { std::cerr << "[wire] pressure-abort failed: unknown" << std::endl; }
            diag = e.what();
            outcome = ExecOutcome::ABORT;
            fprintf(stderr, "[pressure-abort] %.200s | sql: %.160s\n", diag.c_str(), sql.c_str());
        }
    } catch (RMDBError &e) {
        yacc_cleanup_if_needed();
        diag = e.what();
        outcome = ExecOutcome::ERROR;
    } catch (std::bad_alloc &e) {
        // RLIMIT_AS 触顶时的分配失败（rmdb.cpp 线程栈/arena 注释记录过测量中段实例）：
        // 同帧耗尽——内存压力可随重试方退避缓解，回 ABORT 而非 ERROR 终结；COMMIT 例外同上
        yacc_cleanup_if_needed();
        if (sql_is_commit_stmt(sql.c_str())) {
            diag = "bad_alloc";
            outcome = ExecOutcome::ERROR;
        } else {
            try { txn_manager->abort(context->txn_, log_manager.get()); }
            catch (std::exception &e2) { std::cerr << "[wire] pressure-abort failed: " << e2.what() << std::endl; }
            catch (...) { std::cerr << "[wire] pressure-abort failed: unknown" << std::endl; }
            diag = "bad_alloc";
            outcome = ExecOutcome::ABORT;
            fprintf(stderr, "[pressure-abort] bad_alloc | sql: %.160s\n", sql.c_str());
        }
    } catch (std::exception &e) {
        yacc_cleanup_if_needed();
        diag = e.what();
        outcome = ExecOutcome::ERROR;
    }
    if (outcome == ExecOutcome::ERROR) {
        // ERROR 是稀有事件（正常负载不产生），必须留下服务器端痕迹：评测端只回报
        // "ERROR terminal" 不透出 diag，没有这行日志线上故障无法归因（历史上为此
        // 盲调多轮）。截断避免刷屏。
        fprintf(stderr, "[sql-error] %.200s | sql: %.160s\n", diag.c_str(), sql.c_str());
    }
    {
        // 慢语句痕迹（>2s）：评测"响应超时"判负时唯一的服务器端归因线索；低频不刷屏
        auto slow_dt = std::chrono::steady_clock::now() - stmt_start;
        long slow_ms = std::chrono::duration_cast<std::chrono::milliseconds>(slow_dt).count();
        if (slow_ms > 2000) {
            fprintf(stderr, "[sql-slow] %ldms outcome=%d | sql: %.160s\n", slow_ms, (int)outcome, sql.c_str());
        }
    }

    // 与历史 NUL 协议一致：非显式事务在此无条件提交/回收——回复即持久。
    // commit/reap 也可能抛异常（缓冲池压力下的页 IO 等），必须捕获转 ERROR，
    // 否则逃出本函数的调用方无 catch → std::terminate → 服务器 SIGABRT
    try {
        if (context->txn_->get_txn_mode() == false) {
            txn_manager->commit(context->txn_, context->log_mgr_);
        }
        txn_manager->reap(context->txn_);
        // LOAD 提交后打检查点（见 Context::checkpoint_after_commit_ 注释）。
        // 必须在 commit 之后：检查点会推进 restart 起点，若数据尚未提交就打点，
        // 崩后 undo 信息落在 restart 之前而数据页已落盘，无从回滚。
        if (context->checkpoint_after_commit_ && outcome == ExecOutcome::OK) {
            context->checkpoint_after_commit_ = false;
            sm_manager->do_checkpoint(context->log_mgr_);
        }
    } catch (std::exception &e) {
        if (outcome == ExecOutcome::OK) {
            diag = e.what();
            outcome = ExecOutcome::ERROR;
        }
    }
    return outcome;
}

// PREPARE_SET 用类型零值探测语句 schema：只 parse/analyze/plan/portal->start，
// 不调用 portal->run()，因此对 INSERT/UPDATE/DELETE 绝不产生真实写入；探测事务
// 全程只读（UPDATE/DELETE 的 rid 预扫描除外，属只读扫描）随后立即结束。
static bool probe_prepare_schema(const std::string &sql, bool is_query,
                                  std::vector<std::pair<std::string, ColType>> &out_cols, std::string &diag,
                                  bool *pressure_retryable = nullptr) {
    txn_id_t probe_txn = INVALID_TXN_ID;
    IsolationLevel probe_iso = IsolationLevel::SERIALIZABLE;
    std::vector<char> scratch(BUFFER_LENGTH);
    int off = 0;
    Context context_obj(lock_manager.get(), log_manager.get(), nullptr, scratch.data(), &off);
    Context *context = &context_obj;
    context->txn_mgr_ = txn_manager.get();
    SetTransaction(&probe_txn, context, probe_iso);

    bool ok = true;
    bool used_yacc = false;
    YY_BUFFER_STATE buf = nullptr;
    try {
        std::shared_ptr<ast::TreeNode> tree = try_fast_parse_sql(sql.c_str());
        if (tree == nullptr) {
            used_yacc = true;
            pthread_mutex_lock(buffer_mutex);
            buf = yy_scan_string(sql.c_str());
            if (yyparse() != 0 || ast::parse_tree == nullptr) {
                yy_delete_buffer(buf);
                pthread_mutex_unlock(buffer_mutex);
                diag = "parse error";
                ok = false;
            } else {
                tree = ast::parse_tree;
            }
        }
        if (ok) {
            std::shared_ptr<Query> query = analyze->do_analyze(tree);
            if (used_yacc) {
                yy_delete_buffer(buf);
                pthread_mutex_unlock(buffer_mutex);
                used_yacc = false;
            }
            std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
            std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
            if (is_query) {
                if (portalStmt->tag != PORTAL_ONE_SELECT || !portalStmt->root) {
                    diag = "statement_id declared query but is not a SELECT";
                    ok = false;
                } else {
                    for (auto &c : portalStmt->root->cols()) out_cols.emplace_back(c.name, c.type);
                }
            } else if (portalStmt->tag == PORTAL_ONE_SELECT) {
                diag = "statement_id declared command but is a SELECT";
                ok = false;
            }
        }
    } catch (BufferPoolPressureError &e) {
        // 探测期撞上瞬时帧耗尽不是语义错误：标记可重试，调用方短暂退避后重探，
        // 避免 PREPARE_SET 回 ERROR 帧被评测当 setup 失败
        if (used_yacc) { yy_delete_buffer(buf); pthread_mutex_unlock(buffer_mutex); }
        diag = e.what();
        if (pressure_retryable != nullptr) *pressure_retryable = true;
        ok = false;
    } catch (std::exception &e) {
        if (used_yacc) { yy_delete_buffer(buf); pthread_mutex_unlock(buffer_mutex); }
        diag = e.what();
        ok = false;
    }

    Transaction *t = txn_manager->get_transaction(probe_txn);
    if (t != nullptr) {
        if (t->get_txn_mode() == false) txn_manager->abort(t, log_manager.get());
        txn_manager->reap(t);
    }
    return ok;
}

// 用给定字面量文本替换 SQL 模板中的 $1..$n；跳过单引号字符串内部的 $n（那只是文本，
// 不是 marker，见附件 A §5）。生成字面量本身经过转义/精度处理（见 wire_param_literal），
// 因此这里的替换等价于 typed bind，不是未转义字符串拼接。
static std::string wire_substitute_params(const std::string &tmpl, const std::vector<std::string> &literals) {
    std::string out;
    out.reserve(tmpl.size() + literals.size() * 4);
    bool in_str = false;
    for (size_t i = 0; i < tmpl.size();) {
        char c = tmpl[i];
        if (in_str) {
            out.push_back(c);
            if (c == '\'') in_str = false;
            i++;
            continue;
        }
        if (c == '\'') { in_str = true; out.push_back(c); i++; continue; }
        if (c == '$' && i + 1 < tmpl.size() && isdigit((unsigned char)tmpl[i + 1])) {
            size_t j = i + 1;
            int num = 0;
            while (j < tmpl.size() && isdigit((unsigned char)tmpl[j])) { num = num * 10 + (tmpl[j] - '0'); j++; }
            if (num >= 1 && (size_t)num <= literals.size()) {
                out += literals[num - 1];
                i = j;
                continue;
            }
        }
        out.push_back(c);
        i++;
    }
    return out;
}

// 从 EXEC_BATCH operation 的 wire 字节流中按声明类型解出一个参数，生成可安全内嵌进
// SQL 文本的字面量。present=0（NULL）时按类型零值兜底：正式 TPC-C 负载不绑定 SQL
// NULL（附件 A §3），此兜底只覆盖功能测试之外的边角。
static std::string wire_param_literal(uint8_t sql_type, wire::Reader &r) {
    uint8_t present = r.u8();
    if (present > 1) throw wire::WireProtocolError("invalid present byte");
    if (present == 0) {
        if (sql_type == wire::SQLTYPE_INT32) return "0";
        if (sql_type == wire::SQLTYPE_FLOAT32) return "0.0";
        return "''";
    }
    if (sql_type == wire::SQLTYPE_INT32) {
        return std::to_string(r.i32());
    } else if (sql_type == wire::SQLTYPE_FLOAT32) {
        uint32_t bits = r.u32();
        float f;
        memcpy(&f, &bits, sizeof(f));
        // 非 finite FLOAT32 参数不能拒绝（OJ Float Precision 实测会以 ±inf/NaN 位模式
        // 作 SELECT 参数探测边界，回 ERROR 直接判负），须映射为可解析字面量正常执行：
        // - NaN → 词法关键字 NAN（lex.l 专门产 VALUE_FLOAT NaN；比较语义在 analyze
        //   层按 IEEE 改写：除 <> 恒真外其余恒假）；
        // - ±inf → 超出 float 域的字面量 ±1e39，atof→double 后收窄回 float 恰得
        //   ±INFINITY（位模式 0x7F800000/0xFF800000，与 IEEE 传输位精确一致），
        //   inf 参与的比较本身 IEEE 良定义，执行器无需特判。
        if (std::isnan(f)) return "NAN";
        if (std::isinf(f)) return f > 0 ? "1e39" : "-1e39";
        char tmp[64];
        // float32 十进制往返所需的有效位数上限为 9；配合词法 {sign}?digit+(\.{digit}*)?([eE]{sign}?{digit}+)?
        // 恒生成含小数点或指数的形式，避免被误判成整数字面量。
        snprintf(tmp, sizeof(tmp), "%.9g", f);
        std::string s(tmp);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
            s += ".0";
        }
        return s;
    } else {
        uint32_t n = r.u32();
        std::string s(r.bytes(n), n);
        if (s.find('\'') != std::string::npos) {
            // 决赛已知限制：当前词法 value_string 不支持转义单引号，无法安全内嵌该
            // 字面量；TPC-C 正式负载（姓名音节表等）不产生此类值，此处主动报错而非
            // 静默截断/注入。
            throw wire::WireProtocolError("CHAR parameter contains ' which cannot be safely embedded");
        }
        return "'" + s + "'";
    }
}

struct WirePreparedStmt {
    std::string sql_template;
    std::vector<uint8_t> param_types;
    bool is_query = false;
    std::vector<std::pair<std::string, ColType>> out_cols;
};

static void handle_prepare_set(int fd, const std::string &payload,
                                std::unordered_map<uint16_t, WirePreparedStmt> &prepared) {
    wire::Reader r(payload.data(), payload.size());
    uint16_t stmt_count = r.u16();
    if (stmt_count < 1 || stmt_count > 256) throw wire::WireProtocolError("statement_count out of range");

    std::vector<uint16_t> ids;
    std::unordered_map<uint16_t, WirePreparedStmt> fresh;
    for (uint16_t i = 0; i < stmt_count; i++) {
        uint16_t id = r.u16();
        if (id == 0) throw wire::WireProtocolError("statement id must be nonzero");
        if (fresh.count(id)) throw wire::WireProtocolError("duplicate statement id in request");
        uint8_t result_kind = r.u8();
        uint16_t param_count = r.u16();
        WirePreparedStmt st;
        st.is_query = (result_kind == 1);
        st.param_types.reserve(param_count);
        for (uint16_t k = 0; k < param_count; k++) st.param_types.push_back(r.u8());
        uint32_t sql_bytes = r.u32();
        st.sql_template = std::string(r.bytes(sql_bytes), sql_bytes);
        fresh[id] = std::move(st);
        ids.push_back(id);
    }
    if (!r.at_end()) throw wire::WireProtocolError("trailing bytes in PREPARE_SET payload");

    std::string resp;
    wire::put_u16(resp, stmt_count);
    for (uint16_t id : ids) {
        WirePreparedStmt &st = fresh[id];
        std::vector<std::string> dummy(st.param_types.size());
        for (size_t k = 0; k < dummy.size(); k++) {
            dummy[k] = (st.param_types[k] == wire::SQLTYPE_INT32) ? "0"
                       : (st.param_types[k] == wire::SQLTYPE_FLOAT32) ? "0.0" : "''";
        }
        std::string probe_sql = wire_substitute_params(st.sql_template, dummy);
        std::vector<std::pair<std::string, ColType>> cols;
        std::string diag;
        bool probe_ok = false;
        for (int attempt = 0; attempt < 3 && !probe_ok; attempt++) {
            bool pressure = false;
            cols.clear();
            diag.clear();
            probe_ok = probe_prepare_schema(probe_sql, st.is_query, cols, diag, &pressure);
            if (!probe_ok && !pressure) break;   // 语义错误立即定论，只有帧压力才值得重探
            if (!probe_ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!probe_ok) {
            wire::send_frame(fd, wire::TAG_ERROR,
                             wire::truncate_diag("PREPARE_SET failed for statement " + std::to_string(id) +
                                                  ": " + diag));
            return;  // 旧字典保持不变（未替换 prepared）
        }
        st.out_cols = cols;
        wire::put_u16(resp, id);
        wire::put_u16(resp, st.is_query ? (uint16_t)cols.size() : 0);
        if (st.is_query) {
            for (auto &c : cols) wire::put_column_def(resp, c.first, c.second);
        }
    }
    if (!wire::send_frame(fd, wire::TAG_PREPARE_OK, resp)) throw wire::WireProtocolError("write failed");
    prepared = std::move(fresh);  // 整字典原子替换：全部探测成功后才落地
}

static void handle_exec_batch(int fd, const std::string &payload,
                               std::unordered_map<uint16_t, WirePreparedStmt> &prepared, txn_id_t *txn_id,
                               IsolationLevel &sess_iso) {
    wire::Reader r(payload.data(), payload.size());
    uint16_t op_count = r.u16();
    if (op_count < 1 || op_count > 256) throw wire::WireProtocolError("operation_count out of range");

    struct OpResult { uint16_t op_index; uint32_t row_count; std::string rows_payload; };
    std::vector<OpResult> results;
    uint16_t executed = 0;
    uint8_t status = wire::BATCH_STATUS_OK;
    uint16_t failed_op = 0xffff;
    std::string diag;

    for (uint16_t op = 0; op < op_count; op++) {
        uint16_t stmt_id;
        std::vector<std::string> literals;
        bool decode_ok = true;
        try {
            stmt_id = r.u16();
            auto it = prepared.find(stmt_id);
            if (it == prepared.end()) throw wire::WireProtocolError("unknown statement id " + std::to_string(stmt_id));
            WirePreparedStmt &st = it->second;
            literals.resize(st.param_types.size());
            for (size_t k = 0; k < st.param_types.size(); k++) literals[k] = wire_param_literal(st.param_types[k], r);
        } catch (std::exception &e) {
            decode_ok = false;
            diag = e.what();
        }
        if (!decode_ok) { status = wire::BATCH_STATUS_ERROR; failed_op = op; break; }

        WirePreparedStmt &st = prepared.at(stmt_id);
        std::string sql = wire_substitute_params(st.sql_template, literals);

        if (st.is_query) {
            BufferSink sink;
            ExecOutcome outc = run_sql_statement(sql, txn_id, sess_iso, &sink, diag);
            if (outc != ExecOutcome::OK) {
                status = (outc == ExecOutcome::ABORT) ? wire::BATCH_STATUS_TRANSACTION_ABORT : wire::BATCH_STATUS_ERROR;
                failed_op = op;
                break;
            }
            results.push_back(OpResult{op, sink.row_count(), sink.rows_payload()});
        } else {
            ExecOutcome outc = run_sql_statement(sql, txn_id, sess_iso, nullptr, diag);
            if (outc != ExecOutcome::OK) {
                status = (outc == ExecOutcome::ABORT) ? wire::BATCH_STATUS_TRANSACTION_ABORT : wire::BATCH_STATUS_ERROR;
                failed_op = op;
                break;
            }
        }
        executed++;
    }

    // AUTO_ABORT：失败且连接存在活动（显式）事务时，必须先完成回滚再回失败响应。
    // abort 抛异常同样不能逃逸（调用方只 catch WireProtocolError → 否则 SIGABRT）
    if (status != wire::BATCH_STATUS_OK) {
        try {
            Transaction *t = txn_manager->get_transaction(*txn_id);
            if (t != nullptr && t->get_txn_mode() && t->get_state() != TransactionState::COMMITTED &&
                t->get_state() != TransactionState::ABORTED) {
                txn_manager->abort(t, log_manager.get());
                txn_manager->reap(t);   // 回滚即回收；不回收则对象滞留 txn_map 直到断连
            }
        } catch (std::exception &e) {
            std::cerr << "[wire] auto-abort failed: " << e.what() << std::endl;
        }
    }

    std::string resp;
    wire::put_u16(resp, executed);
    wire::put_u8(resp, status);
    wire::put_u16(resp, (status == wire::BATCH_STATUS_OK) ? 0xffff : failed_op);
    std::string diag_trunc = wire::truncate_diag(diag);
    wire::put_u32(resp, (uint32_t)diag_trunc.size());
    wire::put_bytes(resp, diag_trunc.data(), diag_trunc.size());
    if (status == wire::BATCH_STATUS_OK) {
        wire::put_u16(resp, (uint16_t)results.size());
        for (auto &rr : results) {
            wire::put_u16(resp, rr.op_index);
            wire::put_u32(resp, rr.row_count);
            wire::put_bytes(resp, rr.rows_payload.data(), rr.rows_payload.size());
        }
    } else {
        wire::put_u16(resp, 0);
    }
    if (resp.size() > wire::MAX_PAYLOAD_BYTES) throw wire::WireProtocolError("BATCH_RESULT exceeds 1 MiB");
    if (!wire::send_frame(fd, wire::TAG_BATCH_RESULT, resp)) throw wire::WireProtocolError("write failed");
}

static void handle_exec_stream(int fd, const std::string &sql, txn_id_t *txn_id, IsolationLevel &sess_iso) {
    IsolationLevel new_iso;
    if (parse_set_isolation(sql.c_str(), &new_iso)) {
        sess_iso = new_iso;
        if (!wire::send_frame(fd, wire::TAG_COMMAND_OK, "")) throw wire::WireProtocolError("write failed");
        return;
    }
    if (strncasecmp(sql.c_str(), "RINGDUMP", 8) == 0) {
        ReproRing::dump(stderr, 6000);
        if (!wire::send_frame(fd, wire::TAG_COMMAND_OK, "")) throw wire::WireProtocolError("write failed");
        return;
    }
    if (strncasecmp(sql.c_str(), "create static_checkpoint", 24) == 0) {
        // 检查点会截断恢复重放起点：先物化延迟删除，否则墓碑仅在内存链上、
        // 堆页带着活槽位落盘，重启后已提交删除的行会复活
        txn_manager->drain_deferred_deletes(true);
        sm_manager->do_checkpoint(log_manager.get());
        if (!wire::send_frame(fd, wire::TAG_COMMAND_OK, "")) throw wire::WireProtocolError("write failed");
        return;
    }
    {
        std::string load_file, load_tab;
        if (try_parse_load(sql.c_str(), load_file, load_tab)) {
            std::vector<char> scratch(BUFFER_LENGTH);
            int off = 0;
            Context context_obj(lock_manager.get(), log_manager.get(), nullptr, scratch.data(), &off);
            Context *context = &context_obj;
            context->txn_mgr_ = txn_manager.get();
            SetTransaction(txn_id, context, sess_iso);
            try {
                ql_manager->run_load(load_file, load_tab, context);
                if (context->txn_->get_txn_mode() == false) txn_manager->commit(context->txn_, context->log_mgr_);
                txn_manager->reap(context->txn_);
                // 大表装载的 CSV 解析产生海量短命分配，glibc 不会主动把保留堆还给 OS；
                // 及时归还，避免 RSS 长期虚高挤占后续 benchmark 的内存余量（OJ 有内存上限）
                malloc_trim(0);
            } catch (std::exception &e) {
                try { txn_manager->abort(context->txn_, log_manager.get()); } catch (...) {}
                wire::send_frame(fd, wire::TAG_ERROR, wire::truncate_diag(e.what()));
                return;
            }
            if (!wire::send_frame(fd, wire::TAG_COMMAND_OK, "")) throw wire::WireProtocolError("write failed");
            return;
        }
    }

    StreamSink sink(fd);
    std::string diag;
    ExecOutcome outcome = run_sql_statement(sql, txn_id, sess_iso, &sink, diag);
    if (sink.failed()) throw wire::WireProtocolError("client write failed mid-result");

    if (outcome == ExecOutcome::ABORT) {
        wire::send_frame(fd, wire::TAG_TRANSACTION_ABORT, wire::truncate_diag(diag));
        return;
    }
    if (outcome == ExecOutcome::ERROR) {
        wire::send_frame(fd, wire::TAG_ERROR, wire::truncate_diag(diag));
        return;
    }
    if (!sink.sent_result()) {
        // 非查询成功：DDL/DML/事务控制/LOAD 等——payload 为空的 COMMAND_OK
        // show tables / desc 已走 META→ROW*→RESULT_END（sent_result=true）
        if (!wire::send_frame(fd, wire::TAG_COMMAND_OK, "")) throw wire::WireProtocolError("write failed");
    }
}

static void handle_wire_connection(int fd) {
    char hs[8];
    if (!wire::read_exact(fd, hs, 8)) return;       // 握手阶段断开
    if (!wire::write_all_bytes(fd, hs, 8)) return;  // 原样回送 8 字节，不做版本校验

    txn_id_t txn_id = INVALID_TXN_ID;
    IsolationLevel sess_iso = IsolationLevel::SERIALIZABLE;
    std::unordered_map<uint16_t, WirePreparedStmt> prepared;

    while (true) {
        wire::FrameHeader fh;
        bool have;
        try {
            have = wire::read_frame_header(fd, fh);
        } catch (wire::WireProtocolError &) {
            break;
        }
        if (!have) break;

        try {
            std::string payload;
            if (!wire::read_payload(fd, fh.payload_bytes, payload)) break;
            switch (fh.tag) {
                case wire::TAG_EXEC_STREAM:
                    if (fh.flags != 0) throw wire::WireProtocolError("EXEC_STREAM flags must be 0");
                    handle_exec_stream(fd, payload, &txn_id, sess_iso);
                    break;
                case wire::TAG_PREPARE_SET:
                    if (fh.flags != 0) throw wire::WireProtocolError("PREPARE_SET flags must be 0");
                    handle_prepare_set(fd, payload, prepared);
                    break;
                case wire::TAG_EXEC_BATCH:
                    if (fh.flags != wire::EXEC_BATCH_FLAG_AUTO_ABORT)
                        throw wire::WireProtocolError("EXEC_BATCH flags must be AUTO_ABORT");
                    handle_exec_batch(fd, payload, prepared, &txn_id, sess_iso);
                    break;
                default:
                    throw wire::WireProtocolError("unknown request tag " + std::to_string((int)fh.tag));
            }
        } catch (wire::WireProtocolError &e) {
            std::cerr << "[wire] protocol error, closing connection: " << e.what() << std::endl;
            wire::send_frame(fd, wire::TAG_ERROR, wire::truncate_diag(e.what()));
            break;
        } catch (std::exception &e) {
            // 兜底：任何逃逸到此的异常（如 commit/abort 内部抛出）只断本连接，绝不
            // 逃到线程函数触发 std::terminate 杀死整个服务器（SIGABRT）
            std::cerr << "[wire] unexpected exception, closing connection: " << e.what() << std::endl;
            wire::send_frame(fd, wire::TAG_ERROR, wire::truncate_diag(e.what()));
            break;
        } catch (...) {
            // 非 std::exception 异常同样不得逃到线程函数（std::terminate → SIGABRT）
            std::cerr << "[wire] unknown exception, closing connection" << std::endl;
            wire::send_frame(fd, wire::TAG_ERROR, "internal error");
            break;
        }
    }

    // 连接收尾回滚必须自带兜底：abort() 内部可抛（缓冲池取帧重试耗尽的 InternalError、
    // 内存上限下的 bad_alloc 等），此处已在连接主 try 之外，逃逸即 std::terminate
    // 杀全进程——其它连接的评测端只会看到"响应中途 EOF"（transport failure）。
    if (txn_id != INVALID_TXN_ID) {
        try {
            Transaction *t = txn_manager->get_transaction(txn_id);
            if (t != nullptr) {
                if (t->get_txn_mode() && t->get_state() != TransactionState::COMMITTED &&
                    t->get_state() != TransactionState::ABORTED) {
                    txn_manager->abort(t, log_manager.get());
                }
                // abort 后必须 reap：只回滚不回收则 txn_map 条目 + Transaction 对象
                // 随每个"带活动事务断开"的连接泄漏（放弃/超时型驱动实测 20 万连接
                // 泄 ~500MB，评测地址空间上限下 bad_alloc）
                txn_manager->reap(t);
            }
        } catch (std::exception &e) {
            std::cerr << "[wire] teardown abort failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[wire] teardown abort failed: unknown exception" << std::endl;
        }
    }
}

void *client_handler(void *sock_fd) {
    // 无人 join 的线程必须 detach，否则线程退出后 8MB 栈虚存永久滞留：评测一次跑
    // 数百个连接（功能史/装载/warmup/逐轮测量各建一批），累计虚存以 GB 计，触顶
    // RLIMIT_AS 后下一个 malloc/pthread_create 失败 → 语句 ERROR / accept 循环崩坏
    pthread_detach(pthread_self());
    int fd = (int)(intptr_t)sock_fd;
    pthread_mutex_unlock(sockfd_mutex);

    // 决赛 Wire Protocol v3：peek 前 4 字节判断是否为新协议握手，不消费字节——
    // 不匹配（历史 NUL 客户端）时下面的历史协议分支会原样重新读到这些字节。
    {
        unsigned char peek4[4];
        ssize_t pn = recv(fd, peek4, 4, MSG_PEEK);
        if (pn == 4 && peek4[0] == 'R' && peek4[1] == 'M' && peek4[2] == 'D' && peek4[3] == 'B') {
            std::cout << "Wire Protocol v3 connection, sockfd: " << fd << std::endl;
            // 线程函数级最终兜底：任何逃逸异常在此转为断连，绝不 std::terminate
            try {
                handle_wire_connection(fd);
            } catch (std::exception &e) {
                std::cerr << "[wire] connection thread escaped exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[wire] connection thread escaped unknown exception" << std::endl;
            }
            std::cout << "Terminating current wire client_connection..." << std::endl;
            close(fd);
            pthread_exit(NULL);
        }
    }

    int i_recvBytes;
    // 接收客户端发送的请求（按 '\0' 分帧：容忍 TCP 半包/粘包；缓冲随语句长度增长）
    std::vector<char> rbuf(BUFFER_LENGTH);
    int recv_len = 0;
    std::vector<char> stmt_buf(BUFFER_LENGTH);
    char *stmt = stmt_buf.data();
    // 需要返回给客户端的结果（RAII：随线程退出自动释放，避免每连接泄漏）
    std::vector<char> data_send_buf(BUFFER_LENGTH);
    char *data_send = data_send_buf.data();
    // 需要返回给客户端的结果的长度
    int offset = 0;
    // 记录客户端当前正在执行的事务ID
    txn_id_t txn_id = INVALID_TXN_ID;
    // 题9：会话级隔离级别（SET TRANSACTION ISOLATION LEVEL 设置，跨语句保持）。
    // 默认必须 SER（框架原始默认）：评测中有依赖默认隔离级别做 SSI 读跟踪的 SER 会话
    // （ser/select_dangerous_structure），改默认 SI 会漏检危险结构(16.80→16.00)，勿改。
    IsolationLevel sess_iso = IsolationLevel::SERIALIZABLE;

    std::string output = "establish client connection, sockfd: " + std::to_string(fd) + "\n";
    std::cout << output;

    while (true) {
        char *nul = (char *)memchr(rbuf.data(), '\0', recv_len);
        while (nul == nullptr) {
            if (recv_len >= (int)rbuf.size()) {
                if (rbuf.size() >= (1u << 24)) {    // 16MB 上限防御
                    recv_len = 0;
                } else {
                    rbuf.resize(rbuf.size() * 2);
                }
            }
            i_recvBytes = read(fd, rbuf.data() + recv_len, rbuf.size() - recv_len);
            if (i_recvBytes <= 0) break;
            recv_len += i_recvBytes;
            nul = (char *)memchr(rbuf.data(), '\0', recv_len);
        }
        if (nul == nullptr) {
            std::cout << "Maybe the client has closed" << std::endl;
            break;
        }
        int consumed = (int)(nul - rbuf.data()) + 1;
        if ((int)stmt_buf.size() < consumed) {
            stmt_buf.resize(consumed);
        }
        stmt = stmt_buf.data();
        memcpy(stmt, rbuf.data(), consumed);
        memmove(rbuf.data(), rbuf.data() + consumed, recv_len - consumed);
        recv_len -= consumed;

        if (strncasecmp(stmt, "exit", 4) == 0) {
            std::cout << "Client exit." << std::endl;
            break;
        }
        if (strncasecmp(stmt, "crash", 5) == 0) {
            std::cout << "Server crash" << std::endl;
            exit(1);
        }
        // 题10：创建静态检查点（先物化延迟删除，理由同 wire 分支）
        if (strncasecmp(stmt, "create static_checkpoint", 24) == 0) {
            txn_manager->drain_deferred_deletes(true);
            sm_manager->do_checkpoint(log_manager.get());
            data_send[0] = '\0';
            if (!write_all(fd, data_send, 1)) break;
            continue;
        }

        // 题9：会话级隔离级别设置——单独处理，不进解析器、不开启事务、无多余输出
        {
            IsolationLevel new_iso;
            if (parse_set_isolation(stmt, &new_iso)) {
                sess_iso = new_iso;
                data_send[0] = '\0';
                if (!write_all(fd, data_send, 1)) break;
                continue;
            }
        }

        // 性能测试：CSV 批量加载（绕过 yacc）
        {
            std::string load_file, load_tab;
            if (try_parse_load(stmt, load_file, load_tab)) {
                data_send[0] = '\0';
                offset = 0;
                Context context_obj(lock_manager.get(), log_manager.get(), nullptr, data_send, &offset);
                Context *context = &context_obj;
                context->txn_mgr_ = txn_manager.get();
                SetTransaction(&txn_id, context, sess_iso);
                try {
                    ql_manager->run_load(load_file, load_tab, context);
                } catch (RMDBError &e) {
                    std::cerr << e.what() << std::endl;
                    memcpy(data_send, e.what(), e.get_msg_len());
                    data_send[e.get_msg_len()] = '\n';
                    data_send[e.get_msg_len() + 1] = '\0';
                    offset = e.get_msg_len() + 1;
                    append_output_file("failure\n");
                    try { txn_manager->abort(context->txn_, log_manager.get()); } catch (...) {}
                    if (!write_all(fd, data_send, offset + 1)) break;
                    continue;
                } catch (std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    memcpy(data_send, "failure\n", 8);
                    data_send[8] = '\0';
                    offset = 8;
                    append_output_file("failure\n");
                    try { txn_manager->abort(context->txn_, log_manager.get()); } catch (...) {}
                    if (!write_all(fd, data_send, offset + 1)) break;
                    continue;
                }
                if (context->txn_->get_txn_mode() == false) {
                    txn_manager->commit(context->txn_, context->log_mgr_);
                }
                if (!write_all(fd, data_send, offset + 1)) break;
                continue;
            }
        }

        data_send[0] = '\0';
        offset = 0;

        // 开启事务，初始化系统所需的上下文信息（包括事务对象指针、锁管理器指针、日志管理器指针、存放结果的buffer、记录结果长度的变量）
        Context context_obj(lock_manager.get(), log_manager.get(), nullptr, data_send, &offset);
        Context *context = &context_obj;
        context->txn_mgr_ = txn_manager.get();          // 题9：执行器经 Context 访问 MVCC 版本存储
        SetTransaction(&txn_id, context, sess_iso);

        // 用于判断是否已经调用了yy_delete_buffer来删除buf
        bool finish_analyze = false;
        std::shared_ptr<ast::TreeNode> fast_tree = try_fast_parse_sql(stmt);
        if (fast_tree != nullptr) {
            try {
                std::shared_ptr<Query> query = analyze->do_analyze(fast_tree);
                finish_analyze = true;
                std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
                std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
                portal->run(portalStmt, ql_manager.get(), &txn_id, context);
                portal->drop();
                if (context->txn_->get_txn_mode() &&
                    context->txn_->get_state() != TransactionState::COMMITTED &&
                    context->txn_->get_state() != TransactionState::ABORTED) {
                    txn_manager->release_statement_writes(context->txn_);
                }
            } catch (TransactionAbortException &e) {
                std::string str = "abort\n";
                memcpy(data_send, str.c_str(), str.length());
                data_send[str.length()] = '\0';
                offset = str.length();
                try { txn_manager->abort(context->txn_, log_manager.get()); } catch (...) {}
                std::cout << e.GetInfo() << std::endl;
                append_output_file(str);
            } catch (RMDBError &e) {
                std::cerr << e.what() << std::endl;
                memcpy(data_send, e.what(), e.get_msg_len());
                data_send[e.get_msg_len()] = '\n';
                data_send[e.get_msg_len() + 1] = '\0';
                offset = e.get_msg_len() + 1;
                append_output_file("failure\n");
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                memcpy(data_send, "failure\n", 8);
                data_send[8] = '\0';
                offset = 8;
                append_output_file("failure\n");
            }
        } else {
        pthread_mutex_lock(buffer_mutex);
        YY_BUFFER_STATE buf = yy_scan_string(stmt);
        if (yyparse() == 0) {
            if (ast::parse_tree != nullptr) {
                try {
                    // analyze and rewrite
                    std::shared_ptr<Query> query = analyze->do_analyze(ast::parse_tree);
                    yy_delete_buffer(buf);
                    finish_analyze = true;
                    pthread_mutex_unlock(buffer_mutex);
                    std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
                    std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
                    portal->run(portalStmt, ql_manager.get(), &txn_id, context);
                    portal->drop();
                    if (context->txn_->get_txn_mode() &&
                        context->txn_->get_state() != TransactionState::COMMITTED &&
                        context->txn_->get_state() != TransactionState::ABORTED) {
                        txn_manager->release_statement_writes(context->txn_);
                    }
                } catch (TransactionAbortException &e) {
                    // 事务需要回滚，需要把abort信息返回给客户端并写入output.txt文件中
                    std::string str = "abort\n";
                    memcpy(data_send, str.c_str(), str.length());
                    data_send[str.length()] = '\0';
                    offset = str.length();

                    // 回滚事务（兜底 catch：此处已在语句 try 的 catch 手柄内，再抛即逃逸线程函数）
                    try { txn_manager->abort(context->txn_, log_manager.get()); } catch (...) {}
                    std::cout << e.GetInfo() << std::endl;

                    append_output_file(str);
                } catch (RMDBError &e) {
                    // 遇到异常，需要打印failure到output.txt文件中，并发异常信息返回给客户端
                    std::cerr << e.what() << std::endl;

                    memcpy(data_send, e.what(), e.get_msg_len());
                    data_send[e.get_msg_len()] = '\n';
                    data_send[e.get_msg_len() + 1] = '\0';
                    offset = e.get_msg_len() + 1;

                    append_output_file("failure\n");
                } catch (std::exception &e) {
                    // 题10：任何未预期异常不得终止服务进程
                    if (!finish_analyze) {
                        yy_delete_buffer(buf);
                        finish_analyze = true;
                        pthread_mutex_unlock(buffer_mutex);
                    }
                    std::cerr << e.what() << std::endl;
                    memcpy(data_send, "failure\n", 8);
                    data_send[8] = '\0';
                    offset = 8;

                    append_output_file("failure\n");
                }
            }
        } else {
            yy_delete_buffer(buf);
            finish_analyze = true;
            pthread_mutex_unlock(buffer_mutex);
            append_output_file("failure\n");
        }
        if(finish_analyze == false) {
            yy_delete_buffer(buf);
            pthread_mutex_unlock(buffer_mutex);
        }
        }  // end yacc path
        // 如果是单挑语句，需要按照一个完整的事务来执行，所以执行完当前语句后，自动提交事务。
        // 必须先提交（WAL 落盘）再回复客户端：回复即持久，否则 ack 后崩溃会丢已确认语句
        if(context->txn_->get_txn_mode() == false)
        {
            txn_manager->commit(context->txn_, context->log_mgr_);
        }
        // future TODO: 格式化 sql_handler.result, 传给客户端
        // send result with fixed format, use protobuf in the future
        data_send[offset] = '\0';
        if (!write_all(fd, data_send, offset + 1)) {
            txn_manager->reap(context->txn_);
            break;
        }
        txn_manager->reap(context->txn_);
    }

    // 客户端断开：回滚未结束的显式事务并 unlock_all，避免行锁泄漏。
    // 兜底 catch 理由同 wire 收尾：此处异常逃逸即 std::terminate 杀全进程。
    if (txn_id != INVALID_TXN_ID) {
        try {
            Transaction *t = txn_manager->get_transaction(txn_id);
            if (t != nullptr) {
                if (t->get_txn_mode() &&
                    t->get_state() != TransactionState::COMMITTED &&
                    t->get_state() != TransactionState::ABORTED) {
                    txn_manager->abort(t, log_manager.get());
                }
                txn_manager->reap(t);   // 理由同 wire 收尾：只 abort 不 reap 泄 txn 对象
            }
        } catch (std::exception &e) {
            std::cerr << "[nul] teardown abort failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[nul] teardown abort failed: unknown exception" << std::endl;
        }
    }

    // Clear
    std::cout << "Terminating current client_connection..." << std::endl;
    close(fd);           // close a file descriptor.
    pthread_exit(NULL);  // terminate calling thread!
}

void start_server() {
    // init mutex
    buffer_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    sockfd_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(buffer_mutex, nullptr);
    pthread_mutex_init(sockfd_mutex, nullptr);

    int sockfd_server;
    int fd_temp;
    struct sockaddr_in s_addr_in {};

    // 初始化连接
    sockfd_server = socket(AF_INET, SOCK_STREAM, 0);  // ipv4,TCP
    assert(sockfd_server != -1);
    int val = 1;
    setsockopt(sockfd_server, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // before bind(), set the attr of structure sockaddr.
    memset(&s_addr_in, 0, sizeof(s_addr_in));
    s_addr_in.sin_family = AF_INET;
    s_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
    s_addr_in.sin_port = htons(SOCK_PORT);
    fd_temp = bind(sockfd_server, (struct sockaddr *)(&s_addr_in), sizeof(s_addr_in));
    if (fd_temp == -1) {
        std::cout << "Bind error!" << std::endl;
        exit(1);
    }

    fd_temp = listen(sockfd_server, MAX_CONN_LIMIT);
    if (fd_temp == -1) {
        std::cout << "Listen error!" << std::endl;
        exit(1);
    }

    while (!should_exit) {
        std::cout << "Waiting for new connection..." << std::endl;
        pthread_t thread_id;
        struct sockaddr_in s_addr_client {};
        int client_length = sizeof(s_addr_client);

        if (setjmp(jmpbuf)) {
            std::cout << "Break from Server Listen Loop\n";
            break;
        }

        // Block here. Until server accepts a new connection.
        pthread_mutex_lock(sockfd_mutex);
        int sockfd = accept(sockfd_server, (struct sockaddr *)(&s_addr_client), (socklen_t *)(&client_length));
        if (sockfd == -1) {
            std::cout << "Accept error!" << std::endl;
            continue;  // ignore current socket ,continue while loop.
        }
        // 题10：扩大发送缓冲——流水线装载下回复不被客户端及时读取时避免写阻塞死锁
        {
            int sndbuf = 16 << 20;
            setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
            // TCP_NODELAY：wire 响应帧是"8 字节头 + payload"两次小包写，Nagle 会把
            // 第二段扣到对端 delayed-ACK（~40ms）——逐语句往返恒定 +40ms（实测），
            // 关闭 Nagle 后由 send 侧立即发出
            int nodelay = 1;
            setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        }
        
        // 和客户端建立连接，并开启一个线程负责处理客户端请求。
        // 连接线程栈显式 1MB：默认 8MB 是纯虚存浪费（评测 RLIMIT_AS 上限）；
        // 服务器语句处理无深递归（yacc 解析栈在堆上、B+ 树迭代式）。
        pthread_attr_t tattr;
        pthread_attr_init(&tattr);
        pthread_attr_setstacksize(&tattr, 1 << 20);
        int cret = pthread_create(&thread_id, &tattr, &client_handler, (void *)(intptr_t)sockfd);
        pthread_attr_destroy(&tattr);
        if (cret != 0) {
            // 绝不能 break：跳出循环会走 close_db() 关掉所有表文件 fd，而存活连接
            // 线程仍在服务 → 全部语句 EBADF（"进程存活但每条 SQL ERROR"，ulimit 复现
            // 实测）。瞬时资源不足只放弃本连接，退避后继续 accept。
            std::cout << "Create thread fail!" << std::endl;
            close(sockfd);
            pthread_mutex_unlock(sockfd_mutex);   // client_handler 未启动，锁由本方释放
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

    }

    // Clear
    buffer_pool_manager->stop_cleaner();  // close_db 前
    std::cout << " Try to close all client-connection.\n";
    int ret = shutdown(sockfd_server, SHUT_WR);  // shut down the all or part of a full-duplex connection.
    if(ret == -1) { printf("%s\n", strerror(errno)); }
//    assert(ret != -1);
    sm_manager->close_db();
    std::cout << " DB has been closed.\n";
    std::cout << "Server shuts down." << std::endl;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        // 需要指定数据库名称
        std::cerr << "Usage: " << argv[0] << " <database>" << std::endl;
        exit(1);
    }

    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);   // 题10：客户端异常断连时 write 不得终止进程
    try {
        std::cout << "\n"
                     "  _____  __  __ _____  ____  \n"
                     " |  __ \\|  \\/  |  __ \\|  _ \\ \n"
                     " | |__) | \\  / | |  | | |_) |\n"
                     " |  _  /| |\\/| | |  | |  _ < \n"
                     " | | \\ \\| |  | | |__| | |_) |\n"
                     " |_|  \\_\\_|  |_|_____/|____/ \n"
                     "\n"
                     "Welcome to RMDB!\n"
                     "Type 'help;' for help.\n"
                     "\n";
        // glibc 每线程 arena 各占 64MB 虚存、随 malloc 争用渐进新建（上限 8×核数），
        // 评测 RLIMIT_AS 上限下测量中段会被 arena 增长顶爆（bad_alloc → 语句 ERROR）。
        // 上限 2 个 arena：虚存封顶 ~128MB，争用由内部分片锁结构消化
        mallopt(M_ARENA_MAX, 2);

        // 恢复各阶段堆归因（RMDB_MVCC_STATS=1 时打印）：mallinfo2 只覆盖 main arena +
        // 统计口径有限，但足以定位"恢复后基线虚存"的产生阶段
        auto log_heap = [](const char *phase) {
            if (std::getenv("RMDB_MVCC_STATS") == nullptr) return;
            struct mallinfo2 mi = mallinfo2();
            long vsz_kb = 0;
            if (FILE *f = fopen("/proc/self/status", "r")) {
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    if (sscanf(line, "VmSize: %ld kB", &vsz_kb) == 1) break;
                }
                fclose(f);
            }
            fprintf(stderr, "[heap] %-14s live_mb=%.0f free_mb=%.0f arena_mb=%.0f mmap_mb=%.0f vsz_mb=%ld\n",
                    phase, mi.uordblks / 1048576.0, mi.fordblks / 1048576.0,
                    mi.arena / 1048576.0, mi.hblkhd / 1048576.0, vsz_kb / 1024);
        };

        // Database name is passed by args
        std::string db_name = argv[1];
        if (!sm_manager->is_dir(db_name)) {
            // Database not found, create a new one
            sm_manager->create_db(db_name);
        }
        // Open database
        sm_manager->open_db(db_name);
        log_heap("open_db");

        // recovery database
        g_log_manager = log_manager.get();
        recovery->set_log_manager(log_manager.get());
        recovery->analyze();
        log_heap("analyze");
        recovery->redo();
        log_heap("redo");
        recovery->undo();
        malloc_trim(0);   // 恢复期 churn 的空闲堆立即还 OS，压低 benchmark 前的水位基线
        log_heap("undo+rebuild");

        buffer_pool_manager->start_cleaner();  // recovery 后
        txn_manager->start_chain_sweeper();    // MVCC 干净链后台回收（防测量轮内存线性增长）

        // MVCC 常驻内存诊断（RMDB_MVCC_STATS=1）：链/版本/字节 + RSS，5s 一行到 stderr
        if (std::getenv("RMDB_MVCC_STATS") != nullptr) {
            std::thread([] {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    size_t chains = 0, vers = 0, bytes = 0;
                    txn_manager->debug_mvcc_stats(chains, vers, bytes);
                    size_t rw = 0, dk = 0, dd = 0, se = 0;
                    txn_manager->debug_aux_stats(rw, dk, dd, se);
                    long rss_kb = 0, vsz_kb = 0;
                    if (FILE *f = fopen("/proc/self/status", "r")) {
                        char line[256];
                        while (fgets(line, sizeof line, f)) {
                            if (sscanf(line, "VmRSS: %ld kB", &rss_kb) == 1) continue;
                            if (sscanf(line, "VmSize: %ld kB", &vsz_kb) == 1) continue;
                        }
                        fclose(f);
                    }
                    struct mallinfo2 mi = mallinfo2();
                    fprintf(stderr, "[mvcc-stats] chains=%zu vers=%zu data_mb=%.1f rlocks=%zu "
                            "rwrites=%zu delkeys=%zu deferred=%zu ser=%zu "
                            "heap_live_mb=%.0f heap_free_mb=%.0f rss_mb=%ld vsz_mb=%ld\n",
                            chains, vers, bytes / 1048576.0, lock_manager->record_lock_count(),
                            rw, dk, dd, se, mi.uordblks / 1048576.0, mi.fordblks / 1048576.0,
                            rss_kb / 1024, vsz_kb / 1024);
                }
            }).detach();
        }

        // 开启服务端，开始接受客户端连接
        start_server();
    } catch (RMDBError &e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
    return 0;
}
