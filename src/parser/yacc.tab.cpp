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
#line 1 "/root/csc-db/src/parser/yacc.y"

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

static bool is_lateral_ref(const std::shared_ptr<ast::FromExpr> &from) {
    return std::dynamic_pointer_cast<ast::LateralRef>(from) != nullptr;
}

#line 98 "/root/csc-db/src/parser/yacc.tab.cpp"

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
  YYSYMBOL_JOIN = 28,                      /* JOIN  */
  YYSYMBOL_ON = 29,                        /* ON  */
  YYSYMBOL_EXIT = 30,                      /* EXIT  */
  YYSYMBOL_HELP = 31,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 32,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 33,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 34,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 35,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 36,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 37,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 38,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_COUNT = 39,                     /* COUNT  */
  YYSYMBOL_MAX = 40,                       /* MAX  */
  YYSYMBOL_MIN = 41,                       /* MIN  */
  YYSYMBOL_SUM = 42,                       /* SUM  */
  YYSYMBOL_AVG = 43,                       /* AVG  */
  YYSYMBOL_AS = 44,                        /* AS  */
  YYSYMBOL_GROUP = 45,                     /* GROUP  */
  YYSYMBOL_HAVING = 46,                    /* HAVING  */
  YYSYMBOL_LIMIT = 47,                     /* LIMIT  */
  YYSYMBOL_UNION = 48,                     /* UNION  */
  YYSYMBOL_DISTINCT = 49,                  /* DISTINCT  */
  YYSYMBOL_LEFT = 50,                      /* LEFT  */
  YYSYMBOL_RIGHT = 51,                     /* RIGHT  */
  YYSYMBOL_INNER = 52,                     /* INNER  */
  YYSYMBOL_OUTER = 53,                     /* OUTER  */
  YYSYMBOL_CROSS = 54,                     /* CROSS  */
  YYSYMBOL_FULL = 55,                      /* FULL  */
  YYSYMBOL_NATURAL = 56,                   /* NATURAL  */
  YYSYMBOL_SEMI = 57,                      /* SEMI  */
  YYSYMBOL_ANTI = 58,                      /* ANTI  */
  YYSYMBOL_LATERAL = 59,                   /* LATERAL  */
  YYSYMBOL_LEQ = 60,                       /* LEQ  */
  YYSYMBOL_NEQ = 61,                       /* NEQ  */
  YYSYMBOL_GEQ = 62,                       /* GEQ  */
  YYSYMBOL_T_EOF = 63,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 64,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 65,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 66,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 67,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 68,                /* VALUE_BOOL  */
  YYSYMBOL_69_ = 69,                       /* ';'  */
  YYSYMBOL_70_ = 70,                       /* '='  */
  YYSYMBOL_71_ = 71,                       /* '('  */
  YYSYMBOL_72_ = 72,                       /* ')'  */
  YYSYMBOL_73_ = 73,                       /* '*'  */
  YYSYMBOL_74_ = 74,                       /* ','  */
  YYSYMBOL_75_ = 75,                       /* '.'  */
  YYSYMBOL_76_ = 76,                       /* '<'  */
  YYSYMBOL_77_ = 77,                       /* '>'  */
  YYSYMBOL_78_ = 78,                       /* '+'  */
  YYSYMBOL_79_ = 79,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 80,                  /* $accept  */
  YYSYMBOL_start = 81,                     /* start  */
  YYSYMBOL_stmt = 82,                      /* stmt  */
  YYSYMBOL_txnStmt = 83,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 84,                    /* dbStmt  */
  YYSYMBOL_setStmt = 85,                   /* setStmt  */
  YYSYMBOL_ddl = 86,                       /* ddl  */
  YYSYMBOL_dml = 87,                       /* dml  */
  YYSYMBOL_select_stmt = 88,               /* select_stmt  */
  YYSYMBOL_union_branch = 89,              /* union_branch  */
  YYSYMBOL_union_query = 90,               /* union_query  */
  YYSYMBOL_fieldList = 91,                 /* fieldList  */
  YYSYMBOL_colNameList = 92,               /* colNameList  */
  YYSYMBOL_field = 93,                     /* field  */
  YYSYMBOL_type = 94,                      /* type  */
  YYSYMBOL_valueList = 95,                 /* valueList  */
  YYSYMBOL_valueRows = 96,                 /* valueRows  */
  YYSYMBOL_value = 97,                     /* value  */
  YYSYMBOL_condition = 98,                 /* condition  */
  YYSYMBOL_optWhereClause = 99,            /* optWhereClause  */
  YYSYMBOL_whereClause = 100,              /* whereClause  */
  YYSYMBOL_col = 101,                      /* col  */
  YYSYMBOL_colList = 102,                  /* colList  */
  YYSYMBOL_op = 103,                       /* op  */
  YYSYMBOL_expr = 104,                     /* expr  */
  YYSYMBOL_setClauses = 105,               /* setClauses  */
  YYSYMBOL_setClause = 106,                /* setClause  */
  YYSYMBOL_arith_chain = 107,              /* arith_chain  */
  YYSYMBOL_arith_term = 108,               /* arith_term  */
  YYSYMBOL_agg_list = 109,                 /* agg_list  */
  YYSYMBOL_agg_func = 110,                 /* agg_func  */
  YYSYMBOL_opt_alias = 111,                /* opt_alias  */
  YYSYMBOL_from_clause = 112,              /* from_clause  */
  YYSYMBOL_table_ref = 113,                /* table_ref  */
  YYSYMBOL_required_alias = 114,           /* required_alias  */
  YYSYMBOL_joined_table = 115,             /* joined_table  */
  YYSYMBOL_opt_outer = 116,                /* opt_outer  */
  YYSYMBOL_opt_group_by = 117,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 118,            /* group_by_list  */
  YYSYMBOL_having_condition = 119,         /* having_condition  */
  YYSYMBOL_having_clause = 120,            /* having_clause  */
  YYSYMBOL_opt_having = 121,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 122,         /* opt_order_clause  */
  YYSYMBOL_order_list = 123,               /* order_list  */
  YYSYMBOL_order_clause = 124,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 125,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit = 126,                /* opt_limit  */
  YYSYMBOL_set_knob_type = 127,            /* set_knob_type  */
  YYSYMBOL_tbName = 128,                   /* tbName  */
  YYSYMBOL_colName = 129                   /* colName  */
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
#define YYFINAL  56
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   478

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  80
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  50
/* YYNRULES -- Number of rules.  */
#define YYNRULES  161
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  405

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   323


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
      71,    72,    73,    78,    74,    79,    75,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    69,
      76,    70,    77,     2,     2,     2,     2,     2,     2,     2,
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
      65,    66,    67,    68
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    82,    82,    87,    92,    97,   105,   106,   107,   108,
     109,   113,   117,   121,   125,   132,   136,   143,   150,   154,
     158,   162,   166,   173,   177,   181,   185,   189,   193,   197,
     201,   208,   212,   216,   220,   227,   231,   235,   239,   246,
     250,   258,   262,   269,   273,   280,   287,   291,   295,   302,
     306,   313,   317,   325,   329,   333,   337,   344,   351,   352,
     359,   363,   370,   374,   381,   385,   389,   395,   403,   407,
     411,   415,   419,   423,   430,   434,   441,   445,   452,   456,
     464,   475,   479,   486,   491,   495,   505,   509,   516,   520,
     524,   529,   534,   538,   542,   546,   554,   557,   561,   568,
     576,   580,   584,   591,   595,   603,   607,   612,   619,   625,
     630,   637,   642,   649,   654,   661,   666,   673,   679,   685,
     691,   697,   703,   709,   714,   721,   726,   733,   738,   745,
     750,   757,   762,   769,   774,   781,   789,   791,   797,   800,
     807,   811,   818,   822,   833,   837,   845,   848,   855,   860,
     866,   870,   877,   884,   885,   886,   891,   894,   901,   902,
     905,   907
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
  "EXPLAIN", "ANALYZE", "INT", "CHAR", "FLOAT", "INDEX", "AND", "JOIN",
  "ON", "EXIT", "HELP", "TXN_BEGIN", "TXN_COMMIT", "TXN_ABORT",
  "TXN_ROLLBACK", "ORDER_BY", "ENABLE_NESTLOOP", "ENABLE_SORTMERGE",
  "COUNT", "MAX", "MIN", "SUM", "AVG", "AS", "GROUP", "HAVING", "LIMIT",
  "UNION", "DISTINCT", "LEFT", "RIGHT", "INNER", "OUTER", "CROSS", "FULL",
  "NATURAL", "SEMI", "ANTI", "LATERAL", "LEQ", "NEQ", "GEQ", "T_EOF",
  "IDENTIFIER", "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT", "VALUE_BOOL",
  "';'", "'='", "'('", "')'", "'*'", "','", "'.'", "'<'", "'>'", "'+'",
  "'-'", "$accept", "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl",
  "dml", "select_stmt", "union_branch", "union_query", "fieldList",
  "colNameList", "field", "type", "valueList", "valueRows", "value",
  "condition", "optWhereClause", "whereClause", "col", "colList", "op",
  "expr", "setClauses", "setClause", "arith_chain", "arith_term",
  "agg_list", "agg_func", "opt_alias", "from_clause", "table_ref",
  "required_alias", "joined_table", "opt_outer", "opt_group_by",
  "group_by_list", "having_condition", "having_clause", "opt_having",
  "opt_order_clause", "order_list", "order_clause", "opt_asc_desc",
  "opt_limit", "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-229)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-161)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     330,    12,     4,   100,   -32,    32,    74,   -32,    18,   155,
      52,  -229,  -229,  -229,  -229,  -229,  -229,  -229,   108,    31,
    -229,  -229,  -229,  -229,  -229,  -229,  -229,   103,   -32,   -32,
     -32,   -32,  -229,  -229,   -32,   -32,   114,  -229,  -229,    49,
      69,    87,    96,    99,   128,   127,   210,   182,    -6,     2,
    -229,   159,  -229,   168,   211,  -229,  -229,  -229,   -32,   174,
     177,  -229,   187,    43,   248,   202,   236,   119,   258,   258,
     258,   258,   107,   276,   156,   247,   156,   313,   202,   299,
    -229,  -229,   202,   202,   202,   275,   202,   258,  -229,  -229,
       1,  -229,   288,  -229,    47,   246,   297,   319,   342,   343,
     344,   314,    -7,   248,  -229,   316,    65,  -229,   156,   248,
     358,     6,   248,  -229,  -229,   156,    77,  -229,   268,   117,
    -229,   260,   341,   329,   285,  -229,   390,   254,   202,  -229,
     315,   258,   346,    65,    65,    65,    65,    65,    65,   211,
     212,   371,   -19,   245,   375,   156,   180,   189,   393,   394,
     370,    93,   396,   397,   156,   362,  -229,  -229,   375,   363,
     156,   375,  -229,   202,  -229,   357,  -229,  -229,  -229,   202,
    -229,  -229,  -229,  -229,  -229,   312,  -229,   359,   418,   258,
    -229,  -229,  -229,  -229,  -229,  -229,   331,  -229,  -229,   310,
     360,    65,  -229,  -229,  -229,  -229,  -229,  -229,   361,   421,
      28,    30,   411,   411,   391,  -229,   420,   392,   408,  -229,
     412,   413,   414,   415,   416,   417,   156,   156,   419,   156,
     370,   370,   422,   370,   156,   156,  -229,  -229,   392,  -229,
     248,   392,  -229,   373,  -229,  -229,   341,   341,   377,  -229,
    -229,  -229,  -229,   341,   341,  -229,   310,  -229,   374,  -229,
      88,   156,   156,   247,   156,  -229,  -229,   -32,   258,   247,
     434,   137,   156,   156,   156,   156,   156,   156,   423,  -229,
     156,  -229,   425,   426,   156,   427,   428,   429,   434,   375,
     434,   379,  -229,   320,   341,  -229,  -229,  -229,    65,   395,
    -229,  -229,   248,   248,    38,   248,   434,  -229,   382,   254,
     254,  -229,   433,   445,   424,  -229,   390,   435,   436,   437,
     438,   439,   440,   150,   441,   156,   156,  -229,   156,   172,
     175,   424,   392,   424,  -229,  -229,   338,  -229,  -229,   375,
     375,   156,   375,   424,   258,   331,   331,   247,   258,   406,
    -229,   196,   214,   230,   277,   279,   336,  -229,   390,   337,
    -229,  -229,  -229,  -229,   390,  -229,   390,  -229,   434,  -229,
    -229,   392,   392,   248,   392,  -229,  -229,  -229,  -229,  -229,
      23,   388,  -229,  -229,  -229,   390,  -229,   390,  -229,   390,
    -229,   390,  -229,   390,  -229,   390,  -229,   390,   424,   424,
     424,   375,   424,  -229,  -229,  -229,   258,  -229,  -229,  -229,
     392,  -229,  -229,   424,  -229
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       9,     6,    10,     7,     8,    27,    15,     0,     0,     0,
       0,     0,   160,    20,     0,     0,     0,   158,   159,     0,
       0,     0,     0,     0,     0,   161,     0,    64,     0,     0,
      86,     0,    63,     0,     0,    28,     1,     2,     0,     0,
       0,    19,     0,     0,    58,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      29,    16,     0,     0,     0,     0,     0,     0,    25,   161,
      58,    76,     0,    17,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    58,   105,    99,    96,    66,     0,    58,
      65,     0,    58,    87,    62,     0,     0,    41,     0,     0,
      43,     0,     0,    23,     0,    60,    59,     0,     0,    26,
       0,     0,     0,    96,    96,    96,    96,    96,    96,     0,
       0,     0,     0,     0,   138,     0,   136,   136,     0,     0,
     136,     0,     0,     0,     0,     0,    98,   100,   138,     0,
       0,   138,    18,     0,    46,     0,    48,    45,    21,     0,
      22,    55,    53,    54,    56,     0,    49,     0,     0,     0,
      72,    71,    73,    68,    69,    70,     0,    77,    78,    79,
       0,    96,    88,    89,    92,    93,    94,    95,     0,     0,
       0,     0,     0,     0,     0,   101,     0,   146,   108,   137,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     136,   136,     0,   136,     0,     0,   135,    97,   146,    67,
      58,   146,    42,     0,    44,    51,     0,     0,     0,    61,
      74,    75,    57,     0,     0,    83,    80,    81,     0,    90,
       0,     0,     0,     0,     0,    39,    40,     0,     0,     0,
     149,     0,     0,     0,     0,     0,     0,     0,     0,   117,
       0,   118,     0,     0,     0,     0,     0,     0,   149,   138,
     149,     0,    50,     0,     0,    84,    85,    82,    96,     0,
     103,   102,    58,    58,     0,    58,   149,   140,   139,     0,
       0,   144,   147,     0,   156,   107,   106,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   119,     0,     0,
       0,   156,   146,   156,    47,    52,     0,    91,   104,   138,
     138,     0,   138,   156,     0,     0,     0,     0,     0,     0,
      31,     0,     0,     0,     0,     0,     0,   110,   109,     0,
     120,   121,   122,   124,   123,   130,   129,    32,   149,    33,
      24,   146,   146,    58,   146,    30,   141,   142,   143,   145,
     155,   148,   150,   157,   126,   125,   132,   131,   112,   111,
     128,   127,   134,   133,   114,   113,   116,   115,   156,   156,
     156,   138,   156,   154,   153,   152,     0,    34,    35,    36,
     146,    37,   151,   156,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -229,  -229,  -229,  -229,  -229,  -229,  -229,  -229,    17,   -23,
    -229,  -229,   327,   300,  -229,  -215,  -229,  -119,   294,   -89,
     -36,    -9,   334,   -87,   -55,  -229,   347,  -229,   231,   -67,
     -74,   -88,   -70,  -128,  -229,    45,  -122,  -156,  -229,   139,
    -229,  -207,  -186,  -229,    82,  -229,  -228,  -229,     5,     0
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,    25,   141,
     142,   116,   119,   117,   167,   175,   123,   176,   125,    88,
     126,   127,    48,   186,   242,    90,    91,   246,   247,    49,
      50,   157,   103,   104,   291,   105,   212,   207,   298,   301,
     302,   260,   304,   371,   372,   395,   340,    39,    51,    52
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      47,   129,   228,   113,   109,   231,   112,    74,   111,    33,
      28,   188,    36,   140,   144,    76,    26,   208,    87,   160,
     158,   278,   283,   161,   280,   215,   226,    55,   218,   203,
      29,   393,    32,    59,    60,    61,    62,   394,    27,    63,
      64,   252,    34,   254,    47,   192,   193,   194,   195,   196,
     197,   331,   101,   204,    85,    37,    38,    32,    96,    97,
      98,    99,   100,    81,   108,    92,   110,   240,    75,   326,
     245,    80,    53,   201,    54,   128,    77,   106,   114,   106,
      77,   106,   118,   120,   120,   132,   120,    35,   268,   269,
     230,   271,   321,   357,   323,   359,   276,   277,   272,   273,
      57,   275,   253,   249,    77,   365,    30,   106,    56,   155,
     333,    45,    77,   106,    86,   358,    58,   282,   131,    66,
     106,   219,   190,   322,   285,   286,    31,   245,    92,   156,
     189,    47,   289,    65,   307,   308,   309,   310,   311,   312,
      67,   279,   314,   220,   221,   222,   317,   143,   223,   162,
     106,   163,   290,   143,   389,   390,   198,   392,    68,   106,
     397,   398,   399,   118,   401,   106,   101,    69,    94,   234,
      70,    32,   388,   361,   362,   404,   364,   241,   102,   255,
     256,   292,   293,    45,   295,   300,   294,   350,   351,   168,
     352,   169,    95,   403,    40,    41,    42,    43,    44,    71,
     327,    45,  -160,   329,   330,   305,   332,    40,    41,    42,
      43,    44,   335,   336,    45,   101,   240,   240,   347,    45,
      32,   106,   106,    72,   106,   306,    73,   108,    46,   106,
     106,    53,    45,   209,    78,   400,    45,   210,   211,    45,
     353,    79,   209,   355,   110,    82,   213,   214,    83,   297,
     299,    40,    41,    42,    43,    44,   106,   106,    84,   106,
      45,   363,   296,   300,   374,    87,    89,   106,   106,   106,
     106,   106,   106,   145,   391,   106,    45,   348,    45,   106,
     367,   368,   376,   354,   356,   199,    40,    41,    42,    43,
      44,   164,   165,   166,    45,   146,   147,   148,   378,   149,
     150,   151,   152,   153,    93,   375,   377,   379,   381,   383,
     385,    45,   115,   387,   180,   181,   182,   205,   133,   154,
     106,   106,    45,   106,   183,   366,   241,   241,   299,   370,
     184,   185,   170,     1,   169,     2,   106,     3,     4,     5,
     107,    45,     6,    45,   145,   380,   122,   382,     7,     8,
       9,    10,    40,    41,    42,    43,    44,   178,   130,   169,
      11,    12,    13,    14,    15,    16,   146,   147,   148,   134,
     149,   150,   151,   152,   153,   171,   172,   173,   174,    89,
     171,   172,   173,   174,   235,   139,   236,   370,   243,   244,
     154,   135,   325,    17,   236,    45,   171,   172,   173,   174,
      45,    45,   159,   177,   384,   386,   171,   172,   173,   174,
     360,   121,   236,   124,   136,   137,   138,   179,   191,   202,
     206,   216,   217,   209,   224,   225,   227,   229,   233,   238,
     237,   140,   248,   250,   251,   257,   258,   261,   259,   281,
     262,   263,   264,   265,   266,   267,   288,   270,   284,   303,
     274,   324,   313,   315,   316,   318,   334,   319,   320,   328,
     337,   338,   396,   232,   341,   342,   343,   344,   345,   346,
     349,   339,   373,   239,   200,   187,   369,   287,   402
};

static const yytype_int16 yycheck[] =
{
       9,    90,   158,    77,    74,   161,    76,    13,    75,     4,
       6,   130,     7,    20,   103,    13,     4,   145,    17,    13,
     109,   228,   237,   112,   231,   147,   154,    10,   150,    48,
      26,     8,    64,    28,    29,    30,    31,    14,    26,    34,
      35,    13,    10,    13,    53,   133,   134,   135,   136,   137,
     138,    13,    59,    72,    11,    37,    38,    64,    67,    68,
      69,    70,    71,    58,    71,    65,    75,   186,    74,   284,
     189,    54,    20,   140,    22,    74,    74,    72,    78,    74,
      74,    76,    82,    83,    84,    94,    86,    13,   216,   217,
     160,   219,   278,   321,   280,   323,   224,   225,   220,   221,
      69,   223,    74,   191,    74,   333,     6,   102,     0,    44,
     296,    64,    74,   108,    71,   322,    13,   236,    71,    70,
     115,    28,   131,   279,   243,   244,    26,   246,   128,    64,
     130,   140,    44,    19,   262,   263,   264,   265,   266,   267,
      71,   230,   270,    50,    51,    52,   274,   102,    55,    72,
     145,    74,    64,   108,   361,   362,   139,   364,    71,   154,
     388,   389,   390,   163,   392,   160,    59,    71,    49,   169,
      71,    64,   358,   329,   330,   403,   332,   186,    71,   202,
     203,   251,   252,    64,   254,   259,   253,   315,   316,    72,
     318,    74,    73,   400,    39,    40,    41,    42,    43,    71,
     288,    64,    75,   292,   293,    68,   295,    39,    40,    41,
      42,    43,   299,   300,    64,    59,   335,   336,    68,    64,
      64,   216,   217,    13,   219,   261,    44,    71,    73,   224,
     225,    20,    64,    53,    75,   391,    64,    57,    58,    64,
      68,    73,    53,    68,   253,    71,    57,    58,    71,   258,
     259,    39,    40,    41,    42,    43,   251,   252,    71,   254,
      64,   331,   257,   337,    68,    17,    64,   262,   263,   264,
     265,   266,   267,    28,   363,   270,    64,   313,    64,   274,
     335,   336,    68,   319,   320,    73,    39,    40,    41,    42,
      43,    23,    24,    25,    64,    50,    51,    52,    68,    54,
      55,    56,    57,    58,    68,   341,   342,   343,   344,   345,
     346,    64,    13,   349,    60,    61,    62,    72,    72,    74,
     315,   316,    64,   318,    70,   334,   335,   336,   337,   338,
      76,    77,    72,     3,    74,     5,   331,     7,     8,     9,
      64,    64,    12,    64,    28,    68,    71,    68,    18,    19,
      20,    21,    39,    40,    41,    42,    43,    72,    70,    74,
      30,    31,    32,    33,    34,    35,    50,    51,    52,    72,
      54,    55,    56,    57,    58,    65,    66,    67,    68,    64,
      65,    66,    67,    68,    72,    71,    74,   396,    78,    79,
      74,    72,    72,    63,    74,    64,    65,    66,    67,    68,
      64,    64,    44,    74,    68,    68,    65,    66,    67,    68,
      72,    84,    74,    86,    72,    72,    72,    27,    72,    48,
      45,    28,    28,    53,    28,    28,    64,    64,    71,    11,
      71,    20,    72,    72,    13,    44,    16,    29,    46,    66,
      28,    28,    28,    28,    28,    28,    72,    28,    71,    15,
      28,    72,    29,    28,    28,    28,    74,    29,    29,    64,
      27,    16,    74,   163,    29,    29,    29,    29,    29,    29,
      29,    47,    66,   179,   140,   128,   337,   246,   396
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    30,    31,    32,    33,    34,    35,    63,    81,    82,
      83,    84,    85,    86,    87,    88,     4,    26,     6,    26,
       6,    26,    64,   128,    10,    13,   128,    37,    38,   127,
      39,    40,    41,    42,    43,    64,    73,   101,   102,   109,
     110,   128,   129,    20,    22,    88,     0,    69,    13,   128,
     128,   128,   128,   128,   128,    19,    70,    71,    71,    71,
      71,    71,    13,    44,    13,    74,    13,    74,    75,    73,
      88,   128,    71,    71,    71,    11,    71,    17,    99,    64,
     105,   106,   129,    68,    49,    73,   101,   101,   101,   101,
     101,    59,    71,   112,   113,   115,   128,    64,    71,   112,
     101,   109,   112,   110,   129,    13,    91,    93,   129,    92,
     129,    92,    71,    96,    92,    98,   100,   101,    74,    99,
      70,    71,   101,    72,    72,    72,    72,    72,    72,    71,
      20,    89,    90,   115,    99,    28,    50,    51,    52,    54,
      55,    56,    57,    58,    74,    44,    64,   111,    99,    44,
      13,    99,    72,    74,    23,    24,    25,    94,    72,    74,
      72,    65,    66,    67,    68,    95,    97,    74,    72,    27,
      60,    61,    62,    70,    76,    77,   103,   106,    97,   129,
     101,    72,   111,   111,   111,   111,   111,   111,    88,    73,
     102,   109,    48,    48,    72,    72,    45,   117,   113,    53,
      57,    58,   116,    57,    58,   116,    28,    28,   116,    28,
      50,    51,    52,    55,    28,    28,   113,    64,   117,    64,
     112,   117,    93,    71,   129,    72,    74,    71,    11,    98,
      97,   101,   104,    78,    79,    97,   107,   108,    72,   111,
      72,    13,    13,    74,    13,    89,    89,    44,    16,    46,
     121,    29,    28,    28,    28,    28,    28,    28,   113,   113,
      28,   113,   116,   116,    28,   116,   113,   113,   121,    99,
     121,    66,    97,    95,    71,    97,    97,   108,    72,    44,
      64,   114,   112,   112,   109,   112,   128,   101,   118,   101,
     110,   119,   120,    15,   122,    68,   100,   113,   113,   113,
     113,   113,   113,    29,   113,    28,    28,   113,    28,    29,
      29,   122,   117,   122,    72,    72,    95,   111,    64,    99,
      99,    13,    99,   122,    74,   103,   103,    27,    16,    47,
     126,    29,    29,    29,    29,    29,    29,    68,   100,    29,
     113,   113,   113,    68,   100,    68,   100,   126,   121,   126,
      72,   117,   117,   112,   117,   126,   101,   104,   104,   119,
     101,   123,   124,    66,    68,   100,    68,   100,    68,   100,
      68,   100,    68,   100,    68,   100,    68,   100,   122,   121,
     121,    99,   121,     8,    14,   125,    74,   126,   126,   126,
     117,   126,   124,   121,   126
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    80,    81,    81,    81,    81,    82,    82,    82,    82,
      82,    83,    83,    83,    83,    84,    84,    85,    86,    86,
      86,    86,    86,    87,    87,    87,    87,    87,    87,    87,
      87,    88,    88,    88,    88,    89,    89,    89,    89,    90,
      90,    91,    91,    92,    92,    93,    94,    94,    94,    95,
      95,    96,    96,    97,    97,    97,    97,    98,    99,    99,
     100,   100,   101,   101,   102,   102,   102,   102,   103,   103,
     103,   103,   103,   103,   104,   104,   105,   105,   106,   106,
     106,   107,   107,   108,   108,   108,   109,   109,   110,   110,
     110,   110,   110,   110,   110,   110,   111,   111,   111,   112,
     113,   113,   113,   114,   114,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   116,   116,   117,   117,
     118,   118,   119,   119,   120,   120,   121,   121,   122,   122,
     123,   123,   124,   125,   125,   125,   126,   126,   127,   127,
     128,   129
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
       1,     3,     3,     1,     1,     3,     3,     5,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     3,     3,
       4,     1,     2,     1,     2,     2,     1,     3,     5,     5,
       6,     8,     5,     5,     5,     5,     0,     2,     1,     1,
       2,     3,     5,     1,     2,     1,     5,     5,     3,     6,
       6,     7,     7,     7,     7,     7,     7,     4,     4,     5,
       6,     6,     6,     6,     6,     7,     7,     7,     7,     6,
       6,     7,     7,     7,     7,     3,     0,     1,     0,     3,
       1,     3,     3,     3,     1,     3,     0,     2,     3,     0,
       1,     3,     2,     1,     1,     0,     0,     2,     1,     1,
       1,     1
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
#line 83 "/root/csc-db/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1894 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 88 "/root/csc-db/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1903 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 93 "/root/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1912 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 98 "/root/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1921 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 114 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1929 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 118 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1937 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 122 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1945 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 126 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1953 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 133 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1961 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 137 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1969 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 144 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1977 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 151 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1985 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 155 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1993 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 159 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 2001 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 163 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2009 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 167 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2017 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 174 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 2025 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 178 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 2033 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 182 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 2041 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 186 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 2049 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: select_stmt  */
#line 190 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select);
    }
#line 2057 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN select_stmt  */
#line 194 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), false);
    }
#line 2065 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE select_stmt  */
#line 198 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), true);
    }
#line 2073 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 30: /* dml: SELECT '*' FROM '(' union_query ')' AS tbName opt_order_clause opt_limit  */
#line 202 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_selects), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2081 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 31: /* select_stmt: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 209 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2089 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 32: /* select_stmt: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 213 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2097 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 33: /* select_stmt: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 217 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2105 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 34: /* select_stmt: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 221 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-9].sv_cols), (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2113 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 35: /* union_branch: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 228 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2121 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 36: /* union_branch: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 232 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-6].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2129 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 37: /* union_branch: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 236 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2137 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 38: /* union_branch: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 240 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-8].sv_cols), (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2145 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 39: /* union_query: union_branch UNION union_branch  */
#line 247 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_selects) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select), (yyvsp[0].sv_select)};
    }
#line 2153 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 40: /* union_query: union_query UNION union_branch  */
#line 251 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_selects) = (yyvsp[-2].sv_selects);
        (yyval.sv_selects).push_back((yyvsp[0].sv_select));
    }
#line 2162 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 41: /* fieldList: field  */
#line 259 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2170 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 42: /* fieldList: fieldList ',' field  */
#line 263 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2178 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 43: /* colNameList: colName  */
#line 270 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2186 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 44: /* colNameList: colNameList ',' colName  */
#line 274 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2194 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 45: /* field: colName type  */
#line 281 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2202 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 46: /* type: INT  */
#line 288 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2210 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 47: /* type: CHAR '(' VALUE_INT ')'  */
#line 292 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2218 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 48: /* type: FLOAT  */
#line 296 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2226 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 49: /* valueList: value  */
#line 303 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2234 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 50: /* valueList: valueList ',' value  */
#line 307 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2242 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 51: /* valueRows: '(' valueList ')'  */
#line 314 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2250 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 52: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 318 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2259 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 53: /* value: VALUE_INT  */
#line 326 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2267 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_FLOAT  */
#line 330 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2275 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 55: /* value: VALUE_STRING  */
#line 334 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2283 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_BOOL  */
#line 338 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2291 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 57: /* condition: col op expr  */
#line 345 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2299 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 58: /* optWhereClause: %empty  */
#line 351 "/root/csc-db/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2305 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 59: /* optWhereClause: WHERE whereClause  */
#line 353 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2313 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 60: /* whereClause: condition  */
#line 360 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2321 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 61: /* whereClause: whereClause AND condition  */
#line 364 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2329 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 62: /* col: tbName '.' colName  */
#line 371 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2337 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 63: /* col: colName  */
#line 375 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2345 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 64: /* colList: col  */
#line 382 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2353 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 65: /* colList: colList ',' col  */
#line 386 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2361 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 66: /* colList: col AS IDENTIFIER  */
#line 390 "/root/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2371 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 67: /* colList: colList ',' col AS IDENTIFIER  */
#line 396 "/root/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2380 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 68: /* op: '='  */
#line 404 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2388 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 69: /* op: '<'  */
#line 408 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2396 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 70: /* op: '>'  */
#line 412 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2404 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 71: /* op: NEQ  */
#line 416 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2412 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 72: /* op: LEQ  */
#line 420 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2420 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 73: /* op: GEQ  */
#line 424 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2428 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 74: /* expr: value  */
#line 431 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2436 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 75: /* expr: col  */
#line 435 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2444 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 76: /* setClauses: setClause  */
#line 442 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2452 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 77: /* setClauses: setClauses ',' setClause  */
#line 446 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2460 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 78: /* setClause: colName '=' value  */
#line 453 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2468 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 79: /* setClause: colName '=' colName  */
#line 457 "/root/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2480 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 80: /* setClause: colName '=' colName arith_chain  */
#line 465 "/root/csc-db/src/parser/yacc.y"
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_vals)[0], false);
        if ((yyvsp[0].sv_vals).size() > 1) (yyval.sv_set_clause)->chain = (yyvsp[0].sv_vals);
    }
#line 2492 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 81: /* arith_chain: arith_term  */
#line 476 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2500 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 82: /* arith_chain: arith_chain arith_term  */
#line 480 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2508 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 83: /* arith_term: value  */
#line 487 "/root/csc-db/src/parser/yacc.y"
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2517 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 84: /* arith_term: '+' value  */
#line 492 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2525 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 85: /* arith_term: '-' value  */
#line 496 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = negate_value((yyvsp[0].sv_val));
        if ((yyval.sv_val) == nullptr) YYERROR;
    }
#line 2534 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 86: /* agg_list: agg_func  */
#line 506 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2542 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 87: /* agg_list: agg_list ',' agg_func  */
#line 510 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2550 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 88: /* agg_func: COUNT '(' '*' ')' opt_alias  */
#line 517 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, nullptr, (yyvsp[0].sv_str), true);
    }
#line 2558 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 89: /* agg_func: COUNT '(' col ')' opt_alias  */
#line 521 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2566 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 90: /* agg_func: COUNT '(' DISTINCT col ')' opt_alias  */
#line 525 "/root/csc-db/src/parser/yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2575 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 91: /* agg_func: COUNT '(' DISTINCT '(' col ')' ')' opt_alias  */
#line 530 "/root/csc-db/src/parser/yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2584 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 92: /* agg_func: MAX '(' col ')' opt_alias  */
#line 535 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MAX, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2592 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 93: /* agg_func: MIN '(' col ')' opt_alias  */
#line 539 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MIN, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2600 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 94: /* agg_func: SUM '(' col ')' opt_alias  */
#line 543 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_SUM, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2608 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 95: /* agg_func: AVG '(' col ')' opt_alias  */
#line 547 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_AVG, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2616 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 96: /* opt_alias: %empty  */
#line 554 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2624 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 97: /* opt_alias: AS IDENTIFIER  */
#line 558 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2632 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 98: /* opt_alias: IDENTIFIER  */
#line 562 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2640 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 99: /* from_clause: joined_table  */
#line 569 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2648 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 100: /* table_ref: tbName opt_alias  */
#line 577 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<TableRef>((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2656 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 101: /* table_ref: '(' joined_table ')'  */
#line 581 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[-1].sv_from);
    }
#line 2664 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 102: /* table_ref: LATERAL '(' select_stmt ')' required_alias  */
#line 585 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<LateralRef>((yyvsp[-2].sv_select), (yyvsp[0].sv_str));
    }
#line 2672 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 103: /* required_alias: IDENTIFIER  */
#line 592 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2680 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 104: /* required_alias: AS IDENTIFIER  */
#line 596 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2688 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 105: /* joined_table: table_ref  */
#line 604 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2696 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 106: /* joined_table: joined_table JOIN table_ref ON whereClause  */
#line 608 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2705 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 107: /* joined_table: joined_table JOIN table_ref ON VALUE_BOOL  */
#line 613 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2716 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 108: /* joined_table: joined_table JOIN table_ref  */
#line 620 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2726 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 109: /* joined_table: joined_table INNER JOIN table_ref ON whereClause  */
#line 626 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2735 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 110: /* joined_table: joined_table INNER JOIN table_ref ON VALUE_BOOL  */
#line 631 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2746 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 111: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON whereClause  */
#line 638 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2755 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 112: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 643 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2766 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 113: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON whereClause  */
#line 650 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2775 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 114: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 655 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2786 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 115: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON whereClause  */
#line 662 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2795 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 116: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON VALUE_BOOL  */
#line 667 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2806 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 117: /* joined_table: joined_table CROSS JOIN table_ref  */
#line 674 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2816 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 118: /* joined_table: joined_table NATURAL JOIN table_ref  */
#line 680 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2826 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 119: /* joined_table: joined_table NATURAL INNER JOIN table_ref  */
#line 686 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2836 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 120: /* joined_table: joined_table NATURAL LEFT opt_outer JOIN table_ref  */
#line 692 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2846 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 121: /* joined_table: joined_table NATURAL RIGHT opt_outer JOIN table_ref  */
#line 698 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2856 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 122: /* joined_table: joined_table NATURAL FULL opt_outer JOIN table_ref  */
#line 704 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-5].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            true, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2866 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 123: /* joined_table: joined_table SEMI JOIN table_ref ON whereClause  */
#line 710 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2875 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 124: /* joined_table: joined_table SEMI JOIN table_ref ON VALUE_BOOL  */
#line 715 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2886 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 125: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON whereClause  */
#line 722 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2895 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 126: /* joined_table: joined_table LEFT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 727 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2906 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 127: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON whereClause  */
#line 734 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2915 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 128: /* joined_table: joined_table RIGHT SEMI JOIN table_ref ON VALUE_BOOL  */
#line 739 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_SEMI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2926 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 129: /* joined_table: joined_table ANTI JOIN table_ref ON whereClause  */
#line 746 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2935 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 130: /* joined_table: joined_table ANTI JOIN table_ref ON VALUE_BOOL  */
#line 751 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2946 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 131: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON whereClause  */
#line 758 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2955 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 132: /* joined_table: joined_table LEFT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 763 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2966 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 133: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON whereClause  */
#line 770 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds), false, is_lateral_ref((yyvsp[-2].sv_from)));
    }
#line 2975 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 134: /* joined_table: joined_table RIGHT ANTI JOIN table_ref ON VALUE_BOOL  */
#line 775 "/root/csc-db/src/parser/yacc.y"
    {
        if (!(yyvsp[0].sv_bool)) YYERROR;
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_ANTI_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[-2].sv_from)), true);
    }
#line 2986 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 135: /* joined_table: joined_table ',' table_ref  */
#line 782 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{},
            false, is_lateral_ref((yyvsp[0].sv_from)));
    }
#line 2996 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 138: /* opt_group_by: %empty  */
#line 797 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 3004 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 139: /* opt_group_by: GROUP BY group_by_list  */
#line 801 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 3012 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 140: /* group_by_list: col  */
#line 808 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 3020 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 141: /* group_by_list: group_by_list ',' col  */
#line 812 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 3028 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 142: /* having_condition: col op expr  */
#line 819 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3036 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 143: /* having_condition: agg_func op expr  */
#line 823 "/root/csc-db/src/parser/yacc.y"
    {
        // 保留聚合实参的限定符，Analyzer 才能拒绝 SEMI/ANTI 已隐藏侧的
        // HAVING count(hidden.col)；函数文本仍沿用现有内部表示。
        auto col = std::make_shared<Col>(
            (!(yyvsp[-2].sv_agg_expr)->is_star && (yyvsp[-2].sv_agg_expr)->col) ? (yyvsp[-2].sv_agg_expr)->col->tab_name : "", (yyvsp[-2].sv_agg_expr)->to_string());
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(col, (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 3048 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 144: /* having_clause: having_condition  */
#line 834 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 3056 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 145: /* having_clause: having_clause AND having_condition  */
#line 838 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 3064 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 146: /* opt_having: %empty  */
#line 845 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = {};
    }
#line 3072 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 147: /* opt_having: HAVING having_clause  */
#line 849 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 3080 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 148: /* opt_order_clause: ORDER BY order_list  */
#line 856 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 3088 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 149: /* opt_order_clause: %empty  */
#line 860 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 3096 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 150: /* order_list: order_clause  */
#line 867 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 3104 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 151: /* order_list: order_list ',' order_clause  */
#line 871 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 3112 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 152: /* order_clause: col opt_asc_desc  */
#line 878 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 3120 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 153: /* opt_asc_desc: ASC  */
#line 884 "/root/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 3126 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 154: /* opt_asc_desc: DESC  */
#line 885 "/root/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 3132 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 155: /* opt_asc_desc: %empty  */
#line 886 "/root/csc-db/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 3138 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 156: /* opt_limit: %empty  */
#line 891 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 3146 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 157: /* opt_limit: LIMIT VALUE_INT  */
#line 895 "/root/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 3154 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 158: /* set_knob_type: ENABLE_NESTLOOP  */
#line 901 "/root/csc-db/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 3160 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 159: /* set_knob_type: ENABLE_SORTMERGE  */
#line 902 "/root/csc-db/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 3166 "/root/csc-db/src/parser/yacc.tab.cpp"
    break;


#line 3170 "/root/csc-db/src/parser/yacc.tab.cpp"

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

#line 908 "/root/csc-db/src/parser/yacc.y"
