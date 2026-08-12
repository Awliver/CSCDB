/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "/root/csc-db-learn/csc-db/src/parser/yacc.y"

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

static std::shared_ptr<ast::AggExpr> make_aggregate_expr(
    const std::string &name, std::vector<std::shared_ptr<ast::Col>> arguments,
    std::string alias, bool distinct) {
    ast::AggType type;
    if (!ast::aggregate_type_from_name(name, type)) return nullptr;
    return std::make_shared<ast::AggExpr>(type, std::move(arguments), std::move(alias),
                                          false, distinct);
}

static std::shared_ptr<ast::QueryExpr> append_set_operand(
    std::shared_ptr<ast::QueryExpr> left, ast::SetOpType op, bool all,
    std::shared_ptr<ast::QueryExpr> right) {
    // INTERSECT binds more tightly than UNION and EXCEPT. QueryGroup is not a
    // UnionStmt, so explicit parentheses remain an association boundary.
    if (op == ast::SetOpType::INTERSECT) {
        if (auto root = std::dynamic_pointer_cast<ast::UnionStmt>(left);
            root != nullptr && root->op != ast::SetOpType::INTERSECT) {
            root->right = append_set_operand(std::move(root->right), op, all,
                                             std::move(right));
            return left;
        }
    }
    return std::make_shared<ast::UnionStmt>(std::move(left), std::move(right), op, all);
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

#line 165 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "yacc.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_SHOW = 3,                       /* SHOW  */
  YYSYMBOL_TABLES = 4,                     /* TABLES  */
  YYSYMBOL_CREATE = 5,                     /* CREATE  */
  YYSYMBOL_TABLE = 6,                      /* TABLE  */
  YYSYMBOL_DROP = 7,                       /* DROP  */
  YYSYMBOL_DESC = 8,                       /* DESC  */
  YYSYMBOL_INSERT = 9,                     /* INSERT  */
  YYSYMBOL_INTO = 10,                      /* INTO  */
  YYSYMBOL_VALUES = 11,                    /* VALUES  */
  YYSYMBOL_DELETE = 12,                    /* DELETE  */
  YYSYMBOL_FROM = 13,                      /* FROM  */
  YYSYMBOL_ASC = 14,                       /* ASC  */
  YYSYMBOL_ORDER = 15,                     /* ORDER  */
  YYSYMBOL_BY = 16,                        /* BY  */
  YYSYMBOL_WHERE = 17,                     /* WHERE  */
  YYSYMBOL_UPDATE = 18,                    /* UPDATE  */
  YYSYMBOL_SET = 19,                       /* SET  */
  YYSYMBOL_SELECT = 20,                    /* SELECT  */
  YYSYMBOL_EXPLAIN = 21,                   /* EXPLAIN  */
  YYSYMBOL_ANALYZE = 22,                   /* ANALYZE  */
  YYSYMBOL_INT = 23,                       /* INT  */
  YYSYMBOL_CHAR = 24,                      /* CHAR  */
  YYSYMBOL_FLOAT = 25,                     /* FLOAT  */
  YYSYMBOL_INDEX = 26,                     /* INDEX  */
  YYSYMBOL_AND = 27,                       /* AND  */
  YYSYMBOL_OR = 28,                        /* OR  */
  YYSYMBOL_NOT = 29,                       /* NOT  */
  YYSYMBOL_JOIN = 30,                      /* JOIN  */
  YYSYMBOL_ON = 31,                        /* ON  */
  YYSYMBOL_EXIT = 32,                      /* EXIT  */
  YYSYMBOL_HELP = 33,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 34,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 35,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 36,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 37,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 38,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 39,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 40,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_COUNT = 41,                     /* COUNT  */
  YYSYMBOL_MAX = 42,                       /* MAX  */
  YYSYMBOL_MIN = 43,                       /* MIN  */
  YYSYMBOL_SUM = 44,                       /* SUM  */
  YYSYMBOL_AVG = 45,                       /* AVG  */
  YYSYMBOL_AS = 46,                        /* AS  */
  YYSYMBOL_GROUP = 47,                     /* GROUP  */
  YYSYMBOL_HAVING = 48,                    /* HAVING  */
  YYSYMBOL_LIMIT = 49,                     /* LIMIT  */
  YYSYMBOL_OFFSET = 50,                    /* OFFSET  */
  YYSYMBOL_ALL = 51,                       /* ALL  */
  YYSYMBOL_DISTINCT = 52,                  /* DISTINCT  */
  YYSYMBOL_USING = 53,                     /* USING  */
  YYSYMBOL_IS = 54,                        /* IS  */
  YYSYMBOL_NULL_T = 55,                    /* NULL_T  */
  YYSYMBOL_UNION = 56,                     /* UNION  */
  YYSYMBOL_LIKE = 57,                      /* LIKE  */
  YYSYMBOL_BETWEEN = 58,                   /* BETWEEN  */
  YYSYMBOL_EXISTS = 59,                    /* EXISTS  */
  YYSYMBOL_IN = 60,                        /* IN  */
  YYSYMBOL_LEFT = 61,                      /* LEFT  */
  YYSYMBOL_RIGHT = 62,                     /* RIGHT  */
  YYSYMBOL_INNER = 63,                     /* INNER  */
  YYSYMBOL_OUTER = 64,                     /* OUTER  */
  YYSYMBOL_CROSS = 65,                     /* CROSS  */
  YYSYMBOL_FULL = 66,                      /* FULL  */
  YYSYMBOL_NATURAL = 67,                   /* NATURAL  */
  YYSYMBOL_SEMI = 68,                      /* SEMI  */
  YYSYMBOL_ANTI = 69,                      /* ANTI  */
  YYSYMBOL_LATERAL = 70,                   /* LATERAL  */
  YYSYMBOL_LEQ = 71,                       /* LEQ  */
  YYSYMBOL_NEQ = 72,                       /* NEQ  */
  YYSYMBOL_GEQ = 73,                       /* GEQ  */
  YYSYMBOL_T_EOF = 74,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 75,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 76,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 77,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 78,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 79,                /* VALUE_BOOL  */
  YYSYMBOL_80_ = 80,                       /* ';'  */
  YYSYMBOL_81_ = 81,                       /* '='  */
  YYSYMBOL_82_ = 82,                       /* '('  */
  YYSYMBOL_83_ = 83,                       /* ')'  */
  YYSYMBOL_84_ = 84,                       /* '*'  */
  YYSYMBOL_85_ = 85,                       /* ','  */
  YYSYMBOL_86_ = 86,                       /* '.'  */
  YYSYMBOL_87_ = 87,                       /* '<'  */
  YYSYMBOL_88_ = 88,                       /* '>'  */
  YYSYMBOL_89_ = 89,                       /* '+'  */
  YYSYMBOL_90_ = 90,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 91,                  /* $accept  */
  YYSYMBOL_start = 92,                     /* start  */
  YYSYMBOL_stmt = 93,                      /* stmt  */
  YYSYMBOL_txnStmt = 94,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 95,                    /* dbStmt  */
  YYSYMBOL_setStmt = 96,                   /* setStmt  */
  YYSYMBOL_ddl = 97,                       /* ddl  */
  YYSYMBOL_dml = 98,                       /* dml  */
  YYSYMBOL_query_expression = 99,          /* query_expression  */
  YYSYMBOL_union_expression = 100,         /* union_expression  */
  YYSYMBOL_union_quantifier = 101,         /* union_quantifier  */
  YYSYMBOL_query_primary = 102,            /* query_primary  */
  YYSYMBOL_select_core = 103,              /* select_core  */
  YYSYMBOL_opt_select_distinct = 104,      /* opt_select_distinct  */
  YYSYMBOL_fieldList = 105,                /* fieldList  */
  YYSYMBOL_colNameList = 106,              /* colNameList  */
  YYSYMBOL_field = 107,                    /* field  */
  YYSYMBOL_type = 108,                     /* type  */
  YYSYMBOL_valueList = 109,                /* valueList  */
  YYSYMBOL_valueRows = 110,                /* valueRows  */
  YYSYMBOL_value = 111,                    /* value  */
  YYSYMBOL_condition = 112,                /* condition  */
  YYSYMBOL_optWhereClause = 113,           /* optWhereClause  */
  YYSYMBOL_whereClause = 114,              /* whereClause  */
  YYSYMBOL_where_or_expr = 115,            /* where_or_expr  */
  YYSYMBOL_where_and_expr = 116,           /* where_and_expr  */
  YYSYMBOL_where_not_expr = 117,           /* where_not_expr  */
  YYSYMBOL_col = 118,                      /* col  */
  YYSYMBOL_colList = 119,                  /* colList  */
  YYSYMBOL_op = 120,                       /* op  */
  YYSYMBOL_expr = 121,                     /* expr  */
  YYSYMBOL_setClauses = 122,               /* setClauses  */
  YYSYMBOL_setClause = 123,                /* setClause  */
  YYSYMBOL_arith_chain = 124,              /* arith_chain  */
  YYSYMBOL_arith_term = 125,               /* arith_term  */
  YYSYMBOL_agg_list = 126,                 /* agg_list  */
  YYSYMBOL_agg_col_list = 127,             /* agg_col_list  */
  YYSYMBOL_agg_func = 128,                 /* agg_func  */
  YYSYMBOL_agg_name = 129,                 /* agg_name  */
  YYSYMBOL_opt_alias = 130,                /* opt_alias  */
  YYSYMBOL_from_clause = 131,              /* from_clause  */
  YYSYMBOL_table_ref = 132,                /* table_ref  */
  YYSYMBOL_required_alias = 133,           /* required_alias  */
  YYSYMBOL_joined_table = 134,             /* joined_table  */
  YYSYMBOL_opt_outer = 135,                /* opt_outer  */
  YYSYMBOL_opt_group_by = 136,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 137,            /* group_by_list  */
  YYSYMBOL_having_condition = 138,         /* having_condition  */
  YYSYMBOL_having_clause = 139,            /* having_clause  */
  YYSYMBOL_having_or_expr = 140,           /* having_or_expr  */
  YYSYMBOL_having_and_expr = 141,          /* having_and_expr  */
  YYSYMBOL_having_not_expr = 142,          /* having_not_expr  */
  YYSYMBOL_opt_having = 143,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 144,         /* opt_order_clause  */
  YYSYMBOL_order_list = 145,               /* order_list  */
  YYSYMBOL_order_clause = 146,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 147,             /* opt_asc_desc  */
  YYSYMBOL_limit_offset_clause = 148,      /* limit_offset_clause  */
  YYSYMBOL_set_knob_type = 149,            /* set_knob_type  */
  YYSYMBOL_tbName = 150,                   /* tbName  */
  YYSYMBOL_colName = 151                   /* colName  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  49
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   577

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  91
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  61
/* YYNRULES -- Number of rules.  */
#define YYNRULES  205
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  452

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   334


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      82,    83,    84,    89,    85,    90,    86,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    80,
      87,    81,    88,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   154,   154,   159,   164,   169,   177,   178,   179,   180,
     181,   185,   189,   193,   197,   204,   208,   215,   222,   226,
     230,   234,   238,   245,   249,   253,   257,   261,   265,   269,
     276,   284,   288,   296,   299,   303,   310,   314,   321,   325,
     329,   333,   340,   341,   345,   349,   356,   360,   367,   374,
     378,   382,   389,   393,   400,   404,   412,   416,   420,   424,
     431,   435,   440,   445,   449,   453,   457,   461,   465,   470,
     475,   479,   487,   490,   497,   504,   508,   515,   519,   526,
     530,   534,   541,   545,   552,   556,   560,   566,   574,   578,
     582,   586,   590,   594,   601,   605,   612,   616,   623,   627,
     635,   646,   650,   657,   662,   666,   676,   680,   687,   691,
     698,   703,   708,   713,   722,   723,   724,   725,   726,   727,
     736,   739,   743,   750,   758,   762,   766,   770,   777,   781,
     789,   793,   798,   805,   810,   816,   821,   828,   833,   838,
     845,   850,   855,   862,   867,   872,   879,   884,   890,   896,
     902,   908,   914,   920,   925,   932,   937,   944,   949,   956,
     961,   968,   973,   980,   985,   992,  1000,  1002,  1008,  1011,
    1018,  1022,  1029,  1033,  1037,  1041,  1045,  1049,  1056,  1063,
    1067,  1074,  1078,  1085,  1089,  1093,  1101,  1104,  1111,  1116,
    1122,  1126,  1133,  1137,  1144,  1145,  1146,  1151,  1154,  1159,
    1164,  1169,  1177,  1178,  1181,  1183
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "SHOW", "TABLES",
  "CREATE", "TABLE", "DROP", "DESC", "INSERT", "INTO", "VALUES", "DELETE",
  "FROM", "ASC", "ORDER", "BY", "WHERE", "UPDATE", "SET", "SELECT",
  "EXPLAIN", "ANALYZE", "INT", "CHAR", "FLOAT", "INDEX", "AND", "OR",
  "NOT", "JOIN", "ON", "EXIT", "HELP", "TXN_BEGIN", "TXN_COMMIT",
  "TXN_ABORT", "TXN_ROLLBACK", "ORDER_BY", "ENABLE_NESTLOOP",
  "ENABLE_SORTMERGE", "COUNT", "MAX", "MIN", "SUM", "AVG", "AS", "GROUP",
  "HAVING", "LIMIT", "OFFSET", "ALL", "DISTINCT", "USING", "IS", "NULL_T",
  "UNION", "LIKE", "BETWEEN", "EXISTS", "IN", "LEFT", "RIGHT", "INNER",
  "OUTER", "CROSS", "FULL", "NATURAL", "SEMI", "ANTI", "LATERAL", "LEQ",
  "NEQ", "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT",
  "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='", "'('", "')'", "'*'", "','",
  "'.'", "'<'", "'>'", "'+'", "'-'", "$accept", "start", "stmt", "txnStmt",
  "dbStmt", "setStmt", "ddl", "dml", "query_expression",
  "union_expression", "union_quantifier", "query_primary", "select_core",
  "opt_select_distinct", "fieldList", "colNameList", "field", "type",
  "valueList", "valueRows", "value", "condition", "optWhereClause",
  "whereClause", "where_or_expr", "where_and_expr", "where_not_expr",
  "col", "colList", "op", "expr", "setClauses", "setClause", "arith_chain",
  "arith_term", "agg_list", "agg_col_list", "agg_func", "agg_name",
  "opt_alias", "from_clause", "table_ref", "required_alias",
  "joined_table", "opt_outer", "opt_group_by", "group_by_list",
  "having_condition", "having_clause", "having_or_expr", "having_and_expr",
  "having_not_expr", "opt_having", "opt_order_clause", "order_list",
  "order_clause", "opt_asc_desc", "limit_offset_clause", "set_knob_type",
  "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-325)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-205)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     371,    43,   114,   147,   -11,    61,    83,   -11,    96,   107,
      29,  -325,  -325,  -325,  -325,  -325,  -325,  -325,     4,   175,
      67,  -325,  -325,  -325,  -325,  -325,  -325,    42,  -325,  -325,
    -325,   193,   -11,   -11,   -11,   -11,  -325,  -325,   -11,   -11,
     212,  -325,  -325,   137,  -325,   424,     4,  -325,   154,  -325,
    -325,   240,   133,   162,   -11,   161,   187,  -325,   194,    -6,
     265,   209,   211,  -325,  -325,  -325,  -325,  -325,   -34,   278,
     267,    -1,     0,  -325,   228,   237,  -325,  -325,  -325,   116,
    -325,  -325,     4,   250,   259,  -325,  -325,   209,   209,   209,
     255,   209,   148,  -325,  -325,     3,  -325,   272,  -325,   126,
     269,   126,   429,   126,   434,    39,   209,   261,   146,   146,
     271,  -325,  -325,   310,   312,   120,  -325,   227,   204,  -325,
     266,   413,   286,   274,   148,   306,   148,  -325,  -325,   374,
     383,  -325,    84,   209,  -325,   219,   338,    12,   265,  -325,
     179,    27,  -325,   265,   349,    10,   265,  -325,  -325,   -17,
     341,   342,  -325,  -325,  -325,  -325,  -325,   116,   361,   362,
    -325,   209,  -325,   348,  -325,  -325,  -325,   209,  -325,  -325,
    -325,  -325,  -325,   346,  -325,   366,   433,  -325,     4,   372,
     148,   148,   243,    24,   413,   409,   381,  -325,  -325,  -325,
    -325,  -325,  -325,   409,  -325,  -325,   404,     4,    12,   376,
     192,   449,   126,   152,   199,   393,   421,   438,   172,   476,
     485,   126,   442,  -325,  -325,   449,   443,   126,   449,   444,
    -325,   351,    27,    27,  -325,  -325,  -325,  -325,   445,  -325,
    -325,   413,   413,   439,   437,  -325,   383,  -325,   413,   409,
     441,   469,  -325,  -325,  -325,  -325,   498,    40,  -325,   413,
     413,  -325,   404,  -325,   446,   447,   105,  -325,   510,   479,
      15,  -325,   501,   502,   503,   504,   505,   506,   126,   126,
     507,   126,   438,   438,   508,   438,   126,   126,  -325,  -325,
     479,  -325,   265,   479,   369,    27,   444,  -325,  -325,   456,
    -325,   373,   413,  -325,  -325,   513,    40,  -325,   409,   458,
     377,  -325,  -325,  -325,   105,   105,   453,  -325,  -325,   444,
     264,  -325,   135,   460,   126,   126,   126,   126,   126,   126,
      30,  -325,   126,  -325,   514,   515,   126,   516,   512,   517,
    -325,   449,  -325,   464,  -325,  -325,  -325,  -325,   412,   409,
     466,   415,  -325,  -325,  -325,  -325,  -325,  -325,   465,   264,
     264,    95,   313,  -325,  -325,   523,   525,  -325,  -325,  -325,
     209,   522,   524,    44,   526,   527,    46,   263,   472,   121,
     126,   126,  -325,   126,   273,   334,   479,    27,  -325,  -325,
    -325,  -325,   444,  -325,   473,   103,   409,   108,   409,   264,
     264,   418,   339,   340,   353,   477,   358,   367,   368,   478,
    -325,  -325,   209,   382,   480,  -325,  -325,  -325,  -325,  -325,
    -325,  -325,  -325,  -325,  -325,  -325,   509,  -325,  -325,   511,
    -325,  -325,   525,  -325,  -325,  -325,  -325,  -325,  -325,  -325,
    -325,   209,  -325,  -325,  -325,  -325,  -325,  -325,   209,   422,
    -325,  -325,   209,  -325,  -325,   427,   428,  -325,   431,  -325,
    -325,  -325
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,    42,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       0,     9,     6,    10,     7,     8,    27,   189,    31,    36,
      15,     0,     0,     0,     0,     0,   204,    20,     0,     0,
       0,   202,   203,     0,    43,     0,     0,    28,     0,     1,
       2,     0,    33,   197,     0,     0,     0,    19,     0,     0,
      72,     0,     0,   114,   115,   116,   117,   118,   205,     0,
      84,     0,     0,   106,     0,     0,    83,    29,    37,     0,
      35,    34,     0,     0,     0,    30,    16,     0,     0,     0,
       0,     0,     0,    25,   205,    72,    96,     0,    17,     0,
       0,     0,     0,     0,     0,     0,     0,   205,   196,   196,
     188,   190,    32,   198,   199,     0,    44,     0,     0,    46,
       0,     0,    23,     0,     0,     0,     0,    81,    73,    74,
      76,    78,     0,     0,    26,     0,     0,     0,    72,   130,
     123,   120,    86,    72,    85,     0,    72,   119,   107,     0,
       0,     0,    82,   195,   194,   193,   192,     0,     0,     0,
      18,     0,    49,     0,    51,    48,    21,     0,    22,    58,
      56,    57,    59,     0,    52,     0,     0,    79,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    92,    91,    93,
      88,    89,    90,     0,    97,    98,    99,     0,     0,     0,
       0,   168,     0,   166,   166,     0,     0,   166,     0,     0,
       0,     0,     0,   122,   124,   168,     0,     0,   168,     0,
     108,     0,   120,   120,   191,   200,   201,    45,     0,    47,
      54,     0,     0,     0,     0,    80,    75,    77,     0,     0,
       0,     0,    70,    61,    94,    95,     0,     0,    60,     0,
       0,   103,   100,   101,     0,     0,     0,   125,     0,   186,
     134,   167,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   166,   166,     0,   166,     0,     0,   165,   121,
     186,    87,    72,   186,     0,   120,     0,   110,   111,     0,
      53,     0,     0,    69,    62,     0,     0,    71,     0,     0,
       0,   104,   105,   102,     0,    37,     0,   128,   126,     0,
       0,    38,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   147,     0,   148,     0,     0,     0,     0,     0,     0,
      39,   168,    40,     0,   112,   109,    50,    55,     0,     0,
       0,     0,    63,    67,    65,   127,   129,   170,   169,     0,
       0,     0,     0,   185,   187,   178,   180,   182,   132,   131,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   149,     0,     0,     0,   186,   120,    24,    64,
      68,    66,     0,   183,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     136,   135,     0,     0,     0,   150,   151,   152,   154,   153,
     160,   159,    41,   113,   171,   184,     0,   174,   172,     0,
     176,   173,   179,   181,   133,   156,   155,   162,   161,   139,
     138,     0,   158,   157,   164,   163,   142,   141,     0,     0,
     145,   144,     0,   175,   177,     0,     0,   137,     0,   140,
     143,   146
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -325,  -325,  -325,  -325,  -325,  -325,  -325,  -325,    -8,  -325,
    -325,   481,  -325,  -325,  -325,   -88,   400,  -325,  -203,  -325,
    -104,  -325,   -84,   -63,  -325,   385,   -91,   -24,  -325,   -86,
    -189,  -325,   435,  -325,   315,   467,   352,    22,  -325,  -215,
     -87,   -90,   268,   -94,  -165,  -200,  -325,  -325,   220,  -325,
     184,  -324,  -261,  -325,  -325,   417,   468,  -325,  -325,     2,
     -61
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      82,    28,    29,    45,   115,   118,   116,   165,   173,   122,
     244,   127,    93,   128,   129,   130,   131,   132,    71,   193,
     246,    95,    96,   252,   253,    72,   221,   352,    74,   214,
     138,   139,   308,   140,   264,   259,   348,   353,   354,   355,
     356,   357,   311,    53,   110,   111,   155,    85,    43,    75,
      76
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      97,   120,    47,   123,   248,    90,    37,   287,   288,    40,
      48,   134,   101,   103,   143,   280,   146,   174,   283,   330,
      92,    70,   332,   217,     9,   383,   117,   119,   119,   291,
     119,   195,     9,   177,    55,    56,    57,    58,    77,   267,
      59,    60,   270,   200,   300,   152,   312,    30,  -119,     9,
     295,    46,  -204,   241,   201,   109,    86,    51,   107,   215,
       9,   367,   218,   179,    36,   219,   423,    73,   313,    31,
     334,    38,    97,   212,   196,   394,    91,   398,   144,   242,
     243,   151,   136,   368,   102,   104,    18,    36,   133,   338,
     237,   149,   251,   341,   198,   104,    39,   395,    52,   399,
     117,   141,   213,   141,   200,   141,   229,   324,   325,   342,
     327,    18,   260,   182,   107,   412,   169,   170,   171,   172,
      32,   278,    18,   150,    73,   220,   148,   290,   174,   199,
     282,   376,   416,   109,   294,    41,    42,   419,   183,   141,
      33,   184,   185,   174,   186,   301,   302,    50,   251,   385,
     379,   306,   403,    34,   153,   187,   188,   189,   417,    44,
     154,   245,   413,   420,   124,   190,   187,   188,   189,   245,
     234,   191,   192,    35,   404,    49,   190,   124,   320,   321,
     307,   323,   191,   192,    80,    81,   328,   329,   174,   254,
     255,   107,   174,   108,   125,   220,   136,   418,   331,   421,
     141,    36,   271,   160,   141,   161,    54,   125,   137,   202,
     107,    83,    84,   141,   358,   245,   261,   126,    62,   141,
     262,   263,   202,   107,   361,   362,   363,   364,   365,   366,
     126,    61,   369,   272,   273,   274,   372,    78,   275,   299,
     203,   204,   205,    87,   206,   207,   208,   209,   210,   359,
     162,   163,   164,   203,   204,   205,    79,   206,   207,   208,
     209,   210,   335,   261,   211,   386,   388,   265,   266,    88,
     141,   141,   391,   141,   245,   257,    89,   211,   141,   141,
     405,   406,    92,   407,    94,   347,   351,   166,   340,   167,
      98,    99,   124,   349,    94,   169,   170,   171,   172,   119,
     238,   239,   124,   240,   401,    63,    64,    65,    66,    67,
     105,   409,   411,   100,   439,   245,   141,   141,   141,   141,
     141,   141,   125,   106,   141,   351,   351,   113,   141,   426,
     428,   430,   125,   433,   435,   437,   114,   121,   107,    68,
     441,   119,   400,   445,   142,   126,   350,  -204,   107,   168,
     446,   167,   408,   135,   448,   126,   157,   176,   414,   167,
     158,   159,   245,   124,   245,   351,   351,   387,   124,   124,
     119,   175,   141,   141,     1,   141,     2,   119,     3,     4,
       5,   119,   124,     6,   187,   188,   189,   124,   178,     7,
       8,     9,    10,   125,   190,   216,   124,   124,   125,   125,
     191,   192,   180,    11,    12,    13,    14,    15,    16,   107,
     181,   124,   125,   410,   107,   107,   126,   125,   425,   427,
     197,   126,   126,   268,   222,   223,   125,   125,   107,   230,
     228,   231,   429,   107,   285,   126,   286,   432,   225,   226,
     126,   125,   107,   107,   233,    17,   434,   436,   232,   126,
     126,   269,   333,    18,   286,   235,   337,   107,   231,   256,
     344,   440,   231,   247,   126,    63,    64,    65,    66,    67,
      63,    64,    65,    66,    67,    63,    64,    65,    66,    67,
     169,   170,   171,   172,   107,   169,   170,   171,   172,   169,
     170,   171,   172,   249,   250,   378,   258,   231,   381,    68,
     231,   424,   261,   167,    68,   447,   276,   167,    69,   147,
     449,   450,   167,   167,   451,   277,   167,   279,   281,   107,
     293,   292,   289,   296,   297,   298,   309,   310,   346,   304,
     305,   314,   315,   316,   317,   318,   319,   322,   326,   336,
     339,   343,   360,   374,   370,   371,   373,   377,   375,   380,
     382,   389,   390,   392,   402,   393,   415,   396,   397,   431,
     438,   227,   442,   112,   443,   236,   444,   303,   194,   145,
     384,   284,   345,   422,   224,     0,     0,   156
};

static const yytype_int16 yycheck[] =
{
      61,    89,    10,    91,   193,    11,     4,   222,   223,     7,
      18,    95,    13,    13,   101,   215,   103,   121,   218,   280,
      17,    45,   283,    13,    20,   349,    87,    88,    89,   232,
      91,   135,    20,   124,    32,    33,    34,    35,    46,   204,
      38,    39,   207,   137,   247,   106,    31,     4,    82,    20,
     239,    22,    86,    29,   138,    79,    54,    15,    75,   143,
      20,    31,   146,   126,    75,    82,   390,    45,    53,    26,
     285,    10,   133,    46,   135,    31,    82,    31,   102,    55,
     184,   105,    70,    53,    85,    85,    82,    75,    85,   292,
     181,    52,   196,   296,    82,    85,    13,    53,    56,    53,
     161,    99,    75,   101,   198,   103,   167,   272,   273,   298,
     275,    82,   202,    29,    75,   376,    76,    77,    78,    79,
       6,   211,    82,    84,   102,   149,   104,   231,   232,   137,
     217,   331,    29,   157,   238,    39,    40,    29,    54,   137,
      26,    57,    58,   247,    60,   249,   250,    80,   252,    54,
     339,    46,    31,     6,     8,    71,    72,    73,    55,    52,
      14,   185,   377,    55,    29,    81,    71,    72,    73,   193,
     178,    87,    88,    26,    53,     0,    81,    29,   268,   269,
      75,   271,    87,    88,    51,    52,   276,   277,   292,   197,
     198,    75,   296,    77,    59,   219,    70,   386,   282,   388,
     198,    75,    30,    83,   202,    85,    13,    59,    82,    30,
      75,    49,    50,   211,    79,   239,    64,    82,    81,   217,
      68,    69,    30,    75,   314,   315,   316,   317,   318,   319,
      82,    19,   322,    61,    62,    63,   326,    83,    66,   247,
      61,    62,    63,    82,    65,    66,    67,    68,    69,   312,
      23,    24,    25,    61,    62,    63,    16,    65,    66,    67,
      68,    69,   286,    64,    85,   351,   352,    68,    69,    82,
     268,   269,   360,   271,   298,    83,    82,    85,   276,   277,
     370,   371,    17,   373,    75,   309,   310,    83,   296,    85,
      79,    13,    29,    29,    75,    76,    77,    78,    79,   360,
      57,    58,    29,    60,   367,    41,    42,    43,    44,    45,
      82,   374,   375,    46,   402,   339,   314,   315,   316,   317,
     318,   319,    59,    86,   322,   349,   350,    77,   326,   392,
     393,   394,    59,   396,   397,   398,    77,    82,    75,    75,
     403,   402,    79,   431,    75,    82,    82,    86,    75,    83,
     438,    85,    79,    81,   442,    82,    85,    83,   382,    85,
      50,    49,   386,    29,   388,   389,   390,    54,    29,    29,
     431,    85,   370,   371,     3,   373,     5,   438,     7,     8,
       9,   442,    29,    12,    71,    72,    73,    29,    82,    18,
      19,    20,    21,    59,    81,    46,    29,    29,    59,    59,
      87,    88,    28,    32,    33,    34,    35,    36,    37,    75,
      27,    29,    59,    79,    75,    75,    82,    59,    79,    79,
      82,    82,    82,    30,    83,    83,    59,    59,    75,    83,
      82,    85,    79,    75,    83,    82,    85,    79,    77,    77,
      82,    59,    75,    75,    11,    74,    79,    79,    82,    82,
      82,    30,    83,    82,    85,    83,    83,    75,    85,    83,
      83,    79,    85,    82,    82,    41,    42,    43,    44,    45,
      41,    42,    43,    44,    45,    41,    42,    43,    44,    45,
      76,    77,    78,    79,    75,    76,    77,    78,    79,    76,
      77,    78,    79,    89,    90,    83,    47,    85,    83,    75,
      85,    83,    64,    85,    75,    83,    30,    85,    84,    75,
      83,    83,    85,    85,    83,    30,    85,    75,    75,    75,
      83,    82,    77,    82,    55,    27,    16,    48,    75,    83,
      83,    30,    30,    30,    30,    30,    30,    30,    30,    83,
      27,    83,    82,    31,    30,    30,    30,    83,    31,    83,
      85,    28,    27,    31,    82,    31,    83,    31,    31,    82,
      82,   161,    82,    82,    55,   180,    55,   252,   133,   102,
     350,   219,   304,   389,   157,    -1,    -1,   109
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    32,    33,    34,    35,    36,    37,    74,    82,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   102,   103,
       4,    26,     6,    26,     6,    26,    75,   150,    10,    13,
     150,    39,    40,   149,    52,   104,    22,    99,    99,     0,
      80,    15,    56,   144,    13,   150,   150,   150,   150,   150,
     150,    19,    81,    41,    42,    43,    44,    45,    75,    84,
     118,   119,   126,   128,   129,   150,   151,    99,    83,    16,
      51,    52,   101,    49,    50,   148,   150,    82,    82,    82,
      11,    82,    17,   113,    75,   122,   123,   151,    79,    13,
      46,    13,    85,    13,    85,    82,    86,    75,    77,   118,
     145,   146,   102,    77,    77,   105,   107,   151,   106,   151,
     106,    82,   110,   106,    29,    59,    82,   112,   114,   115,
     116,   117,   118,    85,   113,    81,    70,    82,   131,   132,
     134,   150,    75,   131,   118,   126,   131,    75,   128,    52,
      84,   118,   151,     8,    14,   147,   147,    85,    50,    49,
      83,    85,    23,    24,    25,   108,    83,    85,    83,    76,
      77,    78,    79,   109,   111,    85,    83,   117,    82,   114,
      28,    27,    29,    54,    57,    58,    60,    71,    72,    73,
      81,    87,    88,   120,   123,   111,   151,    82,    82,    99,
     134,   113,    30,    61,    62,    63,    65,    66,    67,    68,
      69,    85,    46,    75,   130,   113,    46,    13,   113,    82,
     118,   127,    83,    83,   146,    77,    77,   107,    82,   151,
      83,    85,    82,    11,    99,    83,   116,   117,    57,    58,
      60,    29,    55,   111,   111,   118,   121,    82,   121,    89,
      90,   111,   124,   125,    99,    99,    83,    83,    47,   136,
     132,    64,    68,    69,   135,    68,    69,   135,    30,    30,
     135,    30,    61,    62,    63,    66,    30,    30,   132,    75,
     136,    75,   131,   136,   127,    83,    85,   130,   130,    77,
     111,   109,    82,    83,   111,   121,    82,    55,    27,    99,
     109,   111,   111,   125,    83,    83,    46,    75,   133,    16,
      48,   143,    31,    53,    30,    30,    30,    30,    30,    30,
     132,   132,    30,   132,   135,   135,    30,   135,   132,   132,
     143,   113,   143,    83,   130,   118,    83,    83,   109,    27,
      99,   109,   121,    83,    83,   133,    75,   118,   137,    29,
      82,   118,   128,   138,   139,   140,   141,   142,    79,   114,
      82,   132,   132,   132,   132,   132,   132,    31,    53,   132,
      30,    30,   132,    30,    31,    31,   136,    83,    83,   121,
      83,    83,    85,   142,   139,    54,   120,    54,   120,    28,
      27,   106,    31,    31,    31,    53,    31,    31,    31,    53,
      79,   114,    82,    31,    53,   132,   132,   132,    79,   114,
      79,   114,   143,   130,   118,    83,    29,    55,   121,    29,
      55,   121,   141,   142,    83,    79,   114,    79,   114,    79,
     114,    82,    79,   114,    79,   114,    79,   114,    82,   106,
      79,   114,    82,    55,    55,   106,   106,    83,   106,    83,
      83,    83
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    91,    92,    92,    92,    92,    93,    93,    93,    93,
      93,    94,    94,    94,    94,    95,    95,    96,    97,    97,
      97,    97,    97,    98,    98,    98,    98,    98,    98,    98,
      99,   100,   100,   101,   101,   101,   102,   102,   103,   103,
     103,   103,   104,   104,   105,   105,   106,   106,   107,   108,
     108,   108,   109,   109,   110,   110,   111,   111,   111,   111,
     112,   112,   112,   112,   112,   112,   112,   112,   112,   112,
     112,   112,   113,   113,   114,   115,   115,   116,   116,   117,
     117,   117,   118,   118,   119,   119,   119,   119,   120,   120,
     120,   120,   120,   120,   121,   121,   122,   122,   123,   123,
     123,   124,   124,   125,   125,   125,   126,   126,   127,   127,
     128,   128,   128,   128,   129,   129,   129,   129,   129,   129,
     130,   130,   130,   131,   132,   132,   132,   132,   133,   133,
     134,   134,   134,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   134,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   134,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   134,   134,   134,   134,   135,   135,   136,   136,
     137,   137,   138,   138,   138,   138,   138,   138,   139,   140,
     140,   141,   141,   142,   142,   142,   143,   143,   144,   144,
     145,   145,   146,   146,   147,   147,   147,   148,   148,   148,
     148,   148,   149,   149,   150,   151
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     5,    10,     4,     5,     1,     2,     3,
       3,     1,     4,     0,     1,     1,     1,     3,     8,     8,
       8,    10,     0,     1,     1,     3,     1,     3,     2,     1,
       4,     1,     1,     3,     3,     5,     1,     1,     1,     1,
       3,     3,     4,     5,     6,     5,     6,     5,     6,     4,
       3,     4,     0,     2,     1,     3,     1,     3,     1,     2,
       3,     1,     3,     1,     1,     3,     3,     5,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     3,     3,
       4,     1,     2,     1,     2,     2,     1,     3,     1,     3,
       5,     5,     6,     8,     1,     1,     1,     1,     1,     1,
       0,     2,     1,     1,     2,     3,     4,     5,     1,     2,
       1,     5,     5,     7,     3,     6,     6,     8,     7,     7,
       9,     7,     7,     9,     7,     7,     9,     4,     4,     5,
       6,     6,     6,     6,     6,     7,     7,     7,     7,     6,
       6,     7,     7,     7,     7,     3,     0,     1,     0,     3,
       1,     3,     3,     3,     3,     4,     3,     4,     1,     3,
       1,     3,     1,     2,     3,     1,     0,     2,     3,     0,
       1,     3,     2,     2,     1,     1,     0,     0,     2,     2,
       4,     4,     1,     1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* start: stmt ';'  */
#line 155 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 2040 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 160 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 2049 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 165 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 2058 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 170 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 2067 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 186 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 2075 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 190 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 2083 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 194 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 2091 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 198 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 2099 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 205 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 2107 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 209 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 2115 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 216 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 2123 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 223 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 2131 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 227 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 2139 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 231 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 2147 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 235 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2155 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 239 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2163 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 246 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 2171 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 250 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 2179 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 254 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_bool_expr));
    }
#line 2187 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 258 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_bool_expr));
    }
#line 2195 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: query_expression  */
#line 262 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_query);
    }
#line 2203 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN query_expression  */
#line 266 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_query), false);
    }
#line 2211 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE query_expression  */
#line 270 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_query), true);
    }
#line 2219 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 30: /* query_expression: union_expression opt_order_clause limit_offset_clause  */
#line 277 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_query)->set_tail((yyvsp[-1].sv_orderbys), (yyvsp[0].sv_limit_offset).first, (yyvsp[0].sv_limit_offset).second);
        (yyval.sv_query) = (yyvsp[-2].sv_query);
    }
#line 2228 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 31: /* union_expression: query_primary  */
#line 285 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = (yyvsp[0].sv_query);
    }
#line 2236 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 32: /* union_expression: union_expression UNION union_quantifier query_primary  */
#line 289 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = append_set_operand((yyvsp[-3].sv_query), (yyvsp[-2].sv_set_op), (yyvsp[-1].sv_bool), (yyvsp[0].sv_query));
    }
#line 2244 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 33: /* union_quantifier: %empty  */
#line 296 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 2252 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 34: /* union_quantifier: DISTINCT  */
#line 300 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 2260 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 35: /* union_quantifier: ALL  */
#line 304 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = true;
    }
#line 2268 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 36: /* query_primary: select_core  */
#line 311 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = (yyvsp[0].sv_select);
    }
#line 2276 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 37: /* query_primary: '(' query_expression ')'  */
#line 315 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = std::make_shared<QueryGroup>((yyvsp[-1].sv_query));
    }
#line 2284 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 38: /* select_core: SELECT opt_select_distinct '*' FROM from_clause optWhereClause opt_group_by opt_having  */
#line 322 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0, (yyvsp[-6].sv_bool));
    }
#line 2292 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 39: /* select_core: SELECT opt_select_distinct colList FROM from_clause optWhereClause opt_group_by opt_having  */
#line 326 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-5].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0, (yyvsp[-6].sv_bool));
    }
#line 2300 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 40: /* select_core: SELECT opt_select_distinct agg_list FROM from_clause optWhereClause opt_group_by opt_having  */
#line 330 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-5].sv_agg_exprs), (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0, (yyvsp[-6].sv_bool));
    }
#line 2308 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 41: /* select_core: SELECT opt_select_distinct colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having  */
#line 334 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), (yyvsp[-5].sv_agg_exprs), (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0, (yyvsp[-8].sv_bool));
    }
#line 2316 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 42: /* opt_select_distinct: %empty  */
#line 340 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                    { (yyval.sv_bool) = false; }
#line 2322 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 43: /* opt_select_distinct: DISTINCT  */
#line 341 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                    { (yyval.sv_bool) = true; }
#line 2328 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 44: /* fieldList: field  */
#line 346 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2336 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 45: /* fieldList: fieldList ',' field  */
#line 350 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2344 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 46: /* colNameList: colName  */
#line 357 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2352 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 47: /* colNameList: colNameList ',' colName  */
#line 361 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2360 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 48: /* field: colName type  */
#line 368 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2368 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 49: /* type: INT  */
#line 375 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2376 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 50: /* type: CHAR '(' VALUE_INT ')'  */
#line 379 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2384 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 51: /* type: FLOAT  */
#line 383 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2392 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 52: /* valueList: value  */
#line 390 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2400 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 53: /* valueList: valueList ',' value  */
#line 394 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2408 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 54: /* valueRows: '(' valueList ')'  */
#line 401 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2416 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 55: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 405 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2425 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_INT  */
#line 413 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2433 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 57: /* value: VALUE_FLOAT  */
#line 417 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2441 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 58: /* value: VALUE_STRING  */
#line 421 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2449 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 59: /* value: VALUE_BOOL  */
#line 425 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2457 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 60: /* condition: col op expr  */
#line 432 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2465 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 61: /* condition: col LIKE value  */
#line 436 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), SV_OP_LIKE,
                                          std::static_pointer_cast<Expr>((yyvsp[0].sv_val)));
    }
#line 2474 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 62: /* condition: col NOT LIKE value  */
#line 441 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>(std::make_shared<BinaryExpr>(
            (yyvsp[-3].sv_col), SV_OP_LIKE, std::static_pointer_cast<Expr>((yyvsp[0].sv_val))));
    }
#line 2483 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 63: /* condition: col BETWEEN expr AND expr  */
#line 446 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_between_expr((yyvsp[-4].sv_col), (yyvsp[-2].sv_expr), (yyvsp[0].sv_expr), false);
    }
#line 2491 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 64: /* condition: col NOT BETWEEN expr AND expr  */
#line 450 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_between_expr((yyvsp[-5].sv_col), (yyvsp[-2].sv_expr), (yyvsp[0].sv_expr), true);
    }
#line 2499 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 65: /* condition: col IN '(' valueList ')'  */
#line 454 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_in_list_expr((yyvsp[-4].sv_col), (yyvsp[-1].sv_vals), false);
    }
#line 2507 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 66: /* condition: col NOT IN '(' valueList ')'  */
#line 458 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_in_list_expr((yyvsp[-5].sv_col), (yyvsp[-1].sv_vals), true);
    }
#line 2515 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 67: /* condition: col IN '(' query_expression ')'  */
#line 462 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::IN, (yyvsp[-4].sv_col), (yyvsp[-1].sv_query));
    }
#line 2523 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 68: /* condition: col NOT IN '(' query_expression ')'  */
#line 466 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>(std::make_shared<SubqueryPredicate>(
            SubqueryPredicateType::IN, (yyvsp[-5].sv_col), (yyvsp[-1].sv_query)));
    }
#line 2532 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 69: /* condition: EXISTS '(' query_expression ')'  */
#line 471 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::EXISTS,
                                                 nullptr, (yyvsp[-1].sv_query));
    }
#line 2541 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 70: /* condition: col IS NULL_T  */
#line 476 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), SV_OP_IS_NULL, nullptr);
    }
#line 2549 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 71: /* condition: col IS NOT NULL_T  */
#line 480 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-3].sv_col), SV_OP_IS_NOT_NULL, nullptr);
    }
#line 2557 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 72: /* optWhereClause: %empty  */
#line 487 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 2565 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 73: /* optWhereClause: WHERE whereClause  */
#line 491 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2573 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 74: /* whereClause: where_or_expr  */
#line 498 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2581 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 75: /* where_or_expr: where_or_expr OR where_and_expr  */
#line 505 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2589 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 76: /* where_or_expr: where_and_expr  */
#line 509 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2597 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 77: /* where_and_expr: where_and_expr AND where_not_expr  */
#line 516 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2605 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 78: /* where_and_expr: where_not_expr  */
#line 520 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2613 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 79: /* where_not_expr: NOT where_not_expr  */
#line 527 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 2621 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 80: /* where_not_expr: '(' whereClause ')'  */
#line 531 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 2629 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 81: /* where_not_expr: condition  */
#line 535 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2637 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 82: /* col: tbName '.' colName  */
#line 542 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2645 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 83: /* col: colName  */
#line 546 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2653 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 84: /* colList: col  */
#line 553 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2661 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 85: /* colList: colList ',' col  */
#line 557 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2669 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 86: /* colList: col AS IDENTIFIER  */
#line 561 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2679 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 87: /* colList: colList ',' col AS IDENTIFIER  */
#line 567 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2688 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 88: /* op: '='  */
#line 575 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2696 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 89: /* op: '<'  */
#line 579 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2704 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 90: /* op: '>'  */
#line 583 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2712 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 91: /* op: NEQ  */
#line 587 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2720 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 92: /* op: LEQ  */
#line 591 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2728 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 93: /* op: GEQ  */
#line 595 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2736 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 94: /* expr: value  */
#line 602 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2744 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 95: /* expr: col  */
#line 606 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2752 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 96: /* setClauses: setClause  */
#line 613 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2760 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 97: /* setClauses: setClauses ',' setClause  */
#line 617 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2768 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 98: /* setClause: colName '=' value  */
#line 624 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2776 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 99: /* setClause: colName '=' colName  */
#line 628 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2788 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 100: /* setClause: colName '=' colName arith_chain  */
#line 636 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_vals)[0], false);
        if ((yyvsp[0].sv_vals).size() > 1) (yyval.sv_set_clause)->chain = (yyvsp[0].sv_vals);
    }
#line 2800 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 101: /* arith_chain: arith_term  */
#line 647 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2808 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 102: /* arith_chain: arith_chain arith_term  */
#line 651 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2816 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 103: /* arith_term: value  */
#line 658 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2825 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 104: /* arith_term: '+' value  */
#line 663 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2833 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 105: /* arith_term: '-' value  */
#line 667 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = negate_value((yyvsp[0].sv_val));
        if ((yyval.sv_val) == nullptr) YYERROR;
    }
#line 2842 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 106: /* agg_list: agg_func  */
#line 677 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2850 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 107: /* agg_list: agg_list ',' agg_func  */
#line 681 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2858 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 108: /* agg_col_list: col  */
#line 688 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2866 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 109: /* agg_col_list: agg_col_list ',' col  */
#line 692 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2874 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 110: /* agg_func: agg_name '(' '*' ')' opt_alias  */
#line 699 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), nullptr, (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2883 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 111: /* agg_func: agg_name '(' col ')' opt_alias  */
#line 704 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str));
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2892 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 112: /* agg_func: agg_name '(' DISTINCT agg_col_list ')' opt_alias  */
#line 709 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-5].sv_str), std::move((yyvsp[-2].sv_cols)), (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2901 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 113: /* agg_func: agg_name '(' DISTINCT '(' agg_col_list ')' ')' opt_alias  */
#line 714 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* PostgreSQL/MySQL-compatible parenthesized DISTINCT argument list. */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-7].sv_str), std::move((yyvsp[-3].sv_cols)), (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2911 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 114: /* agg_name: COUNT  */
#line 722 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "count"; }
#line 2917 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 115: /* agg_name: MAX  */
#line 723 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "max"; }
#line 2923 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 116: /* agg_name: MIN  */
#line 724 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "min"; }
#line 2929 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 117: /* agg_name: SUM  */
#line 725 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "sum"; }
#line 2935 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 118: /* agg_name: AVG  */
#line 726 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "avg"; }
#line 2941 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 119: /* agg_name: IDENTIFIER  */
#line 728 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (find_aggregate((yyvsp[0].sv_str)) == nullptr) YYERROR;
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2950 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 120: /* opt_alias: %empty  */
#line 736 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2958 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 121: /* opt_alias: AS IDENTIFIER  */
#line 740 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2966 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 122: /* opt_alias: IDENTIFIER  */
#line 744 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2974 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 123: /* from_clause: joined_table  */
#line 751 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2982 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 124: /* table_ref: tbName opt_alias  */
#line 759 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<TableRef>((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2990 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 125: /* table_ref: '(' joined_table ')'  */
#line 763 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[-1].sv_from);
    }
#line 2998 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 126: /* table_ref: '(' query_expression ')' required_alias  */
#line 767 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<DerivedTableRef>((yyvsp[-2].sv_query), (yyvsp[0].sv_str));
    }
#line 3006 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 127: /* table_ref: LATERAL '(' query_expression ')' required_alias  */
#line 771 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<LateralRef>((yyvsp[-2].sv_query), (yyvsp[0].sv_str));
    }
#line 3014 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 128: /* required_alias: IDENTIFIER  */
#line 778 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 3022 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 129: /* required_alias: AS IDENTIFIER  */
#line 782 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 3030 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 130: /* joined_table: table_ref  */
#line 790 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 3038 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 131: /* joined_table: joined_table JOIN table_ref ON whereClause  */
#line 794 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3047 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 132: /* joined_table: joined_table JOIN table_ref ON VALUE_BOOL  */
#line 799 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3058 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 133: /* joined_table: joined_table JOIN table_ref USING '(' colNameList ')'  */
#line 806 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-6].sv_from), (yyvsp[-4].sv_from), nullptr, false, is_lateral_ref((yyvsp[-4].sv_from)), false, (yyvsp[-1].sv_strs));
    }
#line 3067 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 134: /* joined_table: joined_table JOIN table_ref  */
#line 811 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3077 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 135: /* joined_table: joined_table INNER JOIN table_ref ON whereClause  */
#line 817 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3086 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 136: /* joined_table: joined_table INNER JOIN table_ref ON VALUE_BOOL  */
#line 822 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3097 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 137: /* joined_table: joined_table INNER JOIN table_ref USING '(' colNameList ')'  */
#line 829 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-7].sv_from), (yyvsp[-4].sv_from), nullptr, false, is_lateral_ref((yyvsp[-4].sv_from)), false, (yyvsp[-1].sv_strs));
    }
#line 3106 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 138: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON whereClause  */
#line 834 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3115 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 139: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 839 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3126 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 140: /* joined_table: joined_table LEFT opt_outer JOIN table_ref USING '(' colNameList ')'  */
#line 846 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-8].sv_from), (yyvsp[-4].sv_from), nullptr, false, is_lateral_ref((yyvsp[-4].sv_from)), false, (yyvsp[-1].sv_strs));
    }
#line 3135 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 141: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON whereClause  */
#line 851 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3144 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 142: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 856 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3155 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 143: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref USING '(' colNameList ')'  */
#line 863 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-8].sv_from), (yyvsp[-4].sv_from), nullptr, false, is_lateral_ref((yyvsp[-4].sv_from)), false, (yyvsp[-1].sv_strs));
    }
#line 3164 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 144: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON whereClause  */
#line 868 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3173 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 145: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 873 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3184 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 146: /* joined_table: joined_table FULL opt_outer JOIN table_ref USING '(' colNameList ')'  */
#line 880 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-8].sv_from), (yyvsp[-4].sv_from), nullptr, false, is_lateral_ref((yyvsp[-4].sv_from)), false, (yyvsp[-1].sv_strs));
    }
#line 3193 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 147: /* joined_table: joined_table CROSS JOIN table_ref  */
#line 885 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3203 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 148: /* joined_table: joined_table NATURAL JOIN table_ref  */
#line 891 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3213 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 149: /* joined_table: joined_table NATURAL INNER JOIN table_ref  */
#line 897 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3223 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 150: /* joined_table: joined_table NATURAL LEFT opt_outer JOIN table_ref  */
#line 903 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3233 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 151: /* joined_table: joined_table NATURAL RIGHT opt_outer JOIN table_ref  */
#line 909 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3243 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 152: /* joined_table: joined_table NATURAL FULL opt_outer JOIN table_ref  */
#line 915 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3253 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 153: /* joined_table: joined_table SEMI JOIN table_ref ON whereClause  */
#line 921 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3262 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 154: /* joined_table: joined_table SEMI JOIN table_ref ON VALUE_BOOL  */
#line 926 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3273 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 155: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON whereClause  */
#line 933 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3282 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 156: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 938 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3293 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 157: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON whereClause  */
#line 945 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3302 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 158: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 950 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3313 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 159: /* joined_table: joined_table ANTI JOIN table_ref ON whereClause  */
#line 957 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3322 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 160: /* joined_table: joined_table ANTI JOIN table_ref ON VALUE_BOOL  */
#line 962 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3333 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 161: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON whereClause  */
#line 969 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3342 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 162: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 974 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3353 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 163: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON whereClause  */
#line 981 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3362 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 164: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 986 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3373 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 165: /* joined_table: joined_table ',' table_ref  */
#line 993 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3383 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 168: /* opt_group_by: %empty  */
#line 1008 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 3391 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 169: /* opt_group_by: GROUP BY group_by_list  */
#line 1012 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 3399 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 170: /* group_by_list: col  */
#line 1019 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 3407 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 171: /* group_by_list: group_by_list ',' col  */
#line 1023 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 3415 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 172: /* having_condition: col op expr  */
#line 1030 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3423 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 173: /* having_condition: agg_func op expr  */
#line 1034 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_agg_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3431 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 174: /* having_condition: col IS NULL_T  */
#line 1038 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), SV_OP_IS_NULL, nullptr);
    }
#line 3439 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 175: /* having_condition: col IS NOT NULL_T  */
#line 1042 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-3].sv_col), SV_OP_IS_NOT_NULL, nullptr);
    }
#line 3447 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 176: /* having_condition: agg_func IS NULL_T  */
#line 1046 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_agg_expr), SV_OP_IS_NULL, nullptr);
    }
#line 3455 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 177: /* having_condition: agg_func IS NOT NULL_T  */
#line 1050 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-3].sv_agg_expr), SV_OP_IS_NOT_NULL, nullptr);
    }
#line 3463 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 178: /* having_clause: having_or_expr  */
#line 1057 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3471 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 179: /* having_or_expr: having_or_expr OR having_and_expr  */
#line 1064 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3479 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 180: /* having_or_expr: having_and_expr  */
#line 1068 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3487 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 181: /* having_and_expr: having_and_expr AND having_not_expr  */
#line 1075 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3495 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 182: /* having_and_expr: having_not_expr  */
#line 1079 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3503 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 183: /* having_not_expr: NOT having_not_expr  */
#line 1086 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 3511 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 184: /* having_not_expr: '(' having_clause ')'  */
#line 1090 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 3519 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 185: /* having_not_expr: having_condition  */
#line 1094 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3527 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 186: /* opt_having: %empty  */
#line 1101 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 3535 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 187: /* opt_having: HAVING having_clause  */
#line 1105 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3543 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 188: /* opt_order_clause: ORDER BY order_list  */
#line 1112 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 3551 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 189: /* opt_order_clause: %empty  */
#line 1116 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 3559 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 190: /* order_list: order_clause  */
#line 1123 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 3567 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 191: /* order_list: order_list ',' order_clause  */
#line 1127 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 3575 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 192: /* order_clause: col opt_asc_desc  */
#line 1134 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 3583 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 193: /* order_clause: VALUE_INT opt_asc_desc  */
#line 1138 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_int), (yyvsp[0].sv_orderby_dir));
    }
#line 3591 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 194: /* opt_asc_desc: ASC  */
#line 1144 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 3597 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 195: /* opt_asc_desc: DESC  */
#line 1145 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 3603 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 196: /* opt_asc_desc: %empty  */
#line 1146 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 3609 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 197: /* limit_offset_clause: %empty  */
#line 1151 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_limit_offset) = {-1, -1};
    }
#line 3617 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 198: /* limit_offset_clause: LIMIT VALUE_INT  */
#line 1155 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if ((yyvsp[0].sv_int) < 0) YYERROR;
        (yyval.sv_limit_offset) = {(yyvsp[0].sv_int), -1};
    }
#line 3626 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 199: /* limit_offset_clause: OFFSET VALUE_INT  */
#line 1160 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if ((yyvsp[0].sv_int) < 0) YYERROR;
        (yyval.sv_limit_offset) = {-1, (yyvsp[0].sv_int)};
    }
#line 3635 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 200: /* limit_offset_clause: LIMIT VALUE_INT OFFSET VALUE_INT  */
#line 1165 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if ((yyvsp[-2].sv_int) < 0 || (yyvsp[0].sv_int) < 0) YYERROR;
        (yyval.sv_limit_offset) = {(yyvsp[-2].sv_int), (yyvsp[0].sv_int)};
    }
#line 3644 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 201: /* limit_offset_clause: OFFSET VALUE_INT LIMIT VALUE_INT  */
#line 1170 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if ((yyvsp[-2].sv_int) < 0 || (yyvsp[0].sv_int) < 0) YYERROR;
        (yyval.sv_limit_offset) = {(yyvsp[0].sv_int), (yyvsp[-2].sv_int)};
    }
#line 3653 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 202: /* set_knob_type: ENABLE_NESTLOOP  */
#line 1177 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 3659 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 203: /* set_knob_type: ENABLE_SORTMERGE  */
#line 1178 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 3665 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;


#line 3669 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 1184 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
