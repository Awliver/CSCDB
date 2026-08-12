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
#line 1 "yacc.y"

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

static bool is_lateral_ref(const std::shared_ptr<ast::FromExpr> &from) {
    return std::dynamic_pointer_cast<ast::LateralRef>(from) != nullptr;
}

#line 107 "yacc.tab.cpp"

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
  YYSYMBOL_DISTINCT = 51,                  /* DISTINCT  */
  YYSYMBOL_LEFT = 52,                      /* LEFT  */
  YYSYMBOL_RIGHT = 53,                     /* RIGHT  */
  YYSYMBOL_INNER = 54,                     /* INNER  */
  YYSYMBOL_OUTER = 55,                     /* OUTER  */
  YYSYMBOL_CROSS = 56,                     /* CROSS  */
  YYSYMBOL_FULL = 57,                      /* FULL  */
  YYSYMBOL_NATURAL = 58,                   /* NATURAL  */
  YYSYMBOL_SEMI = 59,                      /* SEMI  */
  YYSYMBOL_ANTI = 60,                      /* ANTI  */
  YYSYMBOL_LATERAL = 61,                   /* LATERAL  */
  YYSYMBOL_LEQ = 62,                       /* LEQ  */
  YYSYMBOL_NEQ = 63,                       /* NEQ  */
  YYSYMBOL_GEQ = 64,                       /* GEQ  */
  YYSYMBOL_T_EOF = 65,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 66,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 67,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 68,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 69,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 70,                /* VALUE_BOOL  */
  YYSYMBOL_71_ = 71,                       /* ';'  */
  YYSYMBOL_72_ = 72,                       /* '='  */
  YYSYMBOL_73_ = 73,                       /* '('  */
  YYSYMBOL_74_ = 74,                       /* ')'  */
  YYSYMBOL_75_ = 75,                       /* '*'  */
  YYSYMBOL_76_ = 76,                       /* ','  */
  YYSYMBOL_77_ = 77,                       /* '.'  */
  YYSYMBOL_78_ = 78,                       /* '<'  */
  YYSYMBOL_79_ = 79,                       /* '>'  */
  YYSYMBOL_80_ = 80,                       /* '+'  */
  YYSYMBOL_81_ = 81,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 82,                  /* $accept  */
  YYSYMBOL_start = 83,                     /* start  */
  YYSYMBOL_stmt = 84,                      /* stmt  */
  YYSYMBOL_txnStmt = 85,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 86,                    /* dbStmt  */
  YYSYMBOL_setStmt = 87,                   /* setStmt  */
  YYSYMBOL_ddl = 88,                       /* ddl  */
  YYSYMBOL_dml = 89,                       /* dml  */
  YYSYMBOL_select_stmt = 90,               /* select_stmt  */
  YYSYMBOL_union_branch = 91,              /* union_branch  */
  YYSYMBOL_union_query = 92,               /* union_query  */
  YYSYMBOL_fieldList = 93,                 /* fieldList  */
  YYSYMBOL_colNameList = 94,               /* colNameList  */
  YYSYMBOL_field = 95,                     /* field  */
  YYSYMBOL_type = 96,                      /* type  */
  YYSYMBOL_valueList = 97,                 /* valueList  */
  YYSYMBOL_valueRows = 98,                 /* valueRows  */
  YYSYMBOL_value = 99,                     /* value  */
  YYSYMBOL_condition = 100,                /* condition  */
  YYSYMBOL_optWhereClause = 101,           /* optWhereClause  */
  YYSYMBOL_whereClause = 102,              /* whereClause  */
  YYSYMBOL_where_or_expr = 103,            /* where_or_expr  */
  YYSYMBOL_where_and_expr = 104,           /* where_and_expr  */
  YYSYMBOL_where_not_expr = 105,           /* where_not_expr  */
  YYSYMBOL_col = 106,                      /* col  */
  YYSYMBOL_colList = 107,                  /* colList  */
  YYSYMBOL_op = 108,                       /* op  */
  YYSYMBOL_expr = 109,                     /* expr  */
  YYSYMBOL_setClauses = 110,               /* setClauses  */
  YYSYMBOL_setClause = 111,                /* setClause  */
  YYSYMBOL_arith_chain = 112,              /* arith_chain  */
  YYSYMBOL_arith_term = 113,               /* arith_term  */
  YYSYMBOL_agg_list = 114,                 /* agg_list  */
  YYSYMBOL_agg_func = 115,                 /* agg_func  */
  YYSYMBOL_agg_name = 116,                 /* agg_name  */
  YYSYMBOL_opt_alias = 117,                /* opt_alias  */
  YYSYMBOL_from_clause = 118,              /* from_clause  */
  YYSYMBOL_table_ref = 119,                /* table_ref  */
  YYSYMBOL_required_alias = 120,           /* required_alias  */
  YYSYMBOL_joined_table = 121,             /* joined_table  */
  YYSYMBOL_opt_outer = 122,                /* opt_outer  */
  YYSYMBOL_opt_group_by = 123,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 124,            /* group_by_list  */
  YYSYMBOL_having_condition = 125,         /* having_condition  */
  YYSYMBOL_having_clause = 126,            /* having_clause  */
  YYSYMBOL_having_or_expr = 127,           /* having_or_expr  */
  YYSYMBOL_having_and_expr = 128,          /* having_and_expr  */
  YYSYMBOL_having_not_expr = 129,          /* having_not_expr  */
  YYSYMBOL_opt_having = 130,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 131,         /* opt_order_clause  */
  YYSYMBOL_order_list = 132,               /* order_list  */
  YYSYMBOL_order_clause = 133,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 134,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit = 135,                /* opt_limit  */
  YYSYMBOL_set_knob_type = 136,            /* set_knob_type  */
  YYSYMBOL_tbName = 137,                   /* tbName  */
  YYSYMBOL_colName = 138                   /* colName  */
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
#define YYFINAL  57
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   514

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  82
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  57
/* YYNRULES -- Number of rules.  */
#define YYNRULES  175
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  412

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   325


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
      73,    74,    75,    80,    76,    81,    77,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    71,
      78,    72,    79,     2,     2,     2,     2,     2,     2,     2,
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
      65,    66,    67,    68,    69,    70
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    92,    92,    97,   102,   107,   115,   116,   117,   118,
     119,   123,   127,   131,   135,   142,   146,   153,   160,   164,
     168,   172,   176,   183,   187,   191,   195,   199,   203,   207,
     211,   218,   222,   226,   230,   237,   241,   245,   249,   256,
     260,   268,   272,   279,   283,   290,   297,   301,   305,   312,
     316,   323,   327,   335,   339,   343,   347,   354,   362,   365,
     372,   379,   383,   390,   394,   401,   405,   409,   416,   420,
     427,   431,   435,   441,   449,   453,   457,   461,   465,   469,
     476,   480,   487,   491,   498,   502,   510,   521,   525,   532,
     537,   541,   551,   555,   562,   567,   572,   578,   587,   588,
     589,   590,   591,   592,   601,   604,   608,   615,   623,   627,
     631,   638,   642,   650,   654,   659,   666,   672,   677,   684,
     689,   696,   701,   708,   713,   720,   726,   732,   738,   744,
     750,   756,   761,   768,   773,   780,   785,   792,   797,   804,
     809,   816,   821,   828,   836,   838,   844,   847,   854,   858,
     865,   869,   876,   883,   887,   894,   898,   905,   909,   913,
     921,   924,   931,   936,   942,   946,   953,   960,   961,   962,
     967,   970,   977,   978,   981,   983
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
  "HAVING", "LIMIT", "UNION", "DISTINCT", "LEFT", "RIGHT", "INNER",
  "OUTER", "CROSS", "FULL", "NATURAL", "SEMI", "ANTI", "LATERAL", "LEQ",
  "NEQ", "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT",
  "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='", "'('", "')'", "'*'", "','",
  "'.'", "'<'", "'>'", "'+'", "'-'", "$accept", "start", "stmt", "txnStmt",
  "dbStmt", "setStmt", "ddl", "dml", "select_stmt", "union_branch",
  "union_query", "fieldList", "colNameList", "field", "type", "valueList",
  "valueRows", "value", "condition", "optWhereClause", "whereClause",
  "where_or_expr", "where_and_expr", "where_not_expr", "col", "colList",
  "op", "expr", "setClauses", "setClause", "arith_chain", "arith_term",
  "agg_list", "agg_func", "agg_name", "opt_alias", "from_clause",
  "table_ref", "required_alias", "joined_table", "opt_outer",
  "opt_group_by", "group_by_list", "having_condition", "having_clause",
  "having_or_expr", "having_and_expr", "having_not_expr", "opt_having",
  "opt_order_clause", "order_list", "order_clause", "opt_asc_desc",
  "opt_limit", "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-244)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-175)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     363,    30,    34,    57,   -38,    43,    66,   -38,    29,   335,
      97,  -244,  -244,  -244,  -244,  -244,  -244,  -244,   124,    63,
    -244,  -244,  -244,  -244,  -244,  -244,  -244,   139,   -38,   -38,
     -38,   -38,  -244,  -244,   -38,   -38,   122,  -244,  -244,    89,
    -244,  -244,  -244,  -244,  -244,   -22,   172,   152,     1,     4,
    -244,   134,   136,  -244,   361,   206,  -244,  -244,  -244,   -38,
     173,   177,  -244,   182,    15,   262,   169,   213,    83,   202,
     219,   307,   219,   380,   145,   169,   297,  -244,  -244,   169,
     169,   169,   241,   169,    80,  -244,  -244,     8,  -244,   256,
    -244,   263,    38,   262,  -244,   333,    49,  -244,   219,   262,
     295,     5,   262,  -244,  -244,    47,   270,   285,   290,  -244,
     219,    92,  -244,   252,   119,  -244,   130,   370,   291,   239,
      80,    80,  -244,  -244,   341,   347,  -244,   245,   169,  -244,
     385,   206,   375,   338,   -15,   286,   360,   219,   198,   235,
     378,   381,   359,   162,   396,   404,   219,   379,  -244,  -244,
     360,   382,   219,   360,   383,   373,    49,    49,  -244,   169,
    -244,   371,  -244,  -244,  -244,   169,  -244,  -244,  -244,  -244,
    -244,   246,  -244,   388,   451,  -244,   389,    80,    80,  -244,
    -244,  -244,  -244,  -244,  -244,   390,  -244,  -244,   362,   391,
     453,     6,    10,   444,   444,   421,  -244,   452,   422,   438,
    -244,   441,   442,   443,   445,   446,   447,   219,   219,   448,
     219,   359,   359,   449,   359,   219,   219,  -244,  -244,   422,
    -244,   262,   422,   400,    49,  -244,  -244,  -244,   412,  -244,
    -244,   370,   370,   408,  -244,   347,  -244,  -244,  -244,  -244,
     370,   370,  -244,   362,  -244,    72,   219,   219,   307,   219,
    -244,  -244,   -38,   383,   128,   467,    35,   219,   219,   219,
     219,   219,   219,   454,  -244,   219,  -244,   456,   457,   219,
     458,   459,   460,   467,   360,   467,   409,  -244,   410,  -244,
     251,   370,  -244,  -244,  -244,   423,  -244,  -244,   262,   262,
      11,   262,   467,  -244,   416,   128,   128,   245,   245,  -244,
    -244,   465,   468,  -244,   478,   450,  -244,  -244,   466,   469,
     470,   471,   472,   473,    37,   474,   219,   219,  -244,   219,
      77,    85,   450,   422,   450,    49,  -244,  -244,   261,  -244,
     360,   360,   219,   360,   450,   383,  -244,   424,   390,   390,
     128,   128,   383,   428,  -244,   111,   157,   168,   170,   208,
     218,  -244,  -244,   223,  -244,  -244,  -244,  -244,  -244,  -244,
    -244,  -244,   467,  -244,  -244,  -244,   422,   422,   262,   422,
    -244,  -244,  -244,  -244,  -244,   468,  -244,    33,   430,  -244,
    -244,  -244,  -244,  -244,  -244,  -244,  -244,  -244,  -244,  -244,
    -244,  -244,  -244,  -244,  -244,   450,   450,   450,   360,   450,
    -244,  -244,  -244,   383,  -244,  -244,  -244,   422,  -244,  -244,
     450,  -244
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       9,     6,    10,     7,     8,    27,    15,     0,     0,     0,
       0,     0,   174,    20,     0,     0,     0,   172,   173,     0,
      98,    99,   100,   101,   102,   175,     0,    70,     0,     0,
      92,     0,     0,    69,     0,     0,    28,     1,     2,     0,
       0,     0,    19,     0,     0,    58,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    29,    16,     0,
       0,     0,     0,     0,     0,    25,   175,    58,    82,     0,
      17,     0,     0,    58,   113,   107,   104,    72,     0,    58,
      71,     0,    58,   103,    93,     0,   175,     0,     0,    68,
       0,     0,    41,     0,     0,    43,     0,     0,    23,     0,
       0,     0,    67,    59,    60,    62,    64,     0,     0,    26,
       0,     0,     0,     0,     0,     0,   146,     0,   144,   144,
       0,     0,   144,     0,     0,     0,     0,     0,   106,   108,
     146,     0,     0,   146,     0,     0,   104,   104,    18,     0,
      46,     0,    48,    45,    21,     0,    22,    55,    53,    54,
      56,     0,    49,     0,     0,    65,     0,     0,     0,    78,
      77,    79,    74,    75,    76,     0,    83,    84,    85,     0,
       0,     0,     0,     0,     0,     0,   109,     0,   160,   116,
     145,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   144,   144,     0,   144,     0,     0,   143,   105,   160,
      73,    58,   160,     0,   104,    94,    95,    42,     0,    44,
      51,     0,     0,     0,    66,    61,    63,    80,    81,    57,
       0,     0,    89,    86,    87,     0,     0,     0,     0,     0,
      39,    40,     0,     0,     0,   163,     0,     0,     0,     0,
       0,     0,     0,     0,   125,     0,   126,     0,     0,     0,
       0,     0,     0,   163,   146,   163,     0,    96,     0,    50,
       0,     0,    90,    91,    88,     0,   111,   110,    58,    58,
       0,    58,   163,   148,   147,     0,     0,     0,     0,   159,
     161,   152,   154,   156,     0,   170,   115,   114,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   127,     0,
       0,     0,   170,   160,   170,   104,    47,    52,     0,   112,
     146,   146,     0,   146,   170,     0,   157,     0,     0,     0,
       0,     0,     0,     0,    31,     0,     0,     0,     0,     0,
       0,   118,   117,     0,   128,   129,   130,   132,   131,   138,
     137,    32,   163,    33,    97,    24,   160,   160,    58,   160,
      30,   149,   158,   150,   151,   153,   155,   169,   162,   164,
     171,   134,   133,   140,   139,   120,   119,   136,   135,   142,
     141,   122,   121,   124,   123,   170,   170,   170,   146,   170,
     168,   167,   166,     0,    34,    35,    36,   160,    37,   165,
     170,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -244,  -244,  -244,  -244,  -244,  -244,  -244,  -244,    -6,   -19,
    -244,  -244,   352,   348,  -244,  -205,  -244,  -110,  -244,   -86,
     -48,  -244,   331,   -81,    -9,   377,   -76,  -107,  -244,   384,
    -244,   267,   -61,   -71,  -244,  -135,   -67,    96,  -244,    44,
     -85,  -142,  -244,  -244,   215,  -244,   174,  -243,  -207,  -225,
    -244,   110,  -244,  -232,  -244,     2,   -37
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,    25,   133,
     134,   111,   114,   112,   163,   171,   118,   172,   122,    85,
     123,   124,   125,   126,   127,    48,   185,   239,    87,    88,
     243,   244,    49,    50,    51,   149,    93,    94,   287,    95,
     203,   198,   294,   299,   300,   301,   302,   303,   255,   305,
     378,   379,   402,   344,    39,    52,    53
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      47,   129,   104,    99,    56,   102,    33,   136,   219,    36,
     101,   222,   273,   150,    70,   275,   153,    72,   152,   247,
     187,   225,   226,   249,   332,    84,    82,   280,    32,    89,
      60,    61,    62,    63,    26,   194,    64,    65,   109,   175,
      28,   400,   113,   115,   115,    47,   115,   401,   322,    77,
     324,  -103,   336,    34,   206,  -174,    27,   209,   132,   195,
      29,    78,   100,    30,   120,   108,   120,   334,    37,    38,
      96,   192,    96,   176,    96,   237,   328,    71,   242,    35,
      73,    73,   248,    31,   128,   221,    73,    73,    83,   277,
     361,    89,   363,   188,    96,   147,   155,   236,   376,    91,
      96,   106,   370,   106,    32,   306,   120,   351,   121,   120,
     121,    98,    96,   106,   120,   148,   362,    54,   285,    55,
     154,   279,   113,    47,    57,   189,   267,   268,   229,   270,
     282,   283,   323,   242,    58,   274,   135,   395,   286,    96,
     120,    66,   135,   106,    91,   223,   106,   357,    96,    32,
     121,   106,    59,   121,    96,   359,    92,   295,   121,   396,
     397,    67,   399,   404,   405,   406,   158,   408,   159,    40,
      41,    42,    43,    44,   250,   251,   238,   106,   411,   288,
     289,   381,   291,   298,   121,    68,   120,   290,   366,   367,
     364,   369,   210,   164,    45,   165,   105,   120,    69,   120,
     410,   296,   330,   331,   166,   333,   165,    74,   307,    96,
      96,   106,    96,    75,   211,   212,   213,    96,    96,   214,
     107,   338,   339,   106,   298,   298,    54,   383,   237,   237,
     121,   373,   374,   199,   106,    86,   106,   120,   385,   100,
     387,   121,   217,   121,   293,   297,    79,   120,    96,    96,
      80,    96,   120,   200,   292,    81,   407,   201,   202,    96,
      96,    96,    96,    96,    96,   368,   352,    96,    97,   298,
     298,    96,   358,   360,   106,   160,   161,   162,   389,    84,
      91,   121,   398,    90,   106,    32,   297,   297,   391,   106,
     200,   121,    98,   393,   204,   205,   121,   382,   384,   386,
     388,   390,   392,   263,   264,   394,   266,   179,   180,   181,
     110,   271,   272,   174,   117,   165,   137,   182,    96,    96,
     230,    96,   231,   183,   184,   327,   371,   231,   130,   238,
     238,   297,   297,   377,    96,   365,   131,   231,   138,   139,
     140,   151,   141,   142,   143,   144,   145,  -174,    40,    41,
      42,    43,    44,   308,   309,   310,   311,   312,   313,   156,
     196,   315,   146,   137,   157,   318,     1,   173,     2,   177,
       3,     4,     5,    45,   178,     6,    40,    41,    42,    43,
      44,     7,     8,     9,    10,   138,   139,   140,   193,   141,
     142,   143,   144,   145,   377,    11,    12,    13,    14,    15,
      16,    45,    40,    41,    42,    43,    44,   197,   207,   146,
      46,   208,   354,   355,   200,   356,    40,    41,    42,    43,
      44,    40,    41,    42,    43,    44,   215,    45,    17,   167,
     168,   169,   170,   116,   216,   119,    76,   167,   168,   169,
     170,    45,   240,   241,   228,   218,   103,   224,   220,   106,
     190,    86,   167,   168,   169,   170,   106,   167,   168,   169,
     170,   232,   233,   234,   132,   245,   246,   252,   253,   256,
     254,   257,   258,   259,   276,   260,   261,   262,   265,   269,
     278,   281,   304,   325,   326,   314,   316,   317,   319,   329,
     320,   321,   335,   340,   342,   341,   380,   345,   372,   343,
     346,   347,   348,   349,   350,   353,   403,   227,   235,   191,
     284,   337,   186,   409,   375
};

static const yytype_int16 yycheck[] =
{
       9,    87,    73,    70,    10,    72,     4,    93,   150,     7,
      71,   153,   219,    99,    13,   222,   102,    13,    13,    13,
     130,   156,   157,    13,    13,    17,    11,   232,    66,    66,
      28,    29,    30,    31,     4,    50,    34,    35,    75,   120,
       6,     8,    79,    80,    81,    54,    83,    14,   273,    55,
     275,    73,   295,    10,   139,    77,    26,   142,    20,    74,
      26,    59,    71,     6,    29,    74,    29,   292,    39,    40,
      68,   132,    70,   121,    72,   185,   281,    76,   188,    13,
      76,    76,    76,    26,    76,   152,    76,    76,    73,   224,
     322,   128,   324,   130,    92,    46,   105,   178,   341,    61,
      98,    66,   334,    66,    66,    70,    29,    70,    73,    29,
      73,    73,   110,    66,    29,    66,   323,    20,    46,    22,
      73,   231,   159,   132,     0,   131,   211,   212,   165,   214,
     240,   241,   274,   243,    71,   221,    92,   362,    66,   137,
      29,    19,    98,    66,    61,   154,    66,    70,   146,    66,
      73,    66,    13,    73,   152,    70,    73,    29,    73,   366,
     367,    72,   369,   395,   396,   397,    74,   399,    76,    41,
      42,    43,    44,    45,   193,   194,   185,    66,   410,   246,
     247,    70,   249,   254,    73,    13,    29,   248,   330,   331,
     325,   333,    30,    74,    66,    76,    51,    29,    46,    29,
     407,    73,   288,   289,    74,   291,    76,    73,   256,   207,
     208,    66,   210,    77,    52,    53,    54,   215,   216,    57,
      75,   297,   298,    66,   295,   296,    20,    70,   338,   339,
      73,   338,   339,   137,    66,    66,    66,    29,    70,   248,
      70,    73,   146,    73,   253,   254,    73,    29,   246,   247,
      73,   249,    29,    55,   252,    73,   398,    59,    60,   257,
     258,   259,   260,   261,   262,   332,   314,   265,    66,   340,
     341,   269,   320,   321,    66,    23,    24,    25,    70,    17,
      61,    73,   368,    70,    66,    66,   295,   296,    70,    66,
      55,    73,    73,    70,    59,    60,    73,   345,   346,   347,
     348,   349,   350,   207,   208,   353,   210,    62,    63,    64,
      13,   215,   216,    74,    73,    76,    30,    72,   316,   317,
      74,   319,    76,    78,    79,    74,   335,    76,    72,   338,
     339,   340,   341,   342,   332,    74,    73,    76,    52,    53,
      54,    46,    56,    57,    58,    59,    60,    77,    41,    42,
      43,    44,    45,   257,   258,   259,   260,   261,   262,    74,
      74,   265,    76,    30,    74,   269,     3,    76,     5,    28,
       7,     8,     9,    66,    27,    12,    41,    42,    43,    44,
      45,    18,    19,    20,    21,    52,    53,    54,    50,    56,
      57,    58,    59,    60,   403,    32,    33,    34,    35,    36,
      37,    66,    41,    42,    43,    44,    45,    47,    30,    76,
      75,    30,   316,   317,    55,   319,    41,    42,    43,    44,
      45,    41,    42,    43,    44,    45,    30,    66,    65,    67,
      68,    69,    70,    81,    30,    83,    75,    67,    68,    69,
      70,    66,    80,    81,    73,    66,    66,    74,    66,    66,
      75,    66,    67,    68,    69,    70,    66,    67,    68,    69,
      70,    73,    11,    74,    20,    74,    13,    46,    16,    31,
      48,    30,    30,    30,    74,    30,    30,    30,    30,    30,
      68,    73,    15,    74,    74,    31,    30,    30,    30,    66,
      31,    31,    76,    28,    16,    27,    68,    31,    74,    49,
      31,    31,    31,    31,    31,    31,    76,   159,   177,   132,
     243,   296,   128,   403,   340
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    32,    33,    34,    35,    36,    37,    65,    83,    84,
      85,    86,    87,    88,    89,    90,     4,    26,     6,    26,
       6,    26,    66,   137,    10,    13,   137,    39,    40,   136,
      41,    42,    43,    44,    45,    66,    75,   106,   107,   114,
     115,   116,   137,   138,    20,    22,    90,     0,    71,    13,
     137,   137,   137,   137,   137,   137,    19,    72,    13,    46,
      13,    76,    13,    76,    73,    77,    75,    90,   137,    73,
      73,    73,    11,    73,    17,   101,    66,   110,   111,   138,
      70,    61,    73,   118,   119,   121,   137,    66,    73,   118,
     106,   114,   118,    66,   115,    51,    66,    75,   106,   138,
      13,    93,    95,   138,    94,   138,    94,    73,    98,    94,
      29,    73,   100,   102,   103,   104,   105,   106,    76,   101,
      72,    73,    20,    91,    92,   121,   101,    30,    52,    53,
      54,    56,    57,    58,    59,    60,    76,    46,    66,   117,
     101,    46,    13,   101,    73,   106,    74,    74,    74,    76,
      23,    24,    25,    96,    74,    76,    74,    67,    68,    69,
      70,    97,    99,    76,    74,   105,   102,    28,    27,    62,
      63,    64,    72,    78,    79,   108,   111,    99,   138,    90,
      75,   107,   114,    50,    50,    74,    74,    47,   123,   119,
      55,    59,    60,   122,    59,    60,   122,    30,    30,   122,
      30,    52,    53,    54,    57,    30,    30,   119,    66,   123,
      66,   118,   123,   106,    74,   117,   117,    95,    73,   138,
      74,    76,    73,    11,    74,   104,   105,    99,   106,   109,
      80,    81,    99,   112,   113,    74,    13,    13,    76,    13,
      91,    91,    46,    16,    48,   130,    31,    30,    30,    30,
      30,    30,    30,   119,   119,    30,   119,   122,   122,    30,
     122,   119,   119,   130,   101,   130,    74,   117,    68,    99,
      97,    73,    99,    99,   113,    46,    66,   120,   118,   118,
     114,   118,   137,   106,   124,    29,    73,   106,   115,   125,
     126,   127,   128,   129,    15,   131,    70,   102,   119,   119,
     119,   119,   119,   119,    31,   119,    30,    30,   119,    30,
      31,    31,   131,   123,   131,    74,    74,    74,    97,    66,
     101,   101,    13,   101,   131,    76,   129,   126,   108,   108,
      28,    27,    16,    49,   135,    31,    31,    31,    31,    31,
      31,    70,   102,    31,   119,   119,   119,    70,   102,    70,
     102,   135,   130,   135,   117,    74,   123,   123,   118,   123,
     135,   106,    74,   109,   109,   128,   129,   106,   132,   133,
      68,    70,   102,    70,   102,    70,   102,    70,   102,    70,
     102,    70,   102,    70,   102,   131,   130,   130,   101,   130,
       8,    14,   134,    76,   135,   135,   135,   123,   135,   133,
     130,   135
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    82,    83,    83,    83,    83,    84,    84,    84,    84,
      84,    85,    85,    85,    85,    86,    86,    87,    88,    88,
      88,    88,    88,    89,    89,    89,    89,    89,    89,    89,
      89,    90,    90,    90,    90,    91,    91,    91,    91,    92,
      92,    93,    93,    94,    94,    95,    96,    96,    96,    97,
      97,    98,    98,    99,    99,    99,    99,   100,   101,   101,
     102,   103,   103,   104,   104,   105,   105,   105,   106,   106,
     107,   107,   107,   107,   108,   108,   108,   108,   108,   108,
     109,   109,   110,   110,   111,   111,   111,   112,   112,   113,
     113,   113,   114,   114,   115,   115,   115,   115,   116,   116,
     116,   116,   116,   116,   117,   117,   117,   118,   119,   119,
     119,   120,   120,   121,   121,   121,   121,   121,   121,   121,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     121,   121,   121,   121,   122,   122,   123,   123,   124,   124,
     125,   125,   126,   127,   127,   128,   128,   129,   129,   129,
     130,   130,   131,   131,   132,   132,   133,   134,   134,   134,
     135,   135,   136,   136,   137,   138
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     5,    10,     4,     5,     1,     2,     3,
      10,     9,     9,     9,    11,     8,     8,     8,    10,     3,
       3,     1,     3,     1,     3,     2,     1,     4,     1,     1,
       3,     3,     5,     1,     1,     1,     1,     3,     0,     2,
       1,     3,     1,     3,     1,     2,     3,     1,     3,     1,
       1,     3,     3,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     3,     3,     4,     1,     2,     1,
       2,     2,     1,     3,     5,     5,     6,     8,     1,     1,
       1,     1,     1,     1,     0,     2,     1,     1,     2,     3,
       5,     1,     2,     1,     5,     5,     3,     6,     6,     7,
       7,     7,     7,     7,     7,     4,     4,     5,     6,     6,
       6,     6,     6,     7,     7,     7,     7,     6,     6,     7,
       7,     7,     7,     3,     0,     1,     0,     3,     1,     3,
       3,     3,     1,     3,     1,     3,     1,     2,     3,     1,
       0,     2,     3,     0,     1,     3,     2,     1,     1,     0,
       0,     2,     1,     1,     1,     1
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
#line 93 "yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1930 "yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 98 "yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1939 "yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 103 "yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1948 "yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 108 "yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1957 "yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 124 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1965 "yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 128 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1973 "yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 132 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1981 "yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 136 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1989 "yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 143 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1997 "yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 147 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 2005 "yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 154 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 2013 "yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 161 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 2021 "yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 165 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 2029 "yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 169 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 2037 "yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 173 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2045 "yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 177 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2053 "yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 184 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 2061 "yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 188 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 2069 "yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 192 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_bool_expr));
    }
#line 2077 "yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 196 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_bool_expr));
    }
#line 2085 "yacc.tab.cpp"
    break;

  case 27: /* dml: select_stmt  */
#line 200 "yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select);
    }
#line 2093 "yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN select_stmt  */
#line 204 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), false);
    }
#line 2101 "yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE select_stmt  */
#line 208 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), true);
    }
#line 2109 "yacc.tab.cpp"
    break;

  case 30: /* dml: SELECT '*' FROM '(' union_query ')' AS tbName opt_order_clause opt_limit  */
#line 212 "yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_selects), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2117 "yacc.tab.cpp"
    break;

  case 31: /* select_stmt: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 219 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_bool_expr), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2125 "yacc.tab.cpp"
    break;

  case 32: /* select_stmt: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 223 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_bool_expr), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2133 "yacc.tab.cpp"
    break;

  case 33: /* select_stmt: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 227 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_bool_expr), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2141 "yacc.tab.cpp"
    break;

  case 34: /* select_stmt: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 231 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-9].sv_cols), (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_bool_expr), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_bool_expr), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2149 "yacc.tab.cpp"
    break;

  case 35: /* union_branch: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 238 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_bool_expr), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2157 "yacc.tab.cpp"
    break;

  case 36: /* union_branch: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 242 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-6].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_bool_expr), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2165 "yacc.tab.cpp"
    break;

  case 37: /* union_branch: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 246 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_bool_expr), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2173 "yacc.tab.cpp"
    break;

  case 38: /* union_branch: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 250 "yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-8].sv_cols), (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_bool_expr), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_bool_expr), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2181 "yacc.tab.cpp"
    break;

  case 39: /* union_query: union_branch UNION union_branch  */
#line 257 "yacc.y"
    {
        (yyval.sv_selects) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select), (yyvsp[0].sv_select)};
    }
#line 2189 "yacc.tab.cpp"
    break;

  case 40: /* union_query: union_query UNION union_branch  */
#line 261 "yacc.y"
    {
        (yyval.sv_selects) = (yyvsp[-2].sv_selects);
        (yyval.sv_selects).push_back((yyvsp[0].sv_select));
    }
#line 2198 "yacc.tab.cpp"
    break;

  case 41: /* fieldList: field  */
#line 269 "yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2206 "yacc.tab.cpp"
    break;

  case 42: /* fieldList: fieldList ',' field  */
#line 273 "yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2214 "yacc.tab.cpp"
    break;

  case 43: /* colNameList: colName  */
#line 280 "yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2222 "yacc.tab.cpp"
    break;

  case 44: /* colNameList: colNameList ',' colName  */
#line 284 "yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2230 "yacc.tab.cpp"
    break;

  case 45: /* field: colName type  */
#line 291 "yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2238 "yacc.tab.cpp"
    break;

  case 46: /* type: INT  */
#line 298 "yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2246 "yacc.tab.cpp"
    break;

  case 47: /* type: CHAR '(' VALUE_INT ')'  */
#line 302 "yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2254 "yacc.tab.cpp"
    break;

  case 48: /* type: FLOAT  */
#line 306 "yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2262 "yacc.tab.cpp"
    break;

  case 49: /* valueList: value  */
#line 313 "yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2270 "yacc.tab.cpp"
    break;

  case 50: /* valueList: valueList ',' value  */
#line 317 "yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2278 "yacc.tab.cpp"
    break;

  case 51: /* valueRows: '(' valueList ')'  */
#line 324 "yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2286 "yacc.tab.cpp"
    break;

  case 52: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 328 "yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2295 "yacc.tab.cpp"
    break;

  case 53: /* value: VALUE_INT  */
#line 336 "yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2303 "yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_FLOAT  */
#line 340 "yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2311 "yacc.tab.cpp"
    break;

  case 55: /* value: VALUE_STRING  */
#line 344 "yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2319 "yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_BOOL  */
#line 348 "yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2327 "yacc.tab.cpp"
    break;

  case 57: /* condition: col op expr  */
#line 355 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2335 "yacc.tab.cpp"
    break;

  case 58: /* optWhereClause: %empty  */
#line 362 "yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 2343 "yacc.tab.cpp"
    break;

  case 59: /* optWhereClause: WHERE whereClause  */
#line 366 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2351 "yacc.tab.cpp"
    break;

  case 60: /* whereClause: where_or_expr  */
#line 373 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2359 "yacc.tab.cpp"
    break;

  case 61: /* where_or_expr: where_or_expr OR where_and_expr  */
#line 380 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2367 "yacc.tab.cpp"
    break;

  case 62: /* where_or_expr: where_and_expr  */
#line 384 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2375 "yacc.tab.cpp"
    break;

  case 63: /* where_and_expr: where_and_expr AND where_not_expr  */
#line 391 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 2383 "yacc.tab.cpp"
    break;

  case 64: /* where_and_expr: where_not_expr  */
#line 395 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2391 "yacc.tab.cpp"
    break;

  case 65: /* where_not_expr: NOT where_not_expr  */
#line 402 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 2399 "yacc.tab.cpp"
    break;

  case 66: /* where_not_expr: '(' whereClause ')'  */
#line 406 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 2407 "yacc.tab.cpp"
    break;

  case 67: /* where_not_expr: condition  */
#line 410 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 2415 "yacc.tab.cpp"
    break;

  case 68: /* col: tbName '.' colName  */
#line 417 "yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2423 "yacc.tab.cpp"
    break;

  case 69: /* col: colName  */
#line 421 "yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2431 "yacc.tab.cpp"
    break;

  case 70: /* colList: col  */
#line 428 "yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2439 "yacc.tab.cpp"
    break;

  case 71: /* colList: colList ',' col  */
#line 432 "yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2447 "yacc.tab.cpp"
    break;

  case 72: /* colList: col AS IDENTIFIER  */
#line 436 "yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2457 "yacc.tab.cpp"
    break;

  case 73: /* colList: colList ',' col AS IDENTIFIER  */
#line 442 "yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2466 "yacc.tab.cpp"
    break;

  case 74: /* op: '='  */
#line 450 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2474 "yacc.tab.cpp"
    break;

  case 75: /* op: '<'  */
#line 454 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2482 "yacc.tab.cpp"
    break;

  case 76: /* op: '>'  */
#line 458 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2490 "yacc.tab.cpp"
    break;

  case 77: /* op: NEQ  */
#line 462 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2498 "yacc.tab.cpp"
    break;

  case 78: /* op: LEQ  */
#line 466 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2506 "yacc.tab.cpp"
    break;

  case 79: /* op: GEQ  */
#line 470 "yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2514 "yacc.tab.cpp"
    break;

  case 80: /* expr: value  */
#line 477 "yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2522 "yacc.tab.cpp"
    break;

  case 81: /* expr: col  */
#line 481 "yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2530 "yacc.tab.cpp"
    break;

  case 82: /* setClauses: setClause  */
#line 488 "yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2538 "yacc.tab.cpp"
    break;

  case 83: /* setClauses: setClauses ',' setClause  */
#line 492 "yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2546 "yacc.tab.cpp"
    break;

  case 84: /* setClause: colName '=' value  */
#line 499 "yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2554 "yacc.tab.cpp"
    break;

  case 85: /* setClause: colName '=' colName  */
#line 503 "yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2566 "yacc.tab.cpp"
    break;

  case 86: /* setClause: colName '=' colName arith_chain  */
#line 511 "yacc.y"
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_vals)[0], false);
        if ((yyvsp[0].sv_vals).size() > 1) (yyval.sv_set_clause)->chain = (yyvsp[0].sv_vals);
    }
#line 2578 "yacc.tab.cpp"
    break;

  case 87: /* arith_chain: arith_term  */
#line 522 "yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2586 "yacc.tab.cpp"
    break;

  case 88: /* arith_chain: arith_chain arith_term  */
#line 526 "yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2594 "yacc.tab.cpp"
    break;

  case 89: /* arith_term: value  */
#line 533 "yacc.y"
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2603 "yacc.tab.cpp"
    break;

  case 90: /* arith_term: '+' value  */
#line 538 "yacc.y"
    {
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2611 "yacc.tab.cpp"
    break;

  case 91: /* arith_term: '-' value  */
#line 542 "yacc.y"
    {
        (yyval.sv_val) = negate_value((yyvsp[0].sv_val));
        if ((yyval.sv_val) == nullptr) YYERROR;
    }
#line 2620 "yacc.tab.cpp"
    break;

  case 92: /* agg_list: agg_func  */
#line 552 "yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2628 "yacc.tab.cpp"
    break;

  case 93: /* agg_list: agg_list ',' agg_func  */
#line 556 "yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2636 "yacc.tab.cpp"
    break;

  case 94: /* agg_func: agg_name '(' '*' ')' opt_alias  */
#line 563 "yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), nullptr, (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2645 "yacc.tab.cpp"
    break;

  case 95: /* agg_func: agg_name '(' col ')' opt_alias  */
#line 568 "yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str));
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2654 "yacc.tab.cpp"
    break;

  case 96: /* agg_func: agg_name '(' DISTINCT col ')' opt_alias  */
#line 573 "yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-5].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2664 "yacc.tab.cpp"
    break;

  case 97: /* agg_func: agg_name '(' DISTINCT '(' col ')' ')' opt_alias  */
#line 579 "yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-7].sv_str), (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2674 "yacc.tab.cpp"
    break;

  case 98: /* agg_name: COUNT  */
#line 587 "yacc.y"
              { (yyval.sv_str) = "count"; }
#line 2680 "yacc.tab.cpp"
    break;

  case 99: /* agg_name: MAX  */
#line 588 "yacc.y"
              { (yyval.sv_str) = "max"; }
#line 2686 "yacc.tab.cpp"
    break;

  case 100: /* agg_name: MIN  */
#line 589 "yacc.y"
              { (yyval.sv_str) = "min"; }
#line 2692 "yacc.tab.cpp"
    break;

  case 101: /* agg_name: SUM  */
#line 590 "yacc.y"
              { (yyval.sv_str) = "sum"; }
#line 2698 "yacc.tab.cpp"
    break;

  case 102: /* agg_name: AVG  */
#line 591 "yacc.y"
              { (yyval.sv_str) = "avg"; }
#line 2704 "yacc.tab.cpp"
    break;

  case 103: /* agg_name: IDENTIFIER  */
#line 593 "yacc.y"
    {
        if (find_aggregate((yyvsp[0].sv_str)) == nullptr) YYERROR;
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2713 "yacc.tab.cpp"
    break;

  case 104: /* opt_alias: %empty  */
#line 601 "yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2721 "yacc.tab.cpp"
    break;

  case 105: /* opt_alias: AS IDENTIFIER  */
#line 605 "yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2729 "yacc.tab.cpp"
    break;

  case 106: /* opt_alias: IDENTIFIER  */
#line 609 "yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2737 "yacc.tab.cpp"
    break;

  case 107: /* from_clause: joined_table  */
#line 616 "yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2745 "yacc.tab.cpp"
    break;

  case 108: /* table_ref: tbName opt_alias  */
#line 624 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<TableRef>((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2753 "yacc.tab.cpp"
    break;

  case 109: /* table_ref: '(' joined_table ')'  */
#line 628 "yacc.y"
    {
        (yyval.sv_from) = (yyvsp[-1].sv_from);
    }
#line 2761 "yacc.tab.cpp"
    break;

  case 110: /* table_ref: LATERAL '(' select_stmt ')' required_alias  */
#line 632 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<LateralRef>((yyvsp[-2].sv_select), (yyvsp[0].sv_str));
    }
#line 2769 "yacc.tab.cpp"
    break;

  case 111: /* required_alias: IDENTIFIER  */
#line 639 "yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2777 "yacc.tab.cpp"
    break;

  case 112: /* required_alias: AS IDENTIFIER  */
#line 643 "yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2785 "yacc.tab.cpp"
    break;

  case 113: /* joined_table: table_ref  */
#line 651 "yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2793 "yacc.tab.cpp"
    break;

  case 114: /* joined_table: joined_table JOIN table_ref ON whereClause  */
#line 655 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2802 "yacc.tab.cpp"
    break;

  case 115: /* joined_table: joined_table JOIN table_ref ON VALUE_BOOL  */
#line 660 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2813 "yacc.tab.cpp"
    break;

  case 116: /* joined_table: joined_table JOIN table_ref  */
#line 667 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2823 "yacc.tab.cpp"
    break;

  case 117: /* joined_table: joined_table INNER JOIN table_ref ON whereClause  */
#line 673 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2832 "yacc.tab.cpp"
    break;

  case 118: /* joined_table: joined_table INNER JOIN table_ref ON VALUE_BOOL  */
#line 678 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2843 "yacc.tab.cpp"
    break;

  case 119: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON whereClause  */
#line 685 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2852 "yacc.tab.cpp"
    break;

  case 120: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 690 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2863 "yacc.tab.cpp"
    break;

  case 121: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON whereClause  */
#line 697 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2872 "yacc.tab.cpp"
    break;

  case 122: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 702 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2883 "yacc.tab.cpp"
    break;

  case 123: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON whereClause  */
#line 709 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2892 "yacc.tab.cpp"
    break;

  case 124: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 714 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2903 "yacc.tab.cpp"
    break;

  case 125: /* joined_table: joined_table CROSS JOIN table_ref  */
#line 721 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2913 "yacc.tab.cpp"
    break;

  case 126: /* joined_table: joined_table NATURAL JOIN table_ref  */
#line 727 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2923 "yacc.tab.cpp"
    break;

  case 127: /* joined_table: joined_table NATURAL INNER JOIN table_ref  */
#line 733 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2933 "yacc.tab.cpp"
    break;

  case 128: /* joined_table: joined_table NATURAL LEFT opt_outer JOIN table_ref  */
#line 739 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2943 "yacc.tab.cpp"
    break;

  case 129: /* joined_table: joined_table NATURAL RIGHT opt_outer JOIN table_ref  */
#line 745 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2953 "yacc.tab.cpp"
    break;

  case 130: /* joined_table: joined_table NATURAL FULL opt_outer JOIN table_ref  */
#line 751 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), nullptr,
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2963 "yacc.tab.cpp"
    break;

  case 131: /* joined_table: joined_table SEMI JOIN table_ref ON whereClause  */
#line 757 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2972 "yacc.tab.cpp"
    break;

  case 132: /* joined_table: joined_table SEMI JOIN table_ref ON VALUE_BOOL  */
#line 762 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2983 "yacc.tab.cpp"
    break;

  case 133: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON whereClause  */
#line 769 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2992 "yacc.tab.cpp"
    break;

  case 134: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 774 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3003 "yacc.tab.cpp"
    break;

  case 135: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON whereClause  */
#line 781 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3012 "yacc.tab.cpp"
    break;

  case 136: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 786 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3023 "yacc.tab.cpp"
    break;

  case 137: /* joined_table: joined_table ANTI JOIN table_ref ON whereClause  */
#line 793 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3032 "yacc.tab.cpp"
    break;

  case 138: /* joined_table: joined_table ANTI JOIN table_ref ON VALUE_BOOL  */
#line 798 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3043 "yacc.tab.cpp"
    break;

  case 139: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON whereClause  */
#line 805 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3052 "yacc.tab.cpp"
    break;

  case 140: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 810 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3063 "yacc.tab.cpp"
    break;

  case 141: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON whereClause  */
#line 817 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_bool_expr), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 3072 "yacc.tab.cpp"
    break;

  case 142: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 822 "yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 3083 "yacc.tab.cpp"
    break;

  case 143: /* joined_table: joined_table ',' table_ref  */
#line 829 "yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), nullptr,
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 3093 "yacc.tab.cpp"
    break;

  case 146: /* opt_group_by: %empty  */
#line 844 "yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 3101 "yacc.tab.cpp"
    break;

  case 147: /* opt_group_by: GROUP BY group_by_list  */
#line 848 "yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 3109 "yacc.tab.cpp"
    break;

  case 148: /* group_by_list: col  */
#line 855 "yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 3117 "yacc.tab.cpp"
    break;

  case 149: /* group_by_list: group_by_list ',' col  */
#line 859 "yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 3125 "yacc.tab.cpp"
    break;

  case 150: /* having_condition: col op expr  */
#line 866 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3133 "yacc.tab.cpp"
    break;

  case 151: /* having_condition: agg_func op expr  */
#line 870 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_agg_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3141 "yacc.tab.cpp"
    break;

  case 152: /* having_clause: having_or_expr  */
#line 877 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3149 "yacc.tab.cpp"
    break;

  case 153: /* having_or_expr: having_or_expr OR having_and_expr  */
#line 884 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::OR, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3157 "yacc.tab.cpp"
    break;

  case 154: /* having_or_expr: having_and_expr  */
#line 888 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3165 "yacc.tab.cpp"
    break;

  case 155: /* having_and_expr: having_and_expr AND having_not_expr  */
#line 895 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<LogicalExpr>(LogicalOp::AND, (yyvsp[-2].sv_bool_expr), (yyvsp[0].sv_bool_expr));
    }
#line 3173 "yacc.tab.cpp"
    break;

  case 156: /* having_and_expr: having_not_expr  */
#line 899 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3181 "yacc.tab.cpp"
    break;

  case 157: /* having_not_expr: NOT having_not_expr  */
#line 906 "yacc.y"
    {
        (yyval.sv_bool_expr) = std::make_shared<NotExpr>((yyvsp[0].sv_bool_expr));
    }
#line 3189 "yacc.tab.cpp"
    break;

  case 158: /* having_not_expr: '(' having_clause ')'  */
#line 910 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[-1].sv_bool_expr);
    }
#line 3197 "yacc.tab.cpp"
    break;

  case 159: /* having_not_expr: having_condition  */
#line 914 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3205 "yacc.tab.cpp"
    break;

  case 160: /* opt_having: %empty  */
#line 921 "yacc.y"
    {
        (yyval.sv_bool_expr) = nullptr;
    }
#line 3213 "yacc.tab.cpp"
    break;

  case 161: /* opt_having: HAVING having_clause  */
#line 925 "yacc.y"
    {
        (yyval.sv_bool_expr) = (yyvsp[0].sv_bool_expr);
    }
#line 3221 "yacc.tab.cpp"
    break;

  case 162: /* opt_order_clause: ORDER BY order_list  */
#line 932 "yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 3229 "yacc.tab.cpp"
    break;

  case 163: /* opt_order_clause: %empty  */
#line 936 "yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 3237 "yacc.tab.cpp"
    break;

  case 164: /* order_list: order_clause  */
#line 943 "yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 3245 "yacc.tab.cpp"
    break;

  case 165: /* order_list: order_list ',' order_clause  */
#line 947 "yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 3253 "yacc.tab.cpp"
    break;

  case 166: /* order_clause: col opt_asc_desc  */
#line 954 "yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 3261 "yacc.tab.cpp"
    break;

  case 167: /* opt_asc_desc: ASC  */
#line 960 "yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 3267 "yacc.tab.cpp"
    break;

  case 168: /* opt_asc_desc: DESC  */
#line 961 "yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 3273 "yacc.tab.cpp"
    break;

  case 169: /* opt_asc_desc: %empty  */
#line 962 "yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 3279 "yacc.tab.cpp"
    break;

  case 170: /* opt_limit: %empty  */
#line 967 "yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 3287 "yacc.tab.cpp"
    break;

  case 171: /* opt_limit: LIMIT VALUE_INT  */
#line 971 "yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 3295 "yacc.tab.cpp"
    break;

  case 172: /* set_knob_type: ENABLE_NESTLOOP  */
#line 977 "yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 3301 "yacc.tab.cpp"
    break;

  case 173: /* set_knob_type: ENABLE_SORTMERGE  */
#line 978 "yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 3307 "yacc.tab.cpp"
    break;


#line 3311 "yacc.tab.cpp"

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

#line 984 "yacc.y"
