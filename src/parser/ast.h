/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */
#pragma once

#include <vector>
#include <string>
#include <memory>

enum JoinType {
    INNER_JOIN, LEFT_JOIN, RIGHT_JOIN, FULL_JOIN, CROSS_JOIN
}; // 选择的种类
namespace ast {

enum SvType {
    SV_TYPE_INT, SV_TYPE_FLOAT, SV_TYPE_STRING, SV_TYPE_BOOL
};

enum SvCompOp {
    SV_OP_EQ, SV_OP_NE, SV_OP_LT, SV_OP_GT, SV_OP_LE, SV_OP_GE
};

enum OrderByDir {
    OrderBy_DEFAULT,
    OrderBy_ASC,
    OrderBy_DESC
};

enum SetKnobType {
    EnableNestLoop, EnableSortMerge
};

enum AggType {
    AGG_COUNT, AGG_MAX, AGG_MIN, AGG_SUM, AGG_AVG
};

// Base class for tree nodes
struct TreeNode {
    virtual ~TreeNode() = default;  // enable polymorphism
};

struct Help : public TreeNode {
};

struct ShowTables : public TreeNode {
};

struct ShowIndex : public TreeNode {
    std::string tab_name;
    ShowIndex(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct TxnBegin : public TreeNode {
};

struct TxnCommit : public TreeNode {
};

struct TxnAbort : public TreeNode {
};

struct TxnRollback : public TreeNode {
};

struct TypeLen : public TreeNode {
    SvType type;
    int len;

    TypeLen(SvType type_, int len_) : type(type_), len(len_) {}
};

struct Field : public TreeNode {
};

struct ColDef : public Field {
    std::string col_name;
    std::shared_ptr<TypeLen> type_len;

    ColDef(std::string col_name_, std::shared_ptr<TypeLen> type_len_) :
            col_name(std::move(col_name_)), type_len(std::move(type_len_)) {}
};

struct CreateTable : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Field>> fields;

    CreateTable(std::string tab_name_, std::vector<std::shared_ptr<Field>> fields_) :
            tab_name(std::move(tab_name_)), fields(std::move(fields_)) {}
};

struct DropTable : public TreeNode {
    std::string tab_name;

    DropTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct DescTable : public TreeNode {
    std::string tab_name;

    DescTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct CreateIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    CreateIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct DropIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    DropIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct Expr : public TreeNode {
};

struct Value : public Expr {
};

struct IntLit : public Value {
    int val;

    IntLit(int val_) : val(val_) {}
};

struct FloatLit : public Value {
    float val;

    FloatLit(float val_) : val(val_) {}
};

struct StringLit : public Value {
    std::string val;

    StringLit(std::string val_) : val(std::move(val_)) {}
};

struct BoolLit : public Value {
    bool val;

    BoolLit(bool val_) : val(val_) {}
};

struct Col : public Expr {
    std::string tab_name;
    std::string col_name;
    int agg_type = 0;        // 题10：0=无 1=COUNT 2=MAX 3=MIN 4=SUM（简单聚合快路径）
    std::string alias;       // 题10：AS 别名

    Col(std::string tab_name_, std::string col_name_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)) {}
};

struct AggExpr : public Expr {
    AggType agg_type;
    std::shared_ptr<Col> col;
    std::string alias;
    bool is_star;
    bool distinct;    // 决赛：原生 COUNT(DISTINCT col) / COUNT(DISTINCT (col))
    AggExpr(AggType t, std::shared_ptr<Col> c, std::string a, bool star = false, bool dist = false)
        : agg_type(t), col(std::move(c)), alias(std::move(a)), is_star(star), distinct(dist) {}

    std::string to_string() const {
        std::string name;
        switch (agg_type) {
            case AGG_COUNT: name = "count"; break;
            case AGG_MAX:   name = "max"; break;
            case AGG_MIN:   name = "min"; break;
            case AGG_SUM:   name = "sum"; break;
            case AGG_AVG:   name = "avg"; break;
        }
        name += "(" + (is_star ? std::string("*") : (col ? col->col_name : "")) + ")";
        return name;
    }
};

struct SetClause : public TreeNode {
    std::string col_name;
    std::shared_ptr<Value> val;
    bool is_arith = false;     // 题9：v = v ± 字面量 的算术增量
    std::string rhs_col;       // 算术时右侧列名
    bool arith_neg = false;    // 带空格的减号(v = v - 1)：字面量为正、需取负
    bool self_copy = false;    // 决赛：SET col = col 自赋值（恒等写，须保留冲突/回滚语义）
    std::vector<std::shared_ptr<Value>> chain;  // 决赛：链式算术 v = v ± v1 ± v2 ...（项已带符号，含首项）

    SetClause(std::string col_name_, std::shared_ptr<Value> val_) :
            col_name(std::move(col_name_)), val(std::move(val_)), is_arith(false) {}
    SetClause(std::string col_name_, std::string rhs_col_, std::shared_ptr<Value> val_, bool neg_ = false) :
            col_name(std::move(col_name_)), val(std::move(val_)), is_arith(true), rhs_col(std::move(rhs_col_)), arith_neg(neg_) {}
};

struct BinaryExpr : public TreeNode {
    std::shared_ptr<Col> lhs;
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    BinaryExpr(std::shared_ptr<Col> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct OrderBy : public TreeNode
{
    std::shared_ptr<Col> cols;
    OrderByDir orderby_dir;
    OrderBy( std::shared_ptr<Col> cols_, OrderByDir orderby_dir_) :
       cols(std::move(cols_)), orderby_dir(std::move(orderby_dir_)) {}
};

struct InsertStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Value>> vals;
    std::vector<std::string> cols;   // 题9：可选列清单 insert into t(c1,c2) values(...)，空=按表列序

    InsertStmt(std::string tab_name_, std::vector<std::shared_ptr<Value>> vals_) :
            tab_name(std::move(tab_name_)), vals(std::move(vals_)) {}
    InsertStmt(std::string tab_name_, std::vector<std::string> cols_,
               std::vector<std::shared_ptr<Value>> vals_) :
            tab_name(std::move(tab_name_)), vals(std::move(vals_)), cols(std::move(cols_)) {}
};

struct DeleteStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    DeleteStmt(std::string tab_name_, std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), conds(std::move(conds_)) {}
};

struct UpdateStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<SetClause>> set_clauses;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    UpdateStmt(std::string tab_name_,
               std::vector<std::shared_ptr<SetClause>> set_clauses_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), set_clauses(std::move(set_clauses_)), conds(std::move(conds_)) {}
};

struct FromExpr : TreeNode {
    virtual ~FromExpr() = default;
}; // 表示 FROM 后 WHERE 前的表达式

struct TableRef : FromExpr {
    std::string tab_name;
    std::string alias;
    TableRef(std::string tab_name_, std::string alias_) : tab_name(std::move(tab_name_)), alias(std::move(alias_)) {}
};

struct JoinExpr : FromExpr {
    JoinType type; // 连接类型
    std::shared_ptr<FromExpr> left; // 左表
    std::shared_ptr<FromExpr> right; // 右表
    std::vector<std::shared_ptr<BinaryExpr>> on_conds; // on 条件

    JoinExpr(JoinType type_, std::shared_ptr<FromExpr> left_,
             std::shared_ptr<FromExpr> right_,
             std::vector<std::shared_ptr<BinaryExpr>> on_conds_)
        : type(type_), left(std::move(left_)), right(std::move(right_)),
          on_conds(std::move(on_conds_)) {}
}; // 定义专门的连接表达式结构

struct SelectStmt : public TreeNode {
    std::vector<std::shared_ptr<Col>> cols;
    std::vector<std::shared_ptr<AggExpr>> aggs;
    // 将 FROM 后的 ON 条件与 WHERE 后的 WHERE 条件区分
    std::shared_ptr<FromExpr> from;
    std::vector<std::shared_ptr<BinaryExpr>> where_conds;
    std::vector<std::shared_ptr<Col>> group_by_cols;
    std::vector<std::shared_ptr<BinaryExpr>> having_conds;

    bool has_sort;
    std::shared_ptr<OrderBy> order;                      // 单 ORDER BY（题4/题10 兼容）
    std::vector<std::shared_ptr<OrderBy>> orders;        // 多 ORDER BY（题5/p7）
    int limit = -1;
    bool has_limit = false;
    int limit_count = 0;

    SelectStmt(std::vector<std::shared_ptr<Col>> cols_,
               std::vector<std::shared_ptr<AggExpr>> aggs_,
               std::shared_ptr<FromExpr> from_,
               std::vector<std::shared_ptr<BinaryExpr>> where_conds_,
               std::vector<std::shared_ptr<Col>> group_by_cols_,
               std::vector<std::shared_ptr<BinaryExpr>> having_conds_,
               std::vector<std::shared_ptr<OrderBy>> orders_,
               bool has_limit_, int limit_count_)
        : cols(std::move(cols_)), aggs(std::move(aggs_)), from(std::move(from_)),
          where_conds(std::move(where_conds_)),
          group_by_cols(std::move(group_by_cols_)), having_conds(std::move(having_conds_)),
          orders(std::move(orders_)), has_limit(has_limit_), limit_count(limit_count_) {
        has_sort = !orders.empty();
        if (!orders.empty()) order = orders[0];
    }
};

struct ExplainStmt : public TreeNode {
    std::shared_ptr<SelectStmt> select;
    bool analyze;

    ExplainStmt(std::shared_ptr<SelectStmt> select_, bool analyze_)
        : select(std::move(select_)), analyze(analyze_) {}
};

struct UnionStmt : public TreeNode {
    std::vector<std::shared_ptr<SelectStmt>> selects;
    std::string alias;
    std::vector<std::shared_ptr<OrderBy>> orders;
    bool has_limit;
    int limit_count;

    UnionStmt(std::vector<std::shared_ptr<SelectStmt>> selects_,
              std::string alias_,
              std::vector<std::shared_ptr<OrderBy>> orders_,
              bool has_limit_,
              int limit_count_)
        : selects(std::move(selects_)),
          alias(std::move(alias_)),
          orders(std::move(orders_)),
          has_limit(has_limit_),
          limit_count(limit_count_) {}
};

// set enable_nestloop
struct SetStmt : public TreeNode {
    SetKnobType set_knob_type_;
    bool bool_val_;

    SetStmt(SetKnobType &type, bool bool_value) : 
        set_knob_type_(type), bool_val_(bool_value) { }
};

// Semantic value
struct SemValue {
    int sv_int;
    float sv_float;
    std::string sv_str;
    bool sv_bool;
    OrderByDir sv_orderby_dir;
    std::vector<std::string> sv_strs;
    std::shared_ptr<FromExpr> sv_from;

    std::shared_ptr<TreeNode> sv_node;
    std::shared_ptr<SelectStmt> sv_select;
    std::vector<std::shared_ptr<SelectStmt>> sv_selects;

    SvCompOp sv_comp_op;

    std::shared_ptr<TypeLen> sv_type_len;

    std::shared_ptr<Field> sv_field;
    std::vector<std::shared_ptr<Field>> sv_fields;

    std::shared_ptr<Expr> sv_expr;

    std::shared_ptr<Value> sv_val;
    std::vector<std::shared_ptr<Value>> sv_vals;

    std::shared_ptr<Col> sv_col;
    std::vector<std::shared_ptr<Col>> sv_cols;

    std::shared_ptr<AggExpr> sv_agg_expr;
    std::vector<std::shared_ptr<AggExpr>> sv_agg_exprs;

    std::shared_ptr<SetClause> sv_set_clause;
    std::vector<std::shared_ptr<SetClause>> sv_set_clauses;

    std::shared_ptr<BinaryExpr> sv_cond;
    std::vector<std::shared_ptr<BinaryExpr>> sv_conds;

    std::shared_ptr<OrderBy> sv_orderby;
    std::vector<std::shared_ptr<OrderBy>> sv_orderbys;

    SetKnobType sv_setKnobType;
};

extern std::shared_ptr<ast::TreeNode> parse_tree;

}

#define YYSTYPE ast::SemValue
