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
%token COUNT MAX MIN SUM AVG AS GROUP HAVING LIMIT UNION
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
%type <sv_val> value
%type <sv_vals> valueList valueRows
%type <sv_str> tbName colName
%type <sv_strs> colNameList
%type <sv_table_list> tableList
%type <sv_col> col
%type <sv_cols> colList selector
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_cond> condition having_condition
%type <sv_conds> whereClause optWhereClause having_clause
%type <sv_orderby>  order_clause
%type <sv_orderbys> opt_order_clause order_list
%type <sv_orderby_dir> opt_asc_desc
%type <sv_agg_expr> agg_func
%type <sv_agg_exprs> agg_list
%type <sv_cols> opt_group_by group_by_list
%type <sv_conds> opt_having
%type <sv_int> opt_limit
%type <sv_cols> select_list
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
        SELECT '*' FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, $4->tabs, conds, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT colList FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>($2, std::vector<std::shared_ptr<AggExpr>>{}, $4->tabs, conds, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT agg_list FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, $2, $4->tabs, conds, $6, $7, $8, $9 >= 0, $9 < 0 ? 0 : $9);
    }
    |   SELECT colList ',' agg_list FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit
    {
        auto conds = $6->join_conds;
        conds.insert(conds.end(), $7.begin(), $7.end());
        $$ = std::make_shared<SelectStmt>($2, $4, $6->tabs, conds, $8, $9, $10, $11 >= 0, $11 < 0 ? 0 : $11);
    }
    ;

union_branch:
        SELECT '*' FROM tableList optWhereClause opt_group_by opt_having opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, $4->tabs, conds, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT colList FROM tableList optWhereClause opt_group_by opt_having opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>($2, std::vector<std::shared_ptr<AggExpr>>{}, $4->tabs, conds, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT agg_list FROM tableList optWhereClause opt_group_by opt_having opt_limit
    {
        auto conds = $4->join_conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, $2, $4->tabs, conds, $6, $7, std::vector<std::shared_ptr<OrderBy>>{}, $8 >= 0, $8 < 0 ? 0 : $8);
    }
    |   SELECT colList ',' agg_list FROM tableList optWhereClause opt_group_by opt_having opt_limit
    {
        auto conds = $6->join_conds;
        conds.insert(conds.end(), $7.begin(), $7.end());
        $$ = std::make_shared<SelectStmt>($2, $4, $6->tabs, conds, $8, $9, std::vector<std::shared_ptr<OrderBy>>{}, $10 >= 0, $10 < 0 ? 0 : $10);
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
    |   colName '=' colName value
    {
        /* 题9：算术增量 v=v+1（词法把 +1/-1 归并为带符号 VALUE_INT，无空格情形） */
        $$ = std::make_shared<SetClause>($1, $3, $4, false);
    }
    |   colName '=' colName '+' value
    {
        /* 带空格加号 v = v + 1 */
        $$ = std::make_shared<SetClause>($1, $3, $5, false);
    }
    |   colName '=' colName '-' value
    {
        /* 带空格减号 v = v - 1：字面量为正、置 neg 取负 */
        $$ = std::make_shared<SetClause>($1, $3, $5, true);
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
        COUNT '(' '*' ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_COUNT, nullptr, $5, true);
    }
    |   COUNT '(' col ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_COUNT, $3, $5, false);
    }
    |   MAX '(' col ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_MAX, $3, $5, false);
    }
    |   MIN '(' col ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_MIN, $3, $5, false);
    }
    |   SUM '(' col ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_SUM, $3, $5, false);
    }
    |   AVG '(' col ')' opt_alias
    {
        $$ = std::make_shared<AggExpr>(AGG_AVG, $3, $5, false);
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

selector:
        '*'
    {
        $$ = {};
    }
    |   colList
    ;

select_list:
        colList
    |   colList ',' agg_list
    |   agg_list
    ;

tableList:
        tbName
    {
        $$ = std::make_shared<TableListInfo>();
        $$->tabs.push_back($1);
    }
    |   tableList ',' tbName
    {
        $$ = $1;
        $$->tabs.push_back($3);
    }
    |   tableList JOIN tbName
    {
        $$ = $1;
        $$->tabs.push_back($3);
    }
    |   tableList JOIN tbName ON whereClause
    {
        $$ = $1;
        $$->tabs.push_back($3);
        $$->join_conds.insert($$->join_conds.end(), $5.begin(), $5.end());
    }
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
        auto col = std::make_shared<Col>("", $1->to_string());
        $$ = std::make_shared<BinaryExpr>(col, $2, $3);
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
