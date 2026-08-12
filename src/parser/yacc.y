%{
#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

/* 决赛链式算术 SET col = col ± v ± v ...：'-' 项在此取负成带符号项（IEEE x-y == x+(-y) 精确），
 * 非数值字面量返回 nullptr（动作里转 YYERROR） */
static std::shared_ptr<ast::Value> negate_value(const std::shared_ptr<ast::Value> &v) {
    if (auto i = std::dynamic_pointer_cast<ast::IntLit>(v)) return std::make_shared<ast::IntLit>(-i->val);
    if (auto f = std::dynamic_pointer_cast<ast::FloatLit>(v)) return std::make_shared<ast::FloatLit>(-f->val);
    return nullptr;
}

static std::shared_ptr<ast::AggExpr> make_aggregate_expr(
    const std::string &name, std::shared_ptr<ast::Col> col, std::string alias,
    bool is_star = false, bool distinct = false) {
    ast::AggType type;
    if (!ast::aggregate_type_from_name(name, type)) return nullptr;
    return std::make_shared<ast::AggExpr>(type, std::move(col), std::move(alias),
                                          is_star, distinct);
}

static std::shared_ptr<ast::QueryExpr> append_union_operand(
    std::shared_ptr<ast::QueryExpr> left, bool all,
    std::shared_ptr<ast::QueryExpr> right) {
    return std::make_shared<ast::UnionStmt>(std::move(left), std::move(right), all);
}

static bool is_lateral_ref(const std::shared_ptr<ast::FromExpr> &from) {
    return std::dynamic_pointer_cast<ast::LateralRef>(from) != nullptr;
}

static std::shared_ptr<ast::BoolExpr> make_between_expr(
    const std::shared_ptr<ast::Col> &column,
    const std::shared_ptr<ast::Expr> &lower,
    const std::shared_ptr<ast::Expr> &upper,
    bool negated) {
    auto result = std::make_shared<ast::LogicalExpr>(
        ast::LogicalOp::AND,
        std::make_shared<ast::BinaryExpr>(column, ast::SV_OP_GE, lower),
        std::make_shared<ast::BinaryExpr>(column, ast::SV_OP_LE, upper));
    if (negated) return std::make_shared<ast::NotExpr>(std::move(result));
    return result;
}

static std::shared_ptr<ast::BoolExpr> make_in_list_expr(
    const std::shared_ptr<ast::Col> &column,
    const std::vector<std::shared_ptr<ast::Value>> &values,
    bool negated) {
    std::shared_ptr<ast::BoolExpr> result;
    for (const auto &value : values) {
        auto atom = std::make_shared<ast::BinaryExpr>(
            column, ast::SV_OP_EQ, std::static_pointer_cast<ast::Expr>(value));
        if (result == nullptr) {
            result = std::static_pointer_cast<ast::BoolExpr>(std::move(atom));
        } else {
            result = std::make_shared<ast::LogicalExpr>(ast::LogicalOp::OR,
                                                        std::move(result),
                                                        std::move(atom));
        }
    }
    if (negated) return std::make_shared<ast::NotExpr>(std::move(result));
    return result;
}
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT EXPLAIN ANALYZE INT CHAR FLOAT INDEX AND OR NOT JOIN ON EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE
%token COUNT MAX MIN SUM AVG AS GROUP HAVING LIMIT UNION ALL DISTINCT
%token LIKE BETWEEN EXISTS IN
%token LEFT RIGHT INNER OUTER CROSS FULL NATURAL SEMI ANTI LATERAL
// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_query> query_expression union_expression query_primary
%type <sv_select> select_core
%type <sv_field> field
%type <sv_fields> fieldList
%type <sv_type_len> type
%type <sv_comp_op> op
%type <sv_expr> expr
%type <sv_val> value arith_term
%type <sv_vals> valueList valueRows arith_chain
%type <sv_str> tbName colName
%type <sv_strs> colNameList
%type <sv_from> from_clause joined_table table_ref
%type <sv_col> col
%type <sv_cols> colList
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_bool_expr> condition whereClause where_or_expr where_and_expr where_not_expr
%type <sv_bool_expr> optWhereClause having_condition having_clause
%type <sv_bool_expr> having_or_expr having_and_expr having_not_expr opt_having
%type <sv_orderby>  order_clause
%type <sv_orderbys> opt_order_clause order_list
%type <sv_orderby_dir> opt_asc_desc
%type <sv_agg_expr> agg_func
%type <sv_agg_exprs> agg_list
%type <sv_str> agg_name
%type <sv_cols> opt_group_by group_by_list
%type <sv_int> opt_limit
%type <sv_bool> union_quantifier
%type <sv_str> opt_alias required_alias
%type <sv_setKnobType> set_knob_type

%%
start:
        stmt ';'
    {
        parse_tree = $1;
        YYACCEPT;
    }
    |   HELP
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
    |   EXIT
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    |   T_EOF
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    ;

stmt:
        dbStmt
    |   ddl
    |   dml
    |   txnStmt
    |   setStmt
    ;

txnStmt:
        TXN_BEGIN
    {
        $$ = std::make_shared<TxnBegin>();
    }
    |   TXN_COMMIT
    {
        $$ = std::make_shared<TxnCommit>();
    }
    |   TXN_ABORT
    {
        $$ = std::make_shared<TxnAbort>();
    }
    | TXN_ROLLBACK
    {
        $$ = std::make_shared<TxnRollback>();
    }
    ;

dbStmt:
        SHOW TABLES
    {
        $$ = std::make_shared<ShowTables>();
    }
    |   SHOW INDEX FROM tbName
    {
        $$ = std::make_shared<ShowIndex>($4);
    }
    ;

setStmt:
        SET set_knob_type '=' VALUE_BOOL
    {
        $$ = std::make_shared<SetStmt>($2, $4);
    }
    ;

ddl:
        CREATE TABLE tbName '(' fieldList ')'
    {
        $$ = std::make_shared<CreateTable>($3, $5);
    }
    |   DROP TABLE tbName
    {
        $$ = std::make_shared<DropTable>($3);
    }
    |   DESC tbName
    {
        $$ = std::make_shared<DescTable>($2);
    }
    |   CREATE INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<CreateIndex>($3, $5);
    }
    |   DROP INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<DropIndex>($3, $5);
    }
    ;

dml:
        INSERT INTO tbName VALUES valueRows
    {
        $$ = std::make_shared<InsertStmt>($3, $5);
    }
    |   INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'
    {
        $$ = std::make_shared<InsertStmt>($3, $5, $9);
    }
    |   DELETE FROM tbName optWhereClause
    {
        $$ = std::make_shared<DeleteStmt>($3, $4);
    }
    |   UPDATE tbName SET setClauses optWhereClause
    {
        $$ = std::make_shared<UpdateStmt>($2, $4, $5);
    }
    |   query_expression
    {
        $$ = $1;
    }
    |   EXPLAIN query_expression
    {
        $$ = std::make_shared<ExplainStmt>($2, false);
    }
    |   EXPLAIN ANALYZE query_expression
    {
        $$ = std::make_shared<ExplainStmt>($3, true);
    }
    ;

query_expression:
        union_expression opt_order_clause opt_limit
    {
        $1->set_tail($2, $3);
        $$ = $1;
    }
    ;

union_expression:
        query_primary
    {
        $$ = $1;
    }
    |   union_expression UNION union_quantifier query_primary
    {
        $$ = append_union_operand($1, $3, $4);
    }
    ;

union_quantifier:
        /* UNION defaults to DISTINCT */
    {
        $$ = false;
    }
    |   DISTINCT
    {
        $$ = false;
    }
    |   ALL
    {
        $$ = true;
    }
    ;

query_primary:
        select_core
    {
        $$ = $1;
    }
    |   '(' query_expression ')'
    {
        $$ = std::make_shared<QueryGroup>($2);
    }
    ;

select_core:
        SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
    |   SELECT colList FROM from_clause optWhereClause opt_group_by opt_having
    {
        $$ = std::make_shared<SelectStmt>($2, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
    |   SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, $2, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
    |   SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having
    {
        $$ = std::make_shared<SelectStmt>($2, $4, $6, $7, $8, $9, std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
    ;

fieldList:
        field
    {
        $$ = std::vector<std::shared_ptr<Field>>{$1};
    }
    |   fieldList ',' field
    {
        $$.push_back($3);
    }
    ;

colNameList:
        colName
    {
        $$ = std::vector<std::string>{$1};
    }
    | colNameList ',' colName
    {
        $$.push_back($3);
    }
    ;

field:
        colName type
    {
        $$ = std::make_shared<ColDef>($1, $2);
    }
    ;

type:
        INT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
    |   CHAR '(' VALUE_INT ')'
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, $3);
    }
    |   FLOAT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
    ;

valueList:
        value
    {
        $$ = std::vector<std::shared_ptr<Value>>{$1};
    }
    |   valueList ',' value
    {
        $$.push_back($3);
    }
    ;

valueRows:
        '(' valueList ')'
    {
        $$ = $2;
    }
    |   valueRows ',' '(' valueList ')'
    {
        $$ = $1;
        for (auto &v : $4) $$.push_back(v);
    }
    ;

value:
        VALUE_INT
    {
        $$ = std::make_shared<IntLit>($1);
    }
    |   VALUE_FLOAT
    {
        $$ = std::make_shared<FloatLit>($1);
    }
    |   VALUE_STRING
    {
        $$ = std::make_shared<StringLit>($1);
    }
    |   VALUE_BOOL
    {
        $$ = std::make_shared<BoolLit>($1);
    }
    ;

condition:
        col op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    |   col LIKE value
    {
        $$ = std::make_shared<BinaryExpr>($1, SV_OP_LIKE,
                                          std::static_pointer_cast<Expr>($3));
    }
    |   col NOT LIKE value
    {
        $$ = std::make_shared<NotExpr>(std::make_shared<BinaryExpr>(
            $1, SV_OP_LIKE, std::static_pointer_cast<Expr>($4)));
    }
    |   col BETWEEN expr AND expr
    {
        $$ = make_between_expr($1, $3, $5, false);
    }
    |   col NOT BETWEEN expr AND expr
    {
        $$ = make_between_expr($1, $4, $6, true);
    }
    |   col IN '(' valueList ')'
    {
        $$ = make_in_list_expr($1, $4, false);
    }
    |   col NOT IN '(' valueList ')'
    {
        $$ = make_in_list_expr($1, $5, true);
    }
    |   col IN '(' query_expression ')'
    {
        $$ = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::IN, $1, $4);
    }
    |   col NOT IN '(' query_expression ')'
    {
        $$ = std::make_shared<NotExpr>(std::make_shared<SubqueryPredicate>(
            SubqueryPredicateType::IN, $1, $5));
    }
    |   EXISTS '(' query_expression ')'
    {
        $$ = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::EXISTS,
                                                 nullptr, $3);
    }
    ;

optWhereClause:
        /* epsilon */
    {
        $$ = nullptr;
    }
    |   WHERE whereClause
    {
        $$ = $2;
    }
    ;

whereClause:
        where_or_expr
    {
        $$ = $1;
    }
    ;

where_or_expr:
        where_or_expr OR where_and_expr
    {
        $$ = std::make_shared<LogicalExpr>(LogicalOp::OR, $1, $3);
    }
    |   where_and_expr
    {
        $$ = $1;
    }
    ;

where_and_expr:
        where_and_expr AND where_not_expr
    {
        $$ = std::make_shared<LogicalExpr>(LogicalOp::AND, $1, $3);
    }
    |   where_not_expr
    {
        $$ = $1;
    }
    ;

where_not_expr:
        NOT where_not_expr
    {
        $$ = std::make_shared<NotExpr>($2);
    }
    |   '(' whereClause ')'
    {
        $$ = $2;
    }
    |   condition
    {
        $$ = $1;
    }
    ;

col:
        tbName '.' colName
    {
        $$ = std::make_shared<Col>($1, $3);
    }
    |   colName
    {
        $$ = std::make_shared<Col>("", $1);
    }
    ;

colList:
        col
    {
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   colList ',' col
    {
        $$.push_back($3);
    }
    |   col AS IDENTIFIER
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        $1->alias = $3;
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   colList ',' col AS IDENTIFIER
    {
        $3->alias = $5;
        $$.push_back($3);
    }
    ;

op:
        '='
    {
        $$ = SV_OP_EQ;
    }
    |   '<'
    {
        $$ = SV_OP_LT;
    }
    |   '>'
    {
        $$ = SV_OP_GT;
    }
    |   NEQ
    {
        $$ = SV_OP_NE;
    }
    |   LEQ
    {
        $$ = SV_OP_LE;
    }
    |   GEQ
    {
        $$ = SV_OP_GE;
    }
    ;

expr:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    ;

setClauses:
        setClause
    {
        $$ = std::vector<std::shared_ptr<SetClause>>{$1};
    }
    |   setClauses ',' setClause
    {
        $$.push_back($3);
    }
    ;

setClause:
        colName '=' value
    {
        $$ = std::make_shared<SetClause>($1, $3);
    }
    |   colName '=' colName
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        $$ = std::make_shared<SetClause>($1, $3, std::make_shared<IntLit>(0), false);
        $$->self_copy = ($1 == $3);
    }
    |   colName '=' colName arith_chain
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        $$ = std::make_shared<SetClause>($1, $3, $4[0], false);
        if ($4.size() > 1) $$->chain = $4;
    }
    ;

arith_chain:
        arith_term
    {
        $$ = std::vector<std::shared_ptr<Value>>{$1};
    }
    |   arith_chain arith_term
    {
        $$.push_back($2);
    }
    ;

arith_term:
        value
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        $$ = $1;
    }
    |   '+' value
    {
        $$ = $2;
    }
    |   '-' value
    {
        $$ = negate_value($2);
        if ($$ == nullptr) YYERROR;
    }
    ;



agg_list:
        agg_func
    {
        $$ = std::vector<std::shared_ptr<AggExpr>>{$1};
    }
    |   agg_list ',' agg_func
    {
        $$.push_back($3);
    }
    ;

agg_func:
        agg_name '(' '*' ')' opt_alias
    {
        $$ = make_aggregate_expr($1, nullptr, $5, true);
        if ($$ == nullptr) YYERROR;
    }
    |   agg_name '(' col ')' opt_alias
    {
        $$ = make_aggregate_expr($1, $3, $5);
        if ($$ == nullptr) YYERROR;
    }
    |   agg_name '(' DISTINCT col ')' opt_alias
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        $$ = make_aggregate_expr($1, $4, $6, false, true);
        if ($$ == nullptr) YYERROR;
    }
    |   agg_name '(' DISTINCT '(' col ')' ')' opt_alias
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        $$ = make_aggregate_expr($1, $5, $8, false, true);
        if ($$ == nullptr) YYERROR;
    }
    ;

agg_name:
        COUNT { $$ = "count"; }
    |   MAX   { $$ = "max"; }
    |   MIN   { $$ = "min"; }
    |   SUM   { $$ = "sum"; }
    |   AVG   { $$ = "avg"; }
    |   IDENTIFIER
    {
        if (find_aggregate($1) == nullptr) YYERROR;
        $$ = $1;
    }
    ;

opt_alias:
        /* empty */
    {
        $$ = "";
    }
    |   AS IDENTIFIER
    {
        $$ = $2;
    }
    |   IDENTIFIER
    {
        $$ = $1;
    }
    ;
// 新增 from_clause 用于表示select所选择的表
from_clause:
    joined_table
    {
        $$ = $1;
    }
    ;

// 含表别名的表表示
table_ref:
    tbName opt_alias
    {
        $$ = std::make_shared<TableRef>($1, $2);
    }
    | '(' joined_table ')'
    {
        $$ = $2;
    }
    | '(' query_expression ')' required_alias
    {
        $$ = std::make_shared<DerivedTableRef>($2, $4);
    }
    | LATERAL '(' query_expression ')' required_alias
    {
        $$ = std::make_shared<LateralRef>($3, $5);
    }
    ;

required_alias:
    IDENTIFIER
    {
        $$ = $1;
    }
    | AS IDENTIFIER
    {
        $$ = $2;
    }
    ;

    // joined_table 表示被选择连接的表
joined_table:
    table_ref
    {
        $$ = $1;
    }
    | joined_table JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $3, $5, false, is_lateral_ref($3));
    }
    | joined_table JOIN table_ref ON VALUE_BOOL
    {
        if (!$5) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $3, nullptr,
            false, is_lateral_ref($3), true);
    }
    | joined_table JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $3, nullptr,
            false, is_lateral_ref($3));
    }
    | joined_table INNER JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $4, $6, false, is_lateral_ref($4));
    }
    | joined_table INNER JOIN table_ref ON VALUE_BOOL
    {
        if (!$6) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $4, nullptr,
            false, is_lateral_ref($4), true);
    }
    | joined_table LEFT opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table LEFT opt_outer JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            LEFT_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table RIGHT opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            RIGHT_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table RIGHT opt_outer JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            RIGHT_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table FULL opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            FULL_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table FULL opt_outer JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            FULL_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table CROSS JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $4, nullptr,
            false, is_lateral_ref($4));
    }
    | joined_table NATURAL JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $4, nullptr,
            true, is_lateral_ref($4));
    }
    | joined_table NATURAL INNER JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $5, nullptr,
            true, is_lateral_ref($5));
    }
    | joined_table NATURAL LEFT opt_outer JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_JOIN, $1, $6, nullptr,
            true, is_lateral_ref($6));
    }
    | joined_table NATURAL RIGHT opt_outer JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            RIGHT_JOIN, $1, $6, nullptr,
            true, is_lateral_ref($6));
    }
    | joined_table NATURAL FULL opt_outer JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            FULL_JOIN, $1, $6, nullptr,
            true, is_lateral_ref($6));
    }
    | joined_table SEMI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, $1, $4, $6, false, is_lateral_ref($4));
    }
    | joined_table SEMI JOIN table_ref ON VALUE_BOOL
    {
        if (!$6) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, $1, $4, nullptr,
            false, is_lateral_ref($4), true);
    }
    | joined_table LEFT SEMI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table LEFT SEMI JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table RIGHT SEMI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table RIGHT SEMI JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table ANTI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, $1, $4, $6, false, is_lateral_ref($4));
    }
    | joined_table ANTI JOIN table_ref ON VALUE_BOOL
    {
        if (!$6) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, $1, $4, nullptr,
            false, is_lateral_ref($4), true);
    }
    | joined_table LEFT ANTI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table LEFT ANTI JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table RIGHT ANTI JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, $1, $5, $7, false, is_lateral_ref($5));
    }
    | joined_table RIGHT ANTI JOIN table_ref ON VALUE_BOOL
    {
        if (!$7) YYERROR;
        $$ = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, $1, $5, nullptr,
            false, is_lateral_ref($5), true);
    }
    | joined_table ',' table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $3, nullptr,
            false, is_lateral_ref($3));
    }
    ;

opt_outer:
    /* empty */
    | OUTER
    ;


opt_group_by:
        /* epsilon */
    {
        $$ = {};
    }
    |   GROUP BY group_by_list
    {
        $$ = $3;
    }
    ;

group_by_list:
        col
    {
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   group_by_list ',' col
    {
        $$.push_back($3);
    }
    ;

having_condition:
        col op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    |   agg_func op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    ;

having_clause:
        having_or_expr
    {
        $$ = $1;
    }
    ;

having_or_expr:
        having_or_expr OR having_and_expr
    {
        $$ = std::make_shared<LogicalExpr>(LogicalOp::OR, $1, $3);
    }
    |   having_and_expr
    {
        $$ = $1;
    }
    ;

having_and_expr:
        having_and_expr AND having_not_expr
    {
        $$ = std::make_shared<LogicalExpr>(LogicalOp::AND, $1, $3);
    }
    |   having_not_expr
    {
        $$ = $1;
    }
    ;

having_not_expr:
        NOT having_not_expr
    {
        $$ = std::make_shared<NotExpr>($2);
    }
    |   '(' having_clause ')'
    {
        $$ = $2;
    }
    |   having_condition
    {
        $$ = $1;
    }
    ;

opt_having:
        /* epsilon */
    {
        $$ = nullptr;
    }
    |   HAVING having_clause
    {
        $$ = $2;
    }
    ;

opt_order_clause:
    ORDER BY order_list
    {
        $$ = $3;
    }
    |   /* epsilon */
    {
        $$ = {};
    }
    ;

order_list:
      order_clause
    {
        $$ = std::vector<std::shared_ptr<OrderBy>>{$1};
    }
    |   order_list ',' order_clause
    {
        $$.push_back($3);
    }
    ;

order_clause:
      col  opt_asc_desc
    {
        $$ = std::make_shared<OrderBy>($1, $2);
    }
    | VALUE_INT opt_asc_desc
    {
        $$ = std::make_shared<OrderBy>($1, $2);
    }
    ;   

opt_asc_desc:
    ASC          { $$ = OrderBy_ASC;     }
    |  DESC      { $$ = OrderBy_DESC;    }
    |       { $$ = OrderBy_DEFAULT; }
    ;

opt_limit:
        /* epsilon */
    {
        $$ = -1;
    }
    |   LIMIT VALUE_INT
    {
        if ($2 < 0) YYERROR;
        $$ = $2;
    }
    ;    

set_knob_type:
    ENABLE_NESTLOOP { $$ = EnableNestLoop; }
    |   ENABLE_SORTMERGE { $$ = EnableSortMerge; }
    ;

tbName: IDENTIFIER;

colName: IDENTIFIER;
%%
