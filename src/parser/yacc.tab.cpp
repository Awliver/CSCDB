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

#line 146 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

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
  YYSYMBOL_UNION = 50,                     /* UNION  */
  YYSYMBOL_ALL = 51,                       /* ALL  */
  YYSYMBOL_DISTINCT = 52,                  /* DISTINCT  */
  YYSYMBOL_LIKE = 53,                      /* LIKE  */
  YYSYMBOL_BETWEEN = 54,                   /* BETWEEN  */
  YYSYMBOL_EXISTS = 55,                    /* EXISTS  */
  YYSYMBOL_IN = 56,                        /* IN  */
  YYSYMBOL_LEFT = 57,                      /* LEFT  */
  YYSYMBOL_RIGHT = 58,                     /* RIGHT  */
  YYSYMBOL_INNER = 59,                     /* INNER  */
  YYSYMBOL_OUTER = 60,                     /* OUTER  */
  YYSYMBOL_CROSS = 61,                     /* CROSS  */
  YYSYMBOL_FULL = 62,                      /* FULL  */
  YYSYMBOL_NATURAL = 63,                   /* NATURAL  */
  YYSYMBOL_SEMI = 64,                      /* SEMI  */
  YYSYMBOL_ANTI = 65,                      /* ANTI  */
  YYSYMBOL_LATERAL = 66,                   /* LATERAL  */
  YYSYMBOL_LEQ = 67,                       /* LEQ  */
  YYSYMBOL_NEQ = 68,                       /* NEQ  */
  YYSYMBOL_GEQ = 69,                       /* GEQ  */
  YYSYMBOL_T_EOF = 70,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 71,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 72,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 73,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 74,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 75,                /* VALUE_BOOL  */
  YYSYMBOL_76_ = 76,                       /* ';'  */
  YYSYMBOL_77_ = 77,                       /* '='  */
  YYSYMBOL_78_ = 78,                       /* '('  */
  YYSYMBOL_79_ = 79,                       /* ')'  */
  YYSYMBOL_80_ = 80,                       /* '*'  */
  YYSYMBOL_81_ = 81,                       /* ','  */
  YYSYMBOL_82_ = 82,                       /* '.'  */
  YYSYMBOL_83_ = 83,                       /* '<'  */
  YYSYMBOL_84_ = 84,                       /* '>'  */
  YYSYMBOL_85_ = 85,                       /* '+'  */
  YYSYMBOL_86_ = 86,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 87,                  /* $accept  */
  YYSYMBOL_start = 88,                     /* start  */
  YYSYMBOL_stmt = 89,                      /* stmt  */
  YYSYMBOL_txnStmt = 90,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 91,                    /* dbStmt  */
  YYSYMBOL_setStmt = 92,                   /* setStmt  */
  YYSYMBOL_ddl = 93,                       /* ddl  */
  YYSYMBOL_dml = 94,                       /* dml  */
  YYSYMBOL_query_expression = 95,          /* query_expression  */
  YYSYMBOL_union_expression = 96,          /* union_expression  */
  YYSYMBOL_union_quantifier = 97,          /* union_quantifier  */
  YYSYMBOL_query_primary = 98,             /* query_primary  */
  YYSYMBOL_select_core = 99,               /* select_core  */
  YYSYMBOL_fieldList = 100,                /* fieldList  */
  YYSYMBOL_colNameList = 101,              /* colNameList  */
  YYSYMBOL_field = 102,                    /* field  */
  YYSYMBOL_type = 103,                     /* type  */
  YYSYMBOL_valueList = 104,                /* valueList  */
  YYSYMBOL_valueRows = 105,                /* valueRows  */
  YYSYMBOL_value = 106,                    /* value  */
  YYSYMBOL_condition = 107,                /* condition  */
  YYSYMBOL_optWhereClause = 108,           /* optWhereClause  */
  YYSYMBOL_whereClause = 109,              /* whereClause  */
  YYSYMBOL_where_or_expr = 110,            /* where_or_expr  */
  YYSYMBOL_where_and_expr = 111,           /* where_and_expr  */
  YYSYMBOL_where_not_expr = 112,           /* where_not_expr  */
  YYSYMBOL_col = 113,                      /* col  */
  YYSYMBOL_colList = 114,                  /* colList  */
  YYSYMBOL_op = 115,                       /* op  */
  YYSYMBOL_expr = 116,                     /* expr  */
  YYSYMBOL_setClauses = 117,               /* setClauses  */
  YYSYMBOL_setClause = 118,                /* setClause  */
  YYSYMBOL_arith_chain = 119,              /* arith_chain  */
  YYSYMBOL_arith_term = 120,               /* arith_term  */
  YYSYMBOL_agg_list = 121,                 /* agg_list  */
  YYSYMBOL_agg_func = 122,                 /* agg_func  */
  YYSYMBOL_agg_name = 123,                 /* agg_name  */
  YYSYMBOL_opt_alias = 124,                /* opt_alias  */
  YYSYMBOL_from_clause = 125,              /* from_clause  */
  YYSYMBOL_table_ref = 126,                /* table_ref  */
  YYSYMBOL_required_alias = 127,           /* required_alias  */
  YYSYMBOL_joined_table = 128,             /* joined_table  */
  YYSYMBOL_opt_outer = 129,                /* opt_outer  */
  YYSYMBOL_opt_group_by = 130,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 131,            /* group_by_list  */
  YYSYMBOL_having_condition = 132,         /* having_condition  */
  YYSYMBOL_having_clause = 133,            /* having_clause  */
  YYSYMBOL_having_or_expr = 134,           /* having_or_expr  */
  YYSYMBOL_having_and_expr = 135,          /* having_and_expr  */
  YYSYMBOL_having_not_expr = 136,          /* having_not_expr  */
  YYSYMBOL_opt_having = 137,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 138,         /* opt_order_clause  */
  YYSYMBOL_order_list = 139,               /* order_list  */
  YYSYMBOL_order_clause = 140,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 141,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit = 142,                /* opt_limit  */
  YYSYMBOL_set_knob_type = 143,            /* set_knob_type  */
  YYSYMBOL_tbName = 144,                   /* tbName  */
  YYSYMBOL_colName = 145                   /* colName  */
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
#define YYFINAL  61
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   518

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  87
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  59
/* YYNRULES -- Number of rules.  */
#define YYNRULES  187
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  409

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   330


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
      78,    79,    80,    85,    81,    86,    82,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    76,
      83,    77,    84,     2,     2,     2,     2,     2,     2,     2,
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
      75
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   133,   133,   138,   143,   148,   156,   157,   158,   159,
     160,   164,   168,   172,   176,   183,   187,   194,   201,   205,
     209,   213,   217,   224,   228,   232,   236,   240,   244,   248,
     255,   263,   267,   275,   278,   282,   289,   293,   300,   304,
     308,   312,   319,   323,   330,   334,   341,   348,   352,   356,
     363,   367,   374,   378,   386,   390,   394,   398,   405,   409,
     414,   419,   423,   427,   431,   435,   439,   444,   453,   456,
     463,   470,   474,   481,   485,   492,   496,   500,   507,   511,
     518,   522,   526,   532,   540,   544,   548,   552,   556,   560,
     567,   571,   578,   582,   589,   593,   601,   612,   616,   623,
     628,   632,   642,   646,   653,   658,   663,   669,   678,   679,
     680,   681,   682,   683,   692,   695,   699,   706,   714,   718,
     722,   726,   733,   737,   745,   749,   754,   761,   767,   772,
     779,   784,   791,   796,   803,   808,   815,   821,   827,   833,
     839,   845,   851,   856,   863,   868,   875,   880,   887,   892,
     899,   904,   911,   916,   923,   931,   933,   939,   942,   949,
     953,   960,   964,   971,   978,   982,   989,   993,  1000,  1004,
    1008,  1016,  1019,  1026,  1031,  1037,  1041,  1048,  1052,  1059,
    1060,  1061,  1066,  1069,  1077,  1078,  1081,  1083
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
  "HAVING", "LIMIT", "UNION", "ALL", "DISTINCT", "LIKE", "BETWEEN",
  "EXISTS", "IN", "LEFT", "RIGHT", "INNER", "OUTER", "CROSS", "FULL",
  "NATURAL", "SEMI", "ANTI", "LATERAL", "LEQ", "NEQ", "GEQ", "T_EOF",
  "IDENTIFIER", "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT", "VALUE_BOOL",
  "';'", "'='", "'('", "')'", "'*'", "','", "'.'", "'<'", "'>'", "'+'",
  "'-'", "$accept", "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl",
  "dml", "query_expression", "union_expression", "union_quantifier",
  "query_primary", "select_core", "fieldList", "colNameList", "field",
  "type", "valueList", "valueRows", "value", "condition", "optWhereClause",
  "whereClause", "where_or_expr", "where_and_expr", "where_not_expr",
  "col", "colList", "op", "expr", "setClauses", "setClause", "arith_chain",
  "arith_term", "agg_list", "agg_func", "agg_name", "opt_alias",
  "from_clause", "table_ref", "required_alias", "joined_table",
  "opt_outer", "opt_group_by", "group_by_list", "having_condition",
  "having_clause", "having_or_expr", "having_and_expr", "having_not_expr",
  "opt_having", "opt_order_clause", "order_list", "order_clause",
  "opt_asc_desc", "opt_limit", "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-297)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-187)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     189,    83,   107,   139,    -3,    69,   119,    -3,   114,   399,
      22,  -297,  -297,  -297,  -297,  -297,  -297,  -297,    28,   141,
      82,  -297,  -297,  -297,  -297,  -297,  -297,    35,  -297,  -297,
    -297,   160,    -3,    -3,    -3,    -3,  -297,  -297,    -3,    -3,
     162,  -297,  -297,   116,  -297,  -297,  -297,  -297,  -297,   -24,
     186,   156,     8,    16,  -297,   128,   129,  -297,    28,  -297,
     148,  -297,  -297,   214,     4,   185,    -3,   172,   182,  -297,
     183,    14,   254,   203,   200,    85,   208,    85,   269,    85,
     277,   -14,   203,  -297,  -297,   180,  -297,  -297,    28,   209,
    -297,  -297,   203,   203,   203,   225,   203,   109,  -297,  -297,
      23,  -297,   212,  -297,   250,    -4,   254,  -297,   304,    15,
    -297,   254,   253,    21,   254,  -297,  -297,   -18,   257,   264,
     267,  -297,   158,   158,   255,  -297,  -297,  -297,   199,  -297,
     233,   211,  -297,   221,    43,   272,   256,   109,   281,   109,
    -297,  -297,   347,   355,  -297,   240,   203,  -297,   142,    28,
      -4,   325,   268,   363,    85,   125,   140,   382,   387,   362,
     190,   394,   397,    85,   337,  -297,  -297,   363,   358,    85,
     363,   383,   354,    15,    15,  -297,  -297,  -297,  -297,   180,
    -297,   203,  -297,   375,  -297,  -297,  -297,   203,  -297,  -297,
    -297,  -297,  -297,   302,  -297,   379,   445,  -297,    28,   380,
     109,   109,   115,    43,   377,   385,  -297,  -297,  -297,  -297,
    -297,  -297,   377,  -297,  -297,   191,   381,   386,    17,  -297,
     442,   416,   435,  -297,   437,   438,   439,   441,   443,   444,
      85,    85,   446,    85,   362,   362,   447,   362,    85,    85,
    -297,  -297,   416,  -297,   254,   416,   393,    15,  -297,  -297,
    -297,  -297,   402,  -297,  -297,    43,    43,   400,   401,  -297,
     355,  -297,    43,   377,   403,  -297,  -297,  -297,   455,    51,
    -297,    43,    43,  -297,   191,  -297,    17,    17,   412,  -297,
    -297,   383,   329,  -297,    36,    85,    85,    85,    85,    85,
      85,   453,  -297,    85,  -297,   456,   457,    85,   458,   454,
     459,  -297,   363,  -297,   410,  -297,   413,  -297,   312,    43,
    -297,  -297,   464,    51,   377,   414,   316,  -297,  -297,  -297,
    -297,  -297,  -297,   415,   329,   329,   378,   378,  -297,  -297,
     466,   468,  -297,  -297,  -297,   467,   469,   470,   471,   472,
     473,   226,   474,    85,    85,  -297,    85,   309,   321,   416,
      15,  -297,  -297,   332,   377,   418,   339,  -297,  -297,  -297,
     383,  -297,   420,   377,   377,   329,   329,   323,   331,   348,
     350,   359,   360,  -297,  -297,   361,  -297,  -297,  -297,  -297,
    -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297,
    -297,  -297,  -297,   468,  -297,  -297,  -297,  -297,  -297,  -297,
    -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       0,     9,     6,    10,     7,     8,    27,   174,    31,    36,
      15,     0,     0,     0,     0,     0,   186,    20,     0,     0,
       0,   184,   185,     0,   108,   109,   110,   111,   112,   187,
       0,    80,     0,     0,   102,     0,     0,    79,     0,    28,
       0,     1,     2,     0,    33,   182,     0,     0,     0,    19,
       0,     0,    68,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    29,    37,     0,    35,    34,     0,     0,
      30,    16,     0,     0,     0,     0,     0,     0,    25,   187,
      68,    92,     0,    17,     0,     0,    68,   124,   117,   114,
      82,    68,    81,     0,    68,   113,   103,     0,   187,     0,
       0,    78,   181,   181,   173,   175,    32,   183,     0,    42,
       0,     0,    44,     0,     0,    23,     0,     0,     0,     0,
      77,    69,    70,    72,    74,     0,     0,    26,     0,     0,
       0,     0,     0,   157,     0,   155,   155,     0,     0,   155,
       0,     0,     0,     0,     0,   116,   118,   157,     0,     0,
     157,     0,     0,   114,   114,   180,   179,   178,   177,     0,
      18,     0,    47,     0,    49,    46,    21,     0,    22,    56,
      54,    55,    57,     0,    50,     0,     0,    75,     0,     0,
       0,     0,     0,     0,     0,     0,    88,    87,    89,    84,
      85,    86,     0,    93,    94,    95,     0,     0,     0,   119,
       0,   171,   127,   156,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   155,   155,     0,   155,     0,     0,
     154,   115,   171,    83,    68,   171,     0,   114,   104,   105,
     176,    43,     0,    45,    52,     0,     0,     0,     0,    76,
      71,    73,     0,     0,     0,    59,    90,    91,     0,     0,
      58,     0,     0,    99,    96,    97,     0,    37,     0,   122,
     120,     0,     0,    38,     0,     0,     0,     0,     0,     0,
       0,     0,   136,     0,   137,     0,     0,     0,     0,     0,
       0,    39,   157,    40,     0,   106,     0,    51,     0,     0,
      67,    60,     0,     0,     0,     0,     0,   100,   101,    98,
     121,   123,   159,   158,     0,     0,     0,     0,   170,   172,
     163,   165,   167,   126,   125,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   138,     0,     0,     0,   171,
     114,    48,    53,     0,     0,     0,     0,    61,    65,    63,
       0,   168,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   129,   128,     0,   139,   140,   141,   143,
     142,   149,   148,    41,   107,    24,    62,    66,    64,   160,
     169,   161,   162,   164,   166,   145,   144,   151,   150,   131,
     130,   147,   146,   153,   152,   133,   132,   135,   134
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -297,  -297,  -297,  -297,  -297,  -297,  -297,  -297,    -7,  -297,
    -297,   419,  -297,  -297,   174,   327,  -297,  -174,  -297,  -125,
    -297,   -87,  -129,  -297,   306,   -96,    -9,  -297,   -82,  -204,
    -297,   364,  -297,   235,   433,     3,  -297,  -167,   -57,  -111,
     236,   -66,  -141,  -166,  -297,  -297,   188,  -297,   149,  -296,
    -228,  -297,  -297,   336,   395,  -297,  -297,    -2,   -47
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      88,    28,    29,   128,   131,   129,   185,   193,   135,   266,
     140,    98,   141,   142,   143,   144,   145,    52,   212,   268,
     100,   101,   274,   275,    53,   327,    55,   166,   106,   107,
     280,   108,   226,   221,   323,   328,   329,   330,   331,   332,
     283,    65,   124,   125,   177,    90,    43,    56,    57
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      51,   242,    37,    59,   245,    40,   248,   249,   270,   194,
     199,    60,    54,   147,   301,   229,     9,   303,   232,   153,
     111,    77,   114,   214,   167,    95,   102,   170,   361,    79,
      67,    68,    69,    70,   169,   121,    71,    72,   117,   152,
      97,   197,     9,   222,    58,   130,   132,   132,     9,   132,
      63,    83,   240,   118,  -113,    86,    87,   118,  -186,   312,
     171,   164,   104,   278,    91,   137,   119,    36,    36,   112,
     394,     9,   120,   109,   150,   109,   123,   109,   265,    38,
     305,    54,   308,   116,   152,    64,   165,    30,   279,    78,
     273,   138,    96,   295,   296,   316,   298,    80,   151,   102,
      18,   215,    80,   109,   146,   261,    18,   118,   172,    31,
     357,   333,   244,    32,   139,   189,   190,   191,   192,   291,
     292,   383,   294,   189,   190,   191,   192,   299,   300,    18,
     307,   194,    39,    33,   130,   353,   349,   311,   137,   356,
     253,    61,   216,   217,   194,    34,   317,   318,   109,   273,
     386,   104,   109,    41,    42,   334,    36,   302,    62,   391,
     392,   109,   246,   105,   138,    35,   175,   109,   262,   263,
     123,   264,   176,    66,   335,   336,   337,   338,   339,   340,
     118,    73,   342,   384,   194,   223,   345,   139,   194,   224,
     225,   258,     1,    74,     2,   267,     3,     4,     5,    75,
     223,     6,    76,   267,   227,   228,    81,     7,     8,     9,
      10,    82,   374,    99,   189,   190,   191,   192,   380,   382,
     233,    11,    12,    13,    14,    15,    16,    84,   109,   109,
      85,   109,   376,   377,    89,   378,   109,   109,   396,   398,
     400,   402,   404,   406,   363,   364,   408,   234,   235,   236,
      92,   118,   237,   122,   267,   137,   182,   183,   184,    17,
      93,    94,   315,   189,   190,   191,   192,    18,   133,   202,
     136,    97,   322,   326,    99,   103,   271,   272,   180,   110,
     181,   138,   127,   109,   109,   109,   109,   109,   109,   148,
     186,   109,   187,   203,   204,   109,   205,   118,   154,   168,
     188,   373,   187,   134,   139,   267,   355,   206,   207,   208,
      44,    45,    46,    47,    48,   326,   326,   209,    44,    45,
      46,    47,    48,   210,   211,   155,   156,   157,   149,   158,
     159,   160,   161,   162,   154,   196,   179,   187,   137,  -186,
      49,   109,   109,   173,   109,   267,   174,   219,   115,   163,
     137,   389,   137,   195,   267,   267,   326,   326,   324,   198,
     137,   155,   156,   157,   138,   158,   159,   160,   161,   162,
      44,    45,    46,    47,    48,   200,   138,   137,   138,   137,
     118,   254,   201,   255,   379,   163,   138,   139,   137,   137,
     137,   352,   118,   255,   118,   359,   381,   255,   395,   139,
      49,   139,   118,   138,   218,   138,   397,   325,   241,   139,
     220,   385,   230,   255,   138,   138,   138,   231,   388,   118,
     255,   118,   223,   399,   238,   401,   139,   239,   139,   243,
     118,   118,   118,   247,   403,   405,   407,   139,   139,   139,
      44,    45,    46,    47,    48,   206,   207,   208,   118,   189,
     190,   191,   192,   252,   118,   209,   257,   256,   281,   259,
     276,   210,   211,   269,   282,   277,   284,   285,   286,   287,
      49,   288,   304,   289,   290,   306,   293,   297,   309,    50,
     310,   313,   314,   321,   341,   347,   343,   344,   346,   350,
     348,   354,   351,   358,   365,   366,   360,   387,   367,   390,
     368,   369,   370,   371,   372,   375,   260,   126,   251,   319,
     213,   113,   320,   362,   393,   250,     0,     0,   178
};

static const yytype_int16 yycheck[] =
{
       9,   167,     4,    10,   170,     7,   173,   174,   212,   134,
     139,    18,     9,   100,   242,   156,    20,   245,   159,   106,
      77,    13,    79,   148,   111,    11,    73,   114,   324,    13,
      32,    33,    34,    35,    13,    82,    38,    39,    52,   105,
      17,   137,    20,   154,    22,    92,    93,    94,    20,    96,
      15,    58,   163,    71,    78,    51,    52,    71,    82,   263,
      78,    46,    66,    46,    66,    29,    80,    71,    71,    78,
     366,    20,    81,    75,    78,    77,    85,    79,   203,    10,
     247,    78,   256,    80,   150,    50,    71,     4,    71,    81,
     215,    55,    78,   234,   235,   269,   237,    81,   105,   146,
      78,   148,    81,   105,    81,   201,    78,    71,   117,    26,
     314,    75,   169,     6,    78,    72,    73,    74,    75,   230,
     231,   349,   233,    72,    73,    74,    75,   238,   239,    78,
     255,   256,    13,    26,   181,   309,   302,   262,    29,   313,
     187,     0,   149,   150,   269,     6,   271,   272,   150,   274,
     354,    66,   154,    39,    40,   284,    71,   244,    76,   363,
     364,   163,   171,    78,    55,    26,     8,   169,    53,    54,
     179,    56,    14,    13,   285,   286,   287,   288,   289,   290,
      71,    19,   293,   350,   309,    60,   297,    78,   313,    64,
      65,   198,     3,    77,     5,   204,     7,     8,     9,    13,
      60,    12,    46,   212,    64,    65,    78,    18,    19,    20,
      21,    82,   341,    71,    72,    73,    74,    75,   347,   348,
      30,    32,    33,    34,    35,    36,    37,    79,   230,   231,
      16,   233,   343,   344,    49,   346,   238,   239,   367,   368,
     369,   370,   371,   372,   326,   327,   375,    57,    58,    59,
      78,    71,    62,    73,   263,    29,    23,    24,    25,    70,
      78,    78,   269,    72,    73,    74,    75,    78,    94,    29,
      96,    17,   281,   282,    71,    75,    85,    86,    79,    71,
      81,    55,    73,   285,   286,   287,   288,   289,   290,    77,
      79,   293,    81,    53,    54,   297,    56,    71,    30,    46,
      79,    75,    81,    78,    78,   314,   313,    67,    68,    69,
      41,    42,    43,    44,    45,   324,   325,    77,    41,    42,
      43,    44,    45,    83,    84,    57,    58,    59,    78,    61,
      62,    63,    64,    65,    30,    79,    81,    81,    29,    82,
      71,   343,   344,    79,   346,   354,    79,    79,    71,    81,
      29,   360,    29,    81,   363,   364,   365,   366,    29,    78,
      29,    57,    58,    59,    55,    61,    62,    63,    64,    65,
      41,    42,    43,    44,    45,    28,    55,    29,    55,    29,
      71,    79,    27,    81,    75,    81,    55,    78,    29,    29,
      29,    79,    71,    81,    71,    79,    75,    81,    75,    78,
      71,    78,    71,    55,    79,    55,    75,    78,    71,    78,
      47,    79,    30,    81,    55,    55,    55,    30,    79,    71,
      81,    71,    60,    75,    30,    75,    78,    30,    78,    71,
      71,    71,    71,    79,    75,    75,    75,    78,    78,    78,
      41,    42,    43,    44,    45,    67,    68,    69,    71,    72,
      73,    74,    75,    78,    71,    77,    11,    78,    16,    79,
      79,    83,    84,    78,    48,    79,    31,    30,    30,    30,
      71,    30,    79,    30,    30,    73,    30,    30,    78,    80,
      79,    78,    27,    71,    31,    31,    30,    30,    30,    79,
      31,    27,    79,    79,    28,    27,    81,    79,    31,    79,
      31,    31,    31,    31,    31,    31,   200,    88,   181,   274,
     146,    78,   276,   325,   365,   179,    -1,    -1,   123
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    32,    33,    34,    35,    36,    37,    70,    78,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    98,    99,
       4,    26,     6,    26,     6,    26,    71,   144,    10,    13,
     144,    39,    40,   143,    41,    42,    43,    44,    45,    71,
      80,   113,   114,   121,   122,   123,   144,   145,    22,    95,
      95,     0,    76,    15,    50,   138,    13,   144,   144,   144,
     144,   144,   144,    19,    77,    13,    46,    13,    81,    13,
      81,    78,    82,    95,    79,    16,    51,    52,    97,    49,
     142,   144,    78,    78,    78,    11,    78,    17,   108,    71,
     117,   118,   145,    75,    66,    78,   125,   126,   128,   144,
      71,   125,   113,   121,   125,    71,   122,    52,    71,    80,
     113,   145,    73,   113,   139,   140,    98,    73,   100,   102,
     145,   101,   145,   101,    78,   105,   101,    29,    55,    78,
     107,   109,   110,   111,   112,   113,    81,   108,    77,    78,
      78,    95,   128,   108,    30,    57,    58,    59,    61,    62,
      63,    64,    65,    81,    46,    71,   124,   108,    46,    13,
     108,    78,   113,    79,    79,     8,    14,   141,   141,    81,
      79,    81,    23,    24,    25,   103,    79,    81,    79,    72,
      73,    74,    75,   104,   106,    81,    79,   112,    78,   109,
      28,    27,    29,    53,    54,    56,    67,    68,    69,    77,
      83,    84,   115,   118,   106,   145,    95,    95,    79,    79,
      47,   130,   126,    60,    64,    65,   129,    64,    65,   129,
      30,    30,   129,    30,    57,    58,    59,    62,    30,    30,
     126,    71,   130,    71,   125,   130,   113,    79,   124,   124,
     140,   102,    78,   145,    79,    81,    78,    11,    95,    79,
     111,   112,    53,    54,    56,   106,   106,   113,   116,    78,
     116,    85,    86,   106,   119,   120,    79,    79,    46,    71,
     127,    16,    48,   137,    31,    30,    30,    30,    30,    30,
      30,   126,   126,    30,   126,   129,   129,    30,   129,   126,
     126,   137,   108,   137,    79,   124,    73,   106,   104,    78,
      79,   106,   116,    78,    27,    95,   104,   106,   106,   120,
     127,    71,   113,   131,    29,    78,   113,   122,   132,   133,
     134,   135,   136,    75,   109,   126,   126,   126,   126,   126,
     126,    31,   126,    30,    30,   126,    30,    31,    31,   130,
      79,    79,    79,   104,    27,    95,   104,   116,    79,    79,
      81,   136,   133,   115,   115,    28,    27,    31,    31,    31,
      31,    31,    31,    75,   109,    31,   126,   126,   126,    75,
     109,    75,   109,   137,   124,    79,   116,    79,    79,   113,
      79,   116,   116,   135,   136,    75,   109,    75,   109,    75,
     109,    75,   109,    75,   109,    75,   109,    75,   109
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    87,    88,    88,    88,    88,    89,    89,    89,    89,
      89,    90,    90,    90,    90,    91,    91,    92,    93,    93,
      93,    93,    93,    94,    94,    94,    94,    94,    94,    94,
      95,    96,    96,    97,    97,    97,    98,    98,    99,    99,
      99,    99,   100,   100,   101,   101,   102,   103,   103,   103,
     104,   104,   105,   105,   106,   106,   106,   106,   107,   107,
     107,   107,   107,   107,   107,   107,   107,   107,   108,   108,
     109,   110,   110,   111,   111,   112,   112,   112,   113,   113,
     114,   114,   114,   114,   115,   115,   115,   115,   115,   115,
     116,   116,   117,   117,   118,   118,   118,   119,   119,   120,
     120,   120,   121,   121,   122,   122,   122,   122,   123,   123,
     123,   123,   123,   123,   124,   124,   124,   125,   126,   126,
     126,   126,   127,   127,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   129,   129,   130,   130,   131,
     131,   132,   132,   133,   134,   134,   135,   135,   136,   136,
     136,   137,   137,   138,   138,   139,   139,   140,   140,   141,
     141,   141,   142,   142,   143,   143,   144,   145
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     5,    10,     4,     5,     1,     2,     3,
       3,     1,     4,     0,     1,     1,     1,     3,     7,     7,
       7,     9,     1,     3,     1,     3,     2,     1,     4,     1,
       1,     3,     3,     5,     1,     1,     1,     1,     3,     3,
       4,     5,     6,     5,     6,     5,     6,     4,     0,     2,
       1,     3,     1,     3,     1,     2,     3,     1,     3,     1,
       1,     3,     3,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     3,     3,     4,     1,     2,     1,
       2,     2,     1,     3,     5,     5,     6,     8,     1,     1,
       1,     1,     1,     1,     0,     2,     1,     1,     2,     3,
       4,     5,     1,     2,     1,     5,     5,     3,     6,     6,
       7,     7,     7,     7,     7,     7,     4,     4,     5,     6,
       6,     6,     6,     6,     7,     7,     7,     7,     6,     6,
       7,     7,     7,     7,     3,     0,     1,     0,     3,     1,
       3,     3,     3,     1,     3,     1,     3,     1,     2,     3,
       1,     0,     2,     3,     0,     1,     3,     2,     2,     1,
       1,     0,     0,     2,     1,     1,     1,     1
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
#line 134 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1978 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 139 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1987 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 144 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1996 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 149 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 2005 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 165 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 2013 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 169 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 2021 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 173 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 2029 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 177 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 2037 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 184 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 2045 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 188 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 2053 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 195 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 2061 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 202 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 2069 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 206 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 2077 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 210 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 2085 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 214 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2093 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 218 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2101 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 225 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 2109 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 229 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 2117 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 233 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_bool_expr));
    }
#line 2125 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 237 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_bool_expr));
    }
#line 2133 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: query_expression  */
#line 241 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_query);
    }
#line 2141 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN query_expression  */
#line 245 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_query), false);
    }
#line 2149 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE query_expression  */
#line 249 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_query), true);
    }
#line 2157 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 30: /* query_expression: union_expression opt_order_clause opt_limit  */
#line 256 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_query)->set_tail((yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int));
        (yyval.sv_query) = (yyvsp[-2].sv_query);
    }
#line 2166 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 31: /* union_expression: query_primary  */
#line 264 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = (yyvsp[0].sv_query);
    }
#line 2174 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 32: /* union_expression: union_expression UNION union_quantifier query_primary  */
#line 268 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = append_union_operand((yyvsp[-3].sv_query), (yyvsp[-1].sv_bool), (yyvsp[0].sv_query));
    }
#line 2182 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 33: /* union_quantifier: %empty  */
#line 275 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 2190 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 34: /* union_quantifier: DISTINCT  */
#line 279 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 2198 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 35: /* union_quantifier: ALL  */
#line 283 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool) = true;
    }
#line 2206 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 36: /* query_primary: select_core  */
#line 290 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = (yyvsp[0].sv_select);
    }
#line 2214 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 37: /* query_primary: '(' query_expression ')'  */
#line 294 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_query) = std::make_shared<QueryGroup>((yyvsp[-1].sv_query));
    }
#line 2222 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 38: /* select_core: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having  */
#line 301 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
#line 2230 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 39: /* select_core: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having  */
#line 305 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-5].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
#line 2238 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 40: /* select_core: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having  */
#line 309 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-5].sv_agg_exprs), (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
#line 2246 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 41: /* select_core: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having  */
#line 313 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), (yyvsp[-5].sv_agg_exprs), (yyvsp[-3].sv_from), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_cols), (yyvsp[0].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, false, 0);
    }
#line 2254 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 42: /* fieldList: field  */
#line 320 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2262 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 43: /* fieldList: fieldList ',' field  */
#line 324 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2270 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 44: /* colNameList: colName  */
#line 331 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2278 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 45: /* colNameList: colNameList ',' colName  */
#line 335 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2286 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 46: /* field: colName type  */
#line 342 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2294 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 47: /* type: INT  */
#line 349 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2302 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 48: /* type: CHAR '(' VALUE_INT ')'  */
#line 353 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2310 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 49: /* type: FLOAT  */
#line 357 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2318 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 50: /* valueList: value  */
#line 364 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2326 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 51: /* valueList: valueList ',' value  */
#line 368 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2334 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 52: /* valueRows: '(' valueList ')'  */
#line 375 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2342 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 53: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 379 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2351 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_INT  */
#line 387 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2359 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 55: /* value: VALUE_FLOAT  */
#line 391 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2367 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_STRING  */
#line 395 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2375 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 57: /* value: VALUE_BOOL  */
#line 399 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2383 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 58: /* condition: col op expr  */
#line 406 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2391 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 59: /* condition: col LIKE value  */
#line 410 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), SV_OP_LIKE,
                                          std::static_pointer_cast<Expr>((yyvsp[0].sv_val)));
    }
#line 2400 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 60: /* condition: col NOT LIKE value  */
#line 415 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>(std::make_shared<BinaryExpr>(
            (yyvsp[-3].sv_col), SV_OP_LIKE, std::static_pointer_cast<Expr>((yyvsp[0].sv_val))));
    }
#line 2409 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 61: /* condition: col BETWEEN expr AND expr  */
#line 420 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_between_expr((yyvsp[-4].sv_col), (yyvsp[-2].sv_expr), (yyvsp[0].sv_expr), false);
    }
#line 2417 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 62: /* condition: col NOT BETWEEN expr AND expr  */
#line 424 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_between_expr((yyvsp[-5].sv_col), (yyvsp[-2].sv_expr), (yyvsp[0].sv_expr), true);
    }
#line 2425 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 63: /* condition: col IN '(' valueList ')'  */
#line 428 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_in_list_expr((yyvsp[-4].sv_col), (yyvsp[-1].sv_vals), false);
    }
#line 2433 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 64: /* condition: col NOT IN '(' valueList ')'  */
#line 432 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = make_in_list_expr((yyvsp[-5].sv_col), (yyvsp[-1].sv_vals), true);
    }
#line 2441 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 65: /* condition: col IN '(' query_expression ')'  */
#line 436 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::IN, (yyvsp[-4].sv_col), (yyvsp[-1].sv_query));
    }
#line 2449 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 66: /* condition: col NOT IN '(' query_expression ')'  */
#line 440 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>(std::make_shared<SubqueryPredicate>(
            SubqueryPredicateType::IN, (yyvsp[-5].sv_col), (yyvsp[-1].sv_query)));
    }
#line 2458 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 67: /* condition: EXISTS '(' query_expression ')'  */
#line 445 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<SubqueryPredicate>(SubqueryPredicateType::EXISTS,
                                                 nullptr, (yyvsp[-1].sv_query));
    }
#line 2467 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 68: /* optWhereClause: %empty  */
#line 453 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 2475 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 69: /* optWhereClause: WHERE whereClause  */
#line 457 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2483 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 70: /* whereClause: where_or_expr  */
#line 464 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2491 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 71: /* where_or_expr: where_or_expr OR where_and_expr  */
#line 471 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2499 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 72: /* where_or_expr: where_and_expr  */
#line 475 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2507 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 73: /* where_and_expr: where_and_expr AND where_not_expr  */
#line 482 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2515 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 74: /* where_and_expr: where_not_expr  */
#line 486 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2523 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 75: /* where_not_expr: NOT where_not_expr  */
#line 493 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 2531 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 76: /* where_not_expr: '(' whereClause ')'  */
#line 497 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 2539 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 77: /* where_not_expr: condition  */
#line 501 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2547 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 78: /* col: tbName '.' colName  */
#line 508 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2555 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 79: /* col: colName  */
#line 512 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2563 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 80: /* colList: col  */
#line 519 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2571 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 81: /* colList: colList ',' col  */
#line 523 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2579 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 82: /* colList: col AS IDENTIFIER  */
#line 527 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2589 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 83: /* colList: colList ',' col AS IDENTIFIER  */
#line 533 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2598 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 84: /* op: '='  */
#line 541 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2606 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 85: /* op: '<'  */
#line 545 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2614 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 86: /* op: '>'  */
#line 549 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2622 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 87: /* op: NEQ  */
#line 553 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2630 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 88: /* op: LEQ  */
#line 557 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2638 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 89: /* op: GEQ  */
#line 561 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2646 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 90: /* expr: value  */
#line 568 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2654 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 91: /* expr: col  */
#line 572 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2662 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 92: /* setClauses: setClause  */
#line 579 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2670 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 93: /* setClauses: setClauses ',' setClause  */
#line 583 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2678 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 94: /* setClause: colName '=' value  */
#line 590 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2686 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 95: /* setClause: colName '=' colName  */
#line 594 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2698 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 96: /* setClause: colName '=' colName arith_chain  */
#line 602 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_vals)[0], false);
        if ((yyvsp[0].sv_vals).size() > 1) (yyval.sv_set_clause)->chain = (yyvsp[0].sv_vals);
    }
#line 2710 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 97: /* arith_chain: arith_term  */
#line 613 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2718 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 98: /* arith_chain: arith_chain arith_term  */
#line 617 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2726 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 99: /* arith_term: value  */
#line 624 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2735 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 100: /* arith_term: '+' value  */
#line 629 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2743 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 101: /* arith_term: '-' value  */
#line 633 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = negate_value((yyvsp[0].sv_val));
        if ((yyval.sv_val) == nullptr) YYERROR;
    }
#line 2752 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 102: /* agg_list: agg_func  */
#line 643 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2760 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 103: /* agg_list: agg_list ',' agg_func  */
#line 647 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2768 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 104: /* agg_func: agg_name '(' '*' ')' opt_alias  */
#line 654 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), nullptr, (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2777 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 105: /* agg_func: agg_name '(' col ')' opt_alias  */
#line 659 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str));
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2786 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 106: /* agg_func: agg_name '(' DISTINCT col ')' opt_alias  */
#line 664 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-5].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2796 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 107: /* agg_func: agg_name '(' DISTINCT '(' col ')' ')' opt_alias  */
#line 670 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-7].sv_str), (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2806 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 108: /* agg_name: COUNT  */
#line 678 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "count"; }
#line 2812 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 109: /* agg_name: MAX  */
#line 679 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "max"; }
#line 2818 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 110: /* agg_name: MIN  */
#line 680 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "min"; }
#line 2824 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 111: /* agg_name: SUM  */
#line 681 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "sum"; }
#line 2830 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 112: /* agg_name: AVG  */
#line 682 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "avg"; }
#line 2836 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 113: /* agg_name: IDENTIFIER  */
#line 684 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (find_aggregate((yyvsp[0].sv_str)) == nullptr) YYERROR;
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2845 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 114: /* opt_alias: %empty  */
#line 692 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2853 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 115: /* opt_alias: AS IDENTIFIER  */
#line 696 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2861 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 116: /* opt_alias: IDENTIFIER  */
#line 700 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2869 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 117: /* from_clause: joined_table  */
#line 707 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2877 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 118: /* table_ref: tbName opt_alias  */
#line 715 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<TableRef>((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2885 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 119: /* table_ref: '(' joined_table ')'  */
#line 719 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[-1].sv_from);
    }
#line 2893 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 120: /* table_ref: '(' query_expression ')' required_alias  */
#line 723 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<DerivedTableRef>((yyvsp[-2].sv_query), (yyvsp[0].sv_str));
    }
#line 2901 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 121: /* table_ref: LATERAL '(' query_expression ')' required_alias  */
#line 727 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<LateralRef>((yyvsp[-2].sv_query), (yyvsp[0].sv_str));
    }
#line 2909 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 122: /* required_alias: IDENTIFIER  */
#line 734 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2917 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 123: /* required_alias: AS IDENTIFIER  */
#line 738 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2925 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 124: /* joined_table: table_ref  */
#line 746 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2933 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 125: /* joined_table: joined_table JOIN table_ref ON whereClause  */
#line 750 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2942 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 126: /* joined_table: joined_table JOIN table_ref ON VALUE_BOOL  */
#line 755 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2953 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 127: /* joined_table: joined_table JOIN table_ref  */
#line 762 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2963 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 128: /* joined_table: joined_table INNER JOIN table_ref ON whereClause  */
#line 768 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2972 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 129: /* joined_table: joined_table INNER JOIN table_ref ON VALUE_BOOL  */
#line 773 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2983 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 130: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON whereClause  */
#line 780 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2992 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 131: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 785 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3003 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 132: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON whereClause  */
#line 792 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3012 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 133: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 797 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3023 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 134: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON whereClause  */
#line 804 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3032 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 135: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 809 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3043 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 136: /* joined_table: joined_table CROSS JOIN table_ref  */
#line 816 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3053 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 137: /* joined_table: joined_table NATURAL JOIN table_ref  */
#line 822 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3063 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 138: /* joined_table: joined_table NATURAL INNER JOIN table_ref  */
#line 828 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3073 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 139: /* joined_table: joined_table NATURAL LEFT opt_outer JOIN table_ref  */
#line 834 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3083 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 140: /* joined_table: joined_table NATURAL RIGHT opt_outer JOIN table_ref  */
#line 840 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3093 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 141: /* joined_table: joined_table NATURAL FULL opt_outer JOIN table_ref  */
#line 846 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3103 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 142: /* joined_table: joined_table SEMI JOIN table_ref ON whereClause  */
#line 852 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3112 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 143: /* joined_table: joined_table SEMI JOIN table_ref ON VALUE_BOOL  */
#line 857 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3123 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 144: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON whereClause  */
#line 864 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3132 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 145: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 869 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3143 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 146: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON whereClause  */
#line 876 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3152 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 147: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 881 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3163 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 148: /* joined_table: joined_table ANTI JOIN table_ref ON whereClause  */
#line 888 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3172 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 149: /* joined_table: joined_table ANTI JOIN table_ref ON VALUE_BOOL  */
#line 893 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3183 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 150: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON whereClause  */
#line 900 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3192 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 151: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 905 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3203 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 152: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON whereClause  */
#line 912 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3212 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 153: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 917 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3223 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 154: /* joined_table: joined_table ',' table_ref  */
#line 924 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3233 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 157: /* opt_group_by: %empty  */
#line 939 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 3241 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 158: /* opt_group_by: GROUP BY group_by_list  */
#line 943 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 3249 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 159: /* group_by_list: col  */
#line 950 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 3257 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 160: /* group_by_list: group_by_list ',' col  */
#line 954 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 3265 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 161: /* having_condition: col op expr  */
#line 961 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3273 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 162: /* having_condition: agg_func op expr  */
#line 965 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_agg_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3281 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 163: /* having_clause: having_or_expr  */
#line 972 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3289 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 164: /* having_or_expr: having_or_expr OR having_and_expr  */
#line 979 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3297 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 165: /* having_or_expr: having_and_expr  */
#line 983 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3305 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 166: /* having_and_expr: having_and_expr AND having_not_expr  */
#line 990 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3313 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 167: /* having_and_expr: having_not_expr  */
#line 994 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3321 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 168: /* having_not_expr: NOT having_not_expr  */
#line 1001 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 3329 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 169: /* having_not_expr: '(' having_clause ')'  */
#line 1005 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 3337 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 170: /* having_not_expr: having_condition  */
#line 1009 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3345 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 171: /* opt_having: %empty  */
#line 1016 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 3353 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 172: /* opt_having: HAVING having_clause  */
#line 1020 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3361 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 173: /* opt_order_clause: ORDER BY order_list  */
#line 1027 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 3369 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 174: /* opt_order_clause: %empty  */
#line 1031 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 3377 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 175: /* order_list: order_clause  */
#line 1038 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 3385 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 176: /* order_list: order_list ',' order_clause  */
#line 1042 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 3393 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 177: /* order_clause: col opt_asc_desc  */
#line 1049 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 3401 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 178: /* order_clause: VALUE_INT opt_asc_desc  */
#line 1053 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_int), (yyvsp[0].sv_orderby_dir));
    }
#line 3409 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 179: /* opt_asc_desc: ASC  */
#line 1059 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 3415 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 180: /* opt_asc_desc: DESC  */
#line 1060 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 3421 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 181: /* opt_asc_desc: %empty  */
#line 1061 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 3427 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 182: /* opt_limit: %empty  */
#line 1066 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 3435 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 183: /* opt_limit: LIMIT VALUE_INT  */
#line 1070 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if ((yyvsp[0].sv_int) < 0) YYERROR;
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 3444 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 184: /* set_knob_type: ENABLE_NESTLOOP  */
#line 1077 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 3450 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 185: /* set_knob_type: ENABLE_SORTMERGE  */
#line 1078 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 3456 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;


#line 3460 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

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

#line 1084 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
