    /* keywords are case insensitive */
%option caseless
    /* we don't need yywrap() function */
%option noyywrap
    /* we don't need yyunput() function */
%option nounput
    /* we don't need input() function */
%option noinput
    /* enable location */
%option bison-bridge
%option bison-locations

%{

#include "ast.h"
#include "yacc.tab.h"
#include <cmath>
#include <iostream>

// automatically update location
#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line; \
    yylloc->first_column = yylloc->last_column; \
    for (int i = 0; yytext[i] != '\0'; i++) { \
        if(yytext[i] == '\n') { \
            yylloc->last_line++; \
            yylloc->last_column = 1; \
        } else { \
            yylloc->last_column++; \
        } \
    }

%}

alpha [a-zA-Z]
digit [0-9]
white_space [ \t]+
new_line "\r"|"\n"|"\r\n"
sign "+"|"-"
identifier {alpha}(_|{alpha}|{digit})*
value_int {sign}?{digit}+
    /* 决赛 FLOAT32 位级传输：wire_param_literal 用 %.9g 回拼字面量，超出/低于常规范围时会
     * 生成科学计数法（如 1e-07、3.26937805e+09）。指数后缀设为可选，故纯整数串与
     * value_int 等长时按规则表靠前者(value_int)优先，不影响原有整数解析行为；一旦出现
     * 小数点或指数后缀才会比 value_int 更长，从而正确识别为浮点。 */
value_float {sign}?{digit}+(\.{digit}*)?([eE]{sign}?{digit}+)?
value_string '[^']*'
single_op ";"|"("|")"|","|"*"|"="|">"|"<"|"."|"+"|"-"

%x STATE_COMMENT

%%
    /* block comment */
"/*" { BEGIN(STATE_COMMENT); }
<STATE_COMMENT>"*/" { BEGIN(INITIAL); }
<STATE_COMMENT>[^*] { /* ignore the text of the comment */ }
<STATE_COMMENT>\* { /* ignore *'s that aren't part of */ }
    /* single line comment */
"--".* { /* ignore single line comment */ }
    /* white space and new line */
{white_space} { /* ignore white space */ }
{new_line} { /* ignore new line */ }
    /* keywords */
"SHOW" { return SHOW; }
"BEGIN" { return TXN_BEGIN; }
"COMMIT" { return TXN_COMMIT; }
"ABORT" { return TXN_ABORT; }
"ROLLBACK" { return TXN_ROLLBACK; }
"TABLES" { return TABLES; }
"CREATE" { return CREATE; }
"TABLE" { return TABLE; }
"DROP" { return DROP; }
"DESC" { return DESC; }
"INSERT" { return INSERT; }
"INTO" { return INTO; }
"VALUES" { return VALUES; }
"DELETE" { return DELETE; }
"FROM" { return FROM; }
"WHERE" { return WHERE; }
"UPDATE" { return UPDATE; }
"SET" { return SET; }
"SELECT" { return SELECT; }
"COUNT" { return COUNT; }
"MAX" { return MAX; }
"MIN" { return MIN; }
"SUM" { return SUM; }
"AVG" { return AVG; }
"DISTINCT" { return DISTINCT; }
"AS" { return AS; }
"GROUP" { return GROUP; }
"HAVING" { return HAVING; }
"UNION" { return UNION; }
"INT" { return INT; }
"CHAR" { return CHAR; }
"FLOAT" { return FLOAT; }
"INDEX" { return INDEX; }
"AND" { return AND; }
"OR" { return OR; }
"NOT" { return NOT; }
"JOIN" {return JOIN;}
"LEFT" { return LEFT; }
"RIGHT" { return RIGHT; }
"INNER" { return INNER; }
"OUTER" { return OUTER; }
"CROSS" { return CROSS; }
"FULL" { return FULL; }
"NATURAL" { return NATURAL; }
"SEMI" { return SEMI; }
"ANTI" { return ANTI; }
"LATERAL" { return LATERAL; }
"ON" { return ON; }
"EXPLAIN" { return EXPLAIN; }
"ANALYZE" { return ANALYZE; }
"EXIT" { return EXIT; }
"HELP" { return HELP; }
"ORDER" { return ORDER; }
"LIMIT" { return LIMIT; }
"BY" {  return BY;  }
"ASC" { return ASC; }
"ENABLE_NESTLOOP" { return ENABLE_NESTLOOP; }
"ENABLE_SORTMERGE" { return ENABLE_SORTMERGE; }
"TRUE" { 
    yylval->sv_bool = true;
    return VALUE_BOOL; 
}
"FALSE" {
    yylval->sv_bool = false;
    return VALUE_BOOL;
}
    /* 决赛 FLOAT32 位级传输：wire_param_literal 把 NaN 参数映射为 NAN 关键字（普通
     * 数字字面量无法表示 NaN）。位置在 identifier 规则之前故优先命中；表/列名不叫
     * nan，无冲突。比较语义由 analyze 层按 IEEE 改写。 */
"NAN" {
    yylval->sv_float = NAN;
    return VALUE_FLOAT;
}
    /* operators */
">=" { return GEQ; }
"<=" { return LEQ; }
"<>" { return NEQ; }
    /* 决赛 LOAD 校验 SQL 使用 != 形式的不等号（此前 '!' 落入未知字符规则被静默丢弃，
     * "a != 0" 被吃成 "a = 0"，OJ initial_stock_ytd_zero 校验 0 vs 5000000 实测） */
"!=" { return NEQ; }
{single_op} { return yytext[0]; }
    /* id */
{identifier} {
    yylval->sv_str = yytext;
    return IDENTIFIER;
}
    /* literals */
{value_int} {
    yylval->sv_int = atoi(yytext);
    return VALUE_INT;
}
{value_float} {
    yylval->sv_float = atof(yytext);
    return VALUE_FLOAT;
}
{value_string} {
    yylval->sv_str = std::string(yytext + 1, strlen(yytext) - 2);
    return VALUE_STRING;
}
    /* EOF */
<<EOF>> { return T_EOF; }
    /* unexpected char：必须交给 yacc 报 parse error，不能静默跳过——
     * 静默丢字符会改写查询语义（如 "!=" 曾被吃成 "="，返回错误结果比报错致命）。 */
. {
    std::cerr << "Lexer Error: unexpected character " << yytext[0] << std::endl;
    return yytext[0];
}
%%
