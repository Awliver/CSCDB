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

#include "common/aggregate_defs.h"

enum JoinType {
    INNER_JOIN, LEFT_JOIN, RIGHT_JOIN, FULL_JOIN, CROSS_JOIN,
    LEFT_SEMI_JOIN, RIGHT_SEMI_JOIN, LEFT_ANTI_JOIN, RIGHT_ANTI_JOIN
}; // 选择的种类
namespace ast {

struct QueryExpr;
struct SelectStmt;

enum SvType {
    SV_TYPE_INT, SV_TYPE_FLOAT, SV_TYPE_STRING, SV_TYPE_BOOL
};

enum SvCompOp {
    SV_OP_EQ, SV_OP_NE, SV_OP_LT, SV_OP_GT, SV_OP_LE, SV_OP_GE, SV_OP_LIKE
};

enum class LogicalOp {
    AND,
    OR,
};

enum OrderByDir {
    OrderBy_DEFAULT,
    OrderBy_ASC,
    OrderBy_DESC
};

enum SetKnobType {
    EnableNestLoop, EnableSortMerge
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

// A condition clause is represented by one BoolExpr root. Parentheses are
// reflected by the shape of this tree and therefore need no dedicated node.
struct BoolExpr : public Expr {
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
    std::string alias;       // 题10：AS 别名

    Col(std::string tab_name_, std::string col_name_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)) {}
};

// 语法层聚合表达式
struct AggExpr : public Expr {
    AggType agg_type;
    std::shared_ptr<Col> col;
    std::string alias;
    bool is_star;
    bool distinct;    // 支持 COUNT(DISTINCT col) / COUNT(DISTINCT (col))
    AggExpr(AggType t, std::shared_ptr<Col> c, std::string a, bool star = false, bool dist = false)
        : agg_type(t), col(std::move(c)), alias(std::move(a)), is_star(star), distinct(dist) {}

    std::string to_string() const {
        return format_aggregate_call(agg_type, col ? col->col_name : "", is_star, distinct);
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

// A comparison remains the atom at the leaves of a boolean expression tree.
struct BinaryExpr : public BoolExpr {
    std::shared_ptr<Col> lhs;
    std::shared_ptr<AggExpr> lhs_agg;  // HAVING 以结构化形式保留聚合表达式
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    BinaryExpr(std::shared_ptr<Col> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
    BinaryExpr(std::shared_ptr<AggExpr> lhs_agg_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs_agg(std::move(lhs_agg_)), op(op_), rhs(std::move(rhs_)) {}
};

struct LogicalExpr : public BoolExpr {
    LogicalOp op;
    std::shared_ptr<BoolExpr> left;
    std::shared_ptr<BoolExpr> right;

    LogicalExpr(LogicalOp op_, std::shared_ptr<BoolExpr> left_,
                std::shared_ptr<BoolExpr> right_)
        : op(op_), left(std::move(left_)), right(std::move(right_)) {}
};

struct NotExpr : public BoolExpr {
    std::shared_ptr<BoolExpr> child;

    explicit NotExpr(std::shared_ptr<BoolExpr> child_) : child(std::move(child_)) {}
};

// EXISTS and IN(subquery) remain boolean-expression leaves.  Keeping the
// subquery on the leaf (instead of rewriting it into a JOIN) preserves the
// surrounding OR/NOT topology and therefore the original SQL semantics.
enum class SubqueryPredicateType {
    EXISTS,
    IN,
};

struct SubqueryPredicate : public BoolExpr {
    SubqueryPredicateType type;
    std::shared_ptr<Col> lhs;  // only populated for IN(subquery)
    std::shared_ptr<QueryExpr> subquery;

    SubqueryPredicate(SubqueryPredicateType type_, std::shared_ptr<Col> lhs_,
                      std::shared_ptr<QueryExpr> subquery_)
        : type(type_), lhs(std::move(lhs_)), subquery(std::move(subquery_)) {}
};

struct OrderBy : public TreeNode
{
    std::shared_ptr<Col> cols;
    int ordinal = 0;
    bool is_ordinal = false;
    OrderByDir orderby_dir;
    OrderBy( std::shared_ptr<Col> cols_, OrderByDir orderby_dir_) :
       cols(std::move(cols_)), orderby_dir(std::move(orderby_dir_)) {}
    OrderBy(int ordinal_, OrderByDir orderby_dir_) :
       ordinal(ordinal_), is_ordinal(true), orderby_dir(std::move(orderby_dir_)) {}
};

// SELECT and set operations share the query-level ORDER BY/LIMIT tail.  Keeping
// it on a common base lets a parenthesized UNION be used anywhere a query is
// accepted without wrapping it in the old, special-case SELECT * shell.
struct QueryExpr : public TreeNode {
    bool has_sort = false;
    std::shared_ptr<OrderBy> order;
    std::vector<std::shared_ptr<OrderBy>> orders;
    int limit = -1;
    bool has_limit = false;
    int limit_count = 0;

    void set_tail(std::vector<std::shared_ptr<OrderBy>> orders_, int limit_) {
        orders = std::move(orders_);
        has_sort = !orders.empty();
        order = orders.empty() ? nullptr : orders.front();
        limit = limit_;
        has_limit = limit_ >= 0;
        limit_count = has_limit ? limit_ : 0;
    }
};

// Retains a parenthesized query boundary so an outer ORDER BY/LIMIT never
// overwrites a tail that belongs to the inner query expression.
struct QueryGroup : public QueryExpr {
    std::shared_ptr<QueryExpr> child;

    explicit QueryGroup(std::shared_ptr<QueryExpr> child_) : child(std::move(child_)) {}
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
    std::shared_ptr<BoolExpr> where_expr;

    DeleteStmt(std::string tab_name_, std::shared_ptr<BoolExpr> where_expr_) :
            tab_name(std::move(tab_name_)), where_expr(std::move(where_expr_)) {}
};

struct UpdateStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<SetClause>> set_clauses;
    std::shared_ptr<BoolExpr> where_expr;

    UpdateStmt(std::string tab_name_,
               std::vector<std::shared_ptr<SetClause>> set_clauses_,
               std::shared_ptr<BoolExpr> where_expr_) :
            tab_name(std::move(tab_name_)), set_clauses(std::move(set_clauses_)),
            where_expr(std::move(where_expr_)) {}
};

struct FromExpr : TreeNode {
    virtual ~FromExpr() = default;
}; // 表示 FROM 后 WHERE 前的表达式

struct TableRef : FromExpr {
    std::string tab_name;
    std::string alias;
    TableRef(std::string tab_name_, std::string alias_) : tab_name(std::move(tab_name_)), alias(std::move(alias_)) {}
};

// An ordinary derived table may contain either a SELECT or an arbitrarily
// parenthesized UNION expression.  SQL requires a correlation name here, but
// the AS keyword itself is optional.
struct DerivedTableRef : FromExpr {
    std::shared_ptr<QueryExpr> subquery;
    std::string alias;

    DerivedTableRef(std::shared_ptr<QueryExpr> subquery_, std::string alias_)
        : subquery(std::move(subquery_)), alias(std::move(alias_)) {}
};

// LATERAL 右侧是一个可以引用左侧关系的派生表。别名是其对外的惟一绑定名，
// 因此 grammar 不允许省略 alias。关联名解析由 Analyzer 在上下文作用域中完成。
struct LateralRef : FromExpr {
    std::shared_ptr<QueryExpr> subquery;
    std::string alias;

    LateralRef(std::shared_ptr<QueryExpr> subquery_, std::string alias_)
        : subquery(std::move(subquery_)), alias(std::move(alias_)) {}
};

struct JoinExpr : FromExpr {
    JoinType type; // 连接类型
    std::shared_ptr<FromExpr> left; // 左表
    std::shared_ptr<FromExpr> right; // 右表
    std::shared_ptr<BoolExpr> on_expr; // ON 布尔表达式；nullptr 表示未写 ON
    // NATURAL 和 LATERAL 都是 JOIN 的正交修饰，不应挤占 JoinType。
    bool natural = false;
    bool lateral = false;
    bool on_true = false;  // 显式 ON TRUE；空 on_expr 本身仍表示“未写 ON”。

    JoinExpr(JoinType type_, std::shared_ptr<FromExpr> left_,
             std::shared_ptr<FromExpr> right_,
             std::shared_ptr<BoolExpr> on_expr_,
             bool natural_ = false, bool lateral_ = false, bool on_true_ = false)
        : type(type_), left(std::move(left_)), right(std::move(right_)),
          on_expr(std::move(on_expr_)), natural(natural_), lateral(lateral_),
          on_true(on_true_) {}
}; // 定义专门的连接表达式结构

struct SelectStmt : public QueryExpr {
    std::vector<std::shared_ptr<Col>> cols;
    std::vector<std::shared_ptr<AggExpr>> aggs;
    // 将 FROM 后的 ON 条件与 WHERE 后的 WHERE 条件区分
    std::shared_ptr<FromExpr> from;
    std::shared_ptr<BoolExpr> where_expr;
    std::vector<std::shared_ptr<Col>> group_by_cols;
    std::shared_ptr<BoolExpr> having_expr;

    SelectStmt(std::vector<std::shared_ptr<Col>> cols_,
               std::vector<std::shared_ptr<AggExpr>> aggs_,
               std::shared_ptr<FromExpr> from_,
               std::shared_ptr<BoolExpr> where_expr_,
               std::vector<std::shared_ptr<Col>> group_by_cols_,
               std::shared_ptr<BoolExpr> having_expr_,
               std::vector<std::shared_ptr<OrderBy>> orders_,
               bool has_limit_, int limit_count_)
        : cols(std::move(cols_)), aggs(std::move(aggs_)), from(std::move(from_)),
          where_expr(std::move(where_expr_)),
          group_by_cols(std::move(group_by_cols_)), having_expr(std::move(having_expr_)) {
        set_tail(std::move(orders_), has_limit_ ? limit_count_ : -1);
    }
};

struct ExplainStmt : public TreeNode {
    std::shared_ptr<QueryExpr> query;
    bool analyze;

    ExplainStmt(std::shared_ptr<QueryExpr> query_, bool analyze_)
        : query(std::move(query_)), analyze(analyze_) {}
};

struct UnionStmt : public QueryExpr {
    std::shared_ptr<QueryExpr> left;
    std::shared_ptr<QueryExpr> right;
    // true is UNION ALL; false is UNION DISTINCT (including bare UNION).
    bool all = false;

    UnionStmt(std::shared_ptr<QueryExpr> left_, std::shared_ptr<QueryExpr> right_, bool all_)
        : left(std::move(left_)), right(std::move(right_)), all(all_) {}
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
    int sv_int = 0;
    float sv_float = 0.0F;
    std::string sv_str;
    bool sv_bool = false;
    OrderByDir sv_orderby_dir = OrderBy_DEFAULT;
    std::vector<std::string> sv_strs;
    std::shared_ptr<FromExpr> sv_from;

    std::shared_ptr<TreeNode> sv_node;
    std::shared_ptr<QueryExpr> sv_query;
    std::shared_ptr<SelectStmt> sv_select;
    std::vector<std::shared_ptr<SelectStmt>> sv_selects;

    SvCompOp sv_comp_op = SV_OP_EQ;

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

    std::shared_ptr<BoolExpr> sv_bool_expr;

    std::shared_ptr<OrderBy> sv_orderby;
    std::vector<std::shared_ptr<OrderBy>> sv_orderbys;

    SetKnobType sv_setKnobType = EnableNestLoop;
};

extern std::shared_ptr<ast::TreeNode> parse_tree;

}

#define YYSTYPE ast::SemValue
