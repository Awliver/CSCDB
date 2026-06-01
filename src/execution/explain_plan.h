/* 题4：EXPLAIN ANALYZE —— 优化后计划树的构建、计数执行与格式化
 * 纯头文件，被 execution_manager.cpp 包含（不改 CMakeLists）。
 *
 * 设计：与普通 SELECT 执行解耦。本模块独立构建逻辑计划树（Scan/Filter/Project/Join），
 * 应用谓词下推 + 投影下推、保持 SQL 连接顺序；再用经典嵌套循环（内表按外表行数重扫）
 * 逐节点累计 rows，最后按赛题严格格式输出。
 */
#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "common/common.h"
#include "system/sm.h"
#include "record/rm.h"
#include "analyze/analyze.h"

namespace explain {

enum ExType { EX_SCAN, EX_FILTER, EX_PROJECT, EX_JOIN };

struct ExNode {
    ExType type;
    // SCAN
    std::string table;                 // 真实表名
    // FILTER（单表过滤）/ JOIN（连接条件）
    std::vector<Condition> conds;
    // PROJECT
    std::vector<TabCol> proj_cols;     // 投影列（真实表名）
    bool proj_star = false;            // SELECT *
    // 子节点：FILTER/PROJECT 1 个；JOIN 为 [left, right]（执行顺序）
    std::vector<std::shared_ptr<ExNode>> ch;
    // 该节点输出行的 schema（列含真实表名/类型/在行内偏移），及行字节数
    std::vector<ColMeta> schema;
    int row_size = 0;
    // 该节点下涉及的所有真实表（Join 的 tables=[...] 用）
    std::vector<std::string> tables;
    // 运行时累计行数
    long long rows = 0;
};
using NodePtr = std::shared_ptr<ExNode>;

// ---------- 条件求值（与 SeqScan 同一套字节比较）----------
inline bool ex_compare(const char *a, const char *b, ColType t, int len, CompOp op) {
    int cmp;
    if (t == TYPE_INT) { int x=*(const int*)a, y=*(const int*)b; cmp=(x<y)?-1:(x>y)?1:0; }
    else if (t == TYPE_FLOAT) { float x=*(const float*)a, y=*(const float*)b; cmp=(x<y)?-1:(x>y)?1:0; }
    else cmp = memcmp(a, b, len);
    switch (op) {
        case OP_EQ: return cmp==0; case OP_NE: return cmp!=0;
        case OP_LT: return cmp<0;  case OP_GT: return cmp>0;
        case OP_LE: return cmp<=0; case OP_GE: return cmp>=0;
    }
    return false;
}

// 在 schema 中按 (tab,col) 找列；找不到返回 nullptr
inline const ColMeta* find_col(const std::vector<ColMeta>& schema, const std::string& tab, const std::string& col) {
    for (auto& c : schema) if (c.tab_name==tab && c.name==col) return &c;
    return nullptr;
}

inline bool eval_cond(const Condition& cond, const char* row, const std::vector<ColMeta>& schema) {
    const ColMeta* lc = find_col(schema, cond.lhs_col.tab_name, cond.lhs_col.col_name);
    if (!lc) return false;
    const char* lhs = row + lc->offset;
    if (cond.is_rhs_val) {
        return ex_compare(lhs, cond.rhs_val.raw->data, lc->type, lc->len, cond.op);
    } else {
        const ColMeta* rc = find_col(schema, cond.rhs_col.tab_name, cond.rhs_col.col_name);
        if (!rc) return false;
        return ex_compare(lhs, row + rc->offset, lc->type, lc->len, cond.op);
    }
}

// 条件是否只涉及单个表（且 rhs 为值或同表列）——用于谓词下推判定
inline bool cond_single_table(const Condition& c, const std::string& tab) {
    if (c.lhs_col.tab_name != tab) return false;
    if (!c.is_rhs_val && c.rhs_col.tab_name != tab) return false;
    return true;
}
// 条件涉及的两个表（跨表连接条件）是否都在给定集合内
inline bool cond_tables_in(const Condition& c, const std::set<std::string>& s) {
    if (!s.count(c.lhs_col.tab_name)) return false;
    if (!c.is_rhs_val && !s.count(c.rhs_col.tab_name)) return false;
    return true;
}

// ---------- 计数执行（经典 NLJ：内表按外表行数重扫，逐节点累计 rows）----------
// 返回该节点本次执行产生的所有行（materialized）。node.rows 累加（不在调用间清零）。
inline std::vector<std::vector<char>> ex_execute(ExNode* n, SmManager* sm, Context* ctx) {
    std::vector<std::vector<char>> out;
    if (n->type == EX_SCAN) {
        auto fh = sm->fhs_.at(n->table).get();
        int rs = fh->get_file_hdr().record_size;
        for (RmScan scan(fh); !scan.is_end(); scan.next()) {
            auto rec = fh->get_record(scan.rid(), ctx);
            out.emplace_back(rec->data, rec->data + rs);
        }
        n->rows += (long long)out.size();
    } else if (n->type == EX_FILTER) {
        auto child = ex_execute(n->ch[0].get(), sm, ctx);
        for (auto& row : child) {
            bool ok = true;
            for (auto& c : n->conds) if (!eval_cond(c, row.data(), n->ch[0]->schema)) { ok=false; break; }
            if (ok) out.push_back(std::move(row));
        }
        n->rows += (long long)out.size();
    } else if (n->type == EX_PROJECT) {
        // 计数语义下投影不改变行数，也不真正裁列（保留全行供上层 join 匹配）
        out = ex_execute(n->ch[0].get(), sm, ctx);
        n->rows += (long long)out.size();
    } else { // EX_JOIN
        ExNode* L = n->ch[0].get();
        ExNode* R = n->ch[1].get();
        auto lrows = ex_execute(L, sm, ctx);          // 左子树执行一次
        for (auto& lr : lrows) {
            auto rrows = ex_execute(R, sm, ctx);      // 右子树按左行数重扫（右侧计数累积）
            for (auto& rr : rrows) {
                std::vector<char> comb(L->row_size + R->row_size);
                memcpy(comb.data(), lr.data(), L->row_size);
                memcpy(comb.data() + L->row_size, rr.data(), R->row_size);
                bool ok = true;
                for (auto& c : n->conds) if (!eval_cond(c, comb.data(), n->schema)) { ok=false; break; }
                if (ok) out.push_back(std::move(comb));
            }
        }
        n->rows += (long long)out.size();
    }
    return out;
}

// ---------- 计划树构建 ----------
struct Builder {
    SmManager* sm;
    Query* q;

    // 为表 T 构建 schema（列偏移即表内偏移）
    std::vector<ColMeta> table_schema(const std::string& t, int& size) {
        TabMeta& tm = sm->db_.get_table(t);
        size = 0;
        for (auto& c : tm.cols) size = std::max(size, c.offset + c.len);
        return tm.cols;
    }

    // 表 T 需要保留的列（投影下推用）：join 条件涉及的 T 列 ∪ 最终输出的 T 列
    std::vector<TabCol> needed_cols(const std::string& t) {
        std::vector<TabCol> res;
        auto add = [&](const std::string& col){
            for (auto& r : res) if (r.col_name==col) return;
            res.push_back({.tab_name=t, .col_name=col});
        };
        for (auto& c : q->conds) {           // 仅跨表(join)条件需要保留连接列
            if (c.is_rhs_val) continue;
            if (c.lhs_col.tab_name==t && c.rhs_col.tab_name!=t) add(c.lhs_col.col_name);
            if (c.rhs_col.tab_name==t && c.lhs_col.tab_name!=t) add(c.rhs_col.col_name);
        }
        for (auto& sc : q->cols) if (sc.tab_name==t) add(sc.col_name);
        return res;
    }

    // 单表分支：Scan -> [Filter] -> [Project(下推)]
    NodePtr build_branch(const std::string& t, bool multi_table) {
        auto scan = std::make_shared<ExNode>();
        scan->type = EX_SCAN; scan->table = t; scan->tables = {t};
        scan->schema = table_schema(t, scan->row_size);
        NodePtr top = scan;

        // 谓词下推：把只涉及 T 的 WHERE 条件作为 Filter 放在 Scan 之上
        std::vector<Condition> fconds;
        for (auto& c : q->conds) if (cond_single_table(c, t)) fconds.push_back(c);
        if (!fconds.empty()) {
            auto f = std::make_shared<ExNode>();
            f->type = EX_FILTER; f->conds = fconds; f->tables = {t};
            f->schema = top->schema; f->row_size = top->row_size;
            f->ch = {top};
            top = f;
        }

        // 投影下推：多表 && 非 SELECT* 时，每个表都加 Project 节点——即使该表的列被全部保留
        // 也要显式投影。依据题7 departments 示例：dept_id(连接列)+dept_name(输出列)=全部 2 列，
        // 仍输出 Project(columns=[departments.dept_id, departments.dept_name])。
        // SELECT* 不下推投影（示例2 各表直接 Scan，无 Project）。
        if (multi_table && !q->select_all) {
            auto need = needed_cols(t);
            auto p = std::make_shared<ExNode>();
            p->type = EX_PROJECT; p->proj_cols = need; p->tables = {t};
            p->schema = top->schema; p->row_size = top->row_size;
            p->ch = {top};
            top = p;
        }
        return top;
    }

    // 左深连接树；conds 中的跨表条件按"两表都可用"的最早层级归属
    NodePtr build_tree() {
        bool multi = q->tables.size() > 1;
        NodePtr cur = build_branch(q->tables[0], multi);
        std::set<std::string> joined = {q->tables[0]};
        for (size_t i = 1; i < q->tables.size(); ++i) {
            const std::string& rt = q->tables[i];
            NodePtr right = build_branch(rt, multi);
            auto jn = std::make_shared<ExNode>();
            jn->type = EX_JOIN;
            jn->ch = {cur, right};
            // tables = 左右并集
            jn->tables = cur->tables;
            jn->tables.push_back(rt);
            // schema = 左 schema + 右 schema（右偏移后移）
            jn->schema = cur->schema;
            jn->row_size = cur->row_size;
            for (auto c : right->schema) { c.offset += cur->row_size; jn->schema.push_back(c); }
            jn->row_size += right->row_size;
            // 归属本层的连接条件：涉及 rt 且另一侧在已连接集合内
            std::set<std::string> avail = joined; avail.insert(rt);
            for (auto& c : q->conds) {
                if (c.is_rhs_val) continue;
                bool cross = c.lhs_col.tab_name != c.rhs_col.tab_name;
                if (!cross) continue;
                bool touches_rt = (c.lhs_col.tab_name==rt || c.rhs_col.tab_name==rt);
                if (touches_rt && cond_tables_in(c, avail)) jn->conds.push_back(c);
            }
            cur = jn;
            joined.insert(rt);
        }
        return cur;
    }

    // 顶层 Project
    NodePtr build() {
        NodePtr body = build_tree();
        auto p = std::make_shared<ExNode>();
        p->type = EX_PROJECT;
        p->proj_star = q->select_all;
        p->proj_cols = q->cols;
        p->tables = body->tables;
        p->schema = body->schema; p->row_size = body->row_size;
        p->ch = {body};
        return p;
    }
};

// ---------- 格式化 ----------
inline std::string disp_tab(Query* q, const std::string& real) {
    auto it = q->real2alias.find(real);
    return it != q->real2alias.end() ? it->second : real;
}
inline std::string op_str(CompOp op) {
    switch (op){case OP_EQ:return "=";case OP_NE:return "<>";case OP_LT:return "<";
                case OP_GT:return ">";case OP_LE:return "<=";case OP_GE:return ">=";}
    return "?";
}
// 值显示：与原始 SQL 一致——int 直接输出；float 去掉尾随 0（含被提升的整型字面量）
inline std::string val_str(const Value& v) {
    if (v.type == TYPE_INT) return std::to_string(v.int_val);
    if (v.type == TYPE_FLOAT) {
        std::string s = std::to_string(v.float_val);
        if (s.find('.') != std::string::npos) {
            s.erase(s.find_last_not_of('0') + 1);
            if (!s.empty() && s.back()=='.') s.pop_back();
        }
        return s;
    }
    return "'" + v.str_val + "'";   // 字符串字面量
}
inline std::string cond_str(Query* q, const Condition& c) {
    std::string s = disp_tab(q, c.lhs_col.tab_name) + "." + c.lhs_col.col_name + op_str(c.op);
    if (c.is_rhs_val) s += val_str(c.rhs_val);
    else s += disp_tab(q, c.rhs_col.tab_name) + "." + c.rhs_col.col_name;
    return s;
}
inline std::string col_str(Query* q, const TabCol& tc) {
    return disp_tab(q, tc.tab_name) + "." + tc.col_name;
}

inline void format_node(Query* q, ExNode* n, int depth, std::string& out) {
    std::string ind(depth, '\t');
    if (n->type == EX_SCAN) {
        out += ind + "Scan(table=" + n->table + ", type=SeqScan, rows=" + std::to_string(n->rows) + ")\n";
    } else if (n->type == EX_FILTER) {
        std::vector<std::string> cs;
        for (auto& c : n->conds) cs.push_back(cond_str(q, c));
        std::sort(cs.begin(), cs.end());
        std::string body; for (size_t i=0;i<cs.size();++i){ if(i)body+=", "; body+=cs[i]; }
        out += ind + "Filter(condition=[" + body + "], rows=" + std::to_string(n->rows) + ")\n";
        format_node(q, n->ch[0].get(), depth+1, out);
    } else if (n->type == EX_PROJECT) {
        std::string body;
        if (n->proj_star) body = "*";
        else {
            std::vector<std::string> cs;
            for (auto& tc : n->proj_cols) cs.push_back(col_str(q, tc));
            std::sort(cs.begin(), cs.end());
            for (size_t i=0;i<cs.size();++i){ if(i)body+=", "; body+=cs[i]; }
        }
        out += ind + "Project(columns=[" + body + "], rows=" + std::to_string(n->rows) + ")\n";
        format_node(q, n->ch[0].get(), depth+1, out);
    } else { // EX_JOIN
        std::vector<std::string> tabs = n->tables;
        std::sort(tabs.begin(), tabs.end());
        std::string tbody; for (size_t i=0;i<tabs.size();++i){ if(i)tbody+=", "; tbody+=tabs[i]; }
        std::vector<std::string> cs;
        for (auto& c : n->conds) cs.push_back(cond_str(q, c));
        std::sort(cs.begin(), cs.end());
        std::string cbody; for (size_t i=0;i<cs.size();++i){ if(i)cbody+=", "; cbody+=cs[i]; }
        out += ind + "Join(tables=[" + tbody + "], condition=[" + cbody + "], rows=" + std::to_string(n->rows) + ")\n";
        // 子树按执行顺序：左、右
        format_node(q, n->ch[0].get(), depth+1, out);
        format_node(q, n->ch[1].get(), depth+1, out);
    }
}

// 入口：构建 -> 执行计数 -> 格式化
inline std::string run(Query* q, SmManager* sm, Context* ctx) {
    Builder b{sm, q};
    NodePtr root = b.build();
    ex_execute(root.get(), sm, ctx);
    std::string out;
    format_node(q, root.get(), 0, out);
    return out;
}

} // namespace explain
