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
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT EXPLAIN ANALYZE INT CHAR FLOAT INDEX AND JOIN ON EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE
%token COUNT MAX MIN SUM AVG AS GROUP HAVING LIMIT UNION DISTINCT
%token LEFT RIGHT INNER OUTER CROSS FULL
// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_select> select_stmt union_branch
%type <sv_selects> union_query
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
%type <sv_cond> condition having_condition
%type <sv_conds> whereClause optWhereClause having_clause
%type <sv_orderby>  order_clause
%type <sv_orderbys> opt_order_clause order_list
%type <sv_orderby_dir> opt_asc_desc
%type <sv_agg_expr> agg_func
%type <sv_agg_exprs> agg_list
%type <sv_str> agg_name
%type <sv_cols> opt_group_by group_by_list
%type <sv_conds> opt_having
%type <sv_int> opt_limit
%type <sv_str> opt_alias
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
    |   select_stmt
    {
        $$ = $1;
    }
    |   EXPLAIN select_stmt
    {
        $$ = std::make_shared<ExplainStmt>($2, false);
    }
    |   EXPLAIN ANALYZE select_stmt
    {
        $$ = std::make_shared<ExplainStmt>($3, true);
    }
    |   SELECT '*' FROM '(' union_query ')' AS tbName opt_order_clause opt_limit
    {
        $$ = std::make_shared<UnionStmt>($5, $8, $9, $10 >= 0, $10 < 0 ? 0 : $10);
    }
    ;

select_stmt:
        SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        $$ = std::make_shared<SelectStmt>($2, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, $2, $4, $5, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        $$ = std::make_shared<SelectStmt>($2, $4, $6, $7, $8, $9, $10, $11 >= 0, $11 < 0 ? 0 : $11);
    }
    ;

union_branch:
        SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_limit
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_limit
    {
        $$ = std::make_shared<SelectStmt>($2, std::vector<std::shared_ptr<AggExpr>>{}, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit
    {
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, $2, $4, $5, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit
    {
        $$ = std::make_shared<SelectStmt>($2, $4, $6, $7, $8, $9, std::vector<std::shared_ptr<OrderBy>>{}, $10 >= 0, $10 < 0 ? 0 : $10);
    }
    ;

union_query:
        union_branch UNION union_branch
    {
        $$ = std::vector<std::shared_ptr<SelectStmt>>{$1, $3};
    }
    |   union_query UNION union_branch
    {
        $$ = $1;
        $$.push_back($3);
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
    ;

optWhereClause:
        /* epsilon */ { /* ignore*/ }
    |   WHERE whereClause
    {
        $$ = $2;
    }
    ;

whereClause:
        condition 
    {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    |   whereClause AND condition
    {
        $$.push_back($3);
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
            INNER_JOIN, $1, $3, $5);
    }
    | joined_table JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $3, std::vector<std::shared_ptr<BinaryExpr>>{});
    }
    | joined_table INNER JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            INNER_JOIN, $1, $4, $6);
    }
    | joined_table LEFT opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            LEFT_JOIN, $1, $5, $7);
    }
    | joined_table RIGHT opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            RIGHT_JOIN, $1, $5, $7);
    }
    | joined_table FULL opt_outer JOIN table_ref ON whereClause
    {
        $$ = std::make_shared<JoinExpr>(
            FULL_JOIN, $1, $5, $7);
    }
    | joined_table CROSS JOIN table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $4, std::vector<std::shared_ptr<BinaryExpr>>{});
    }
    | joined_table ',' table_ref
    {
        $$ = std::make_shared<JoinExpr>(
            CROSS_JOIN, $1, $3, std::vector<std::shared_ptr<BinaryExpr>>{});
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
        having_condition
    {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    |   having_clause AND having_condition
    {
        $$.push_back($3);
    }
    ;

opt_having:
        /* epsilon */
    {
        $$ = {};
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
