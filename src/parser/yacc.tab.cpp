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

#line 103 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

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
  YYSYMBOL_LEQ = 56,                       /* LEQ  */
  YYSYMBOL_NEQ = 57,                       /* NEQ  */
  YYSYMBOL_GEQ = 58,                       /* GEQ  */
  YYSYMBOL_T_EOF = 59,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 60,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 61,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 62,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 63,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 64,                /* VALUE_BOOL  */
  YYSYMBOL_65_ = 65,                       /* ';'  */
  YYSYMBOL_66_ = 66,                       /* '='  */
  YYSYMBOL_67_ = 67,                       /* '('  */
  YYSYMBOL_68_ = 68,                       /* ')'  */
  YYSYMBOL_69_ = 69,                       /* '*'  */
  YYSYMBOL_70_ = 70,                       /* ','  */
  YYSYMBOL_71_ = 71,                       /* '.'  */
  YYSYMBOL_72_ = 72,                       /* '<'  */
  YYSYMBOL_73_ = 73,                       /* '>'  */
  YYSYMBOL_74_ = 74,                       /* '+'  */
  YYSYMBOL_75_ = 75,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 76,                  /* $accept  */
  YYSYMBOL_start = 77,                     /* start  */
  YYSYMBOL_stmt = 78,                      /* stmt  */
  YYSYMBOL_txnStmt = 79,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 80,                    /* dbStmt  */
  YYSYMBOL_setStmt = 81,                   /* setStmt  */
  YYSYMBOL_ddl = 82,                       /* ddl  */
  YYSYMBOL_dml = 83,                       /* dml  */
  YYSYMBOL_select_stmt = 84,               /* select_stmt  */
  YYSYMBOL_union_branch = 85,              /* union_branch  */
  YYSYMBOL_union_query = 86,               /* union_query  */
  YYSYMBOL_fieldList = 87,                 /* fieldList  */
  YYSYMBOL_colNameList = 88,               /* colNameList  */
  YYSYMBOL_field = 89,                     /* field  */
  YYSYMBOL_type = 90,                      /* type  */
  YYSYMBOL_valueList = 91,                 /* valueList  */
  YYSYMBOL_valueRows = 92,                 /* valueRows  */
  YYSYMBOL_value = 93,                     /* value  */
  YYSYMBOL_condition = 94,                 /* condition  */
  YYSYMBOL_optWhereClause = 95,            /* optWhereClause  */
  YYSYMBOL_whereClause = 96,               /* whereClause  */
  YYSYMBOL_col = 97,                       /* col  */
  YYSYMBOL_colList = 98,                   /* colList  */
  YYSYMBOL_op = 99,                        /* op  */
  YYSYMBOL_expr = 100,                     /* expr  */
  YYSYMBOL_setClauses = 101,               /* setClauses  */
  YYSYMBOL_setClause = 102,                /* setClause  */
  YYSYMBOL_arith_chain = 103,              /* arith_chain  */
  YYSYMBOL_arith_term = 104,               /* arith_term  */
  YYSYMBOL_agg_list = 105,                 /* agg_list  */
  YYSYMBOL_agg_func = 106,                 /* agg_func  */
  YYSYMBOL_agg_name = 107,                 /* agg_name  */
  YYSYMBOL_opt_alias = 108,                /* opt_alias  */
  YYSYMBOL_from_clause = 109,              /* from_clause  */
  YYSYMBOL_table_ref = 110,                /* table_ref  */
  YYSYMBOL_joined_table = 111,             /* joined_table  */
  YYSYMBOL_opt_outer = 112,                /* opt_outer  */
  YYSYMBOL_opt_group_by = 113,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 114,            /* group_by_list  */
  YYSYMBOL_having_condition = 115,         /* having_condition  */
  YYSYMBOL_having_clause = 116,            /* having_clause  */
  YYSYMBOL_opt_having = 117,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 118,         /* opt_order_clause  */
  YYSYMBOL_order_list = 119,               /* order_list  */
  YYSYMBOL_order_clause = 120,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 121,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit = 122,                /* opt_limit  */
  YYSYMBOL_set_knob_type = 123,            /* set_knob_type  */
  YYSYMBOL_tbName = 124,                   /* tbName  */
  YYSYMBOL_colName = 125                   /* colName  */
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
#define YYLAST   370

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  76
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  50
/* YYNRULES -- Number of rules.  */
#define YYNRULES  138
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  325

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   319


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
      67,    68,    69,    74,    70,    75,    71,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    65,
      72,    66,    73,     2,     2,     2,     2,     2,     2,     2,
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
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    88,    88,    93,    98,   103,   111,   112,   113,   114,
     115,   119,   123,   127,   131,   138,   142,   149,   156,   160,
     164,   168,   172,   179,   183,   187,   191,   195,   199,   203,
     207,   214,   218,   222,   226,   233,   237,   241,   245,   252,
     256,   264,   268,   275,   279,   286,   293,   297,   301,   308,
     312,   319,   323,   331,   335,   339,   343,   350,   357,   358,
     365,   369,   376,   380,   387,   391,   395,   401,   409,   413,
     417,   421,   425,   429,   436,   440,   447,   451,   458,   462,
     470,   481,   485,   492,   497,   501,   511,   515,   522,   527,
     532,   538,   547,   548,   549,   550,   551,   552,   561,   564,
     568,   575,   583,   587,   595,   599,   604,   609,   614,   619,
     624,   629,   634,   641,   643,   649,   652,   659,   663,   670,
     674,   681,   685,   693,   696,   703,   708,   714,   718,   725,
     732,   733,   734,   739,   742,   749,   750,   753,   755
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
  "LEQ", "NEQ", "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT",
  "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='", "'('", "')'", "'*'", "','",
  "'.'", "'<'", "'>'", "'+'", "'-'", "$accept", "start", "stmt", "txnStmt",
  "dbStmt", "setStmt", "ddl", "dml", "select_stmt", "union_branch",
  "union_query", "fieldList", "colNameList", "field", "type", "valueList",
  "valueRows", "value", "condition", "optWhereClause", "whereClause",
  "col", "colList", "op", "expr", "setClauses", "setClause", "arith_chain",
  "arith_term", "agg_list", "agg_func", "agg_name", "opt_alias",
  "from_clause", "table_ref", "joined_table", "opt_outer", "opt_group_by",
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

#define YYPACT_NINF (-193)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-138)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     229,    32,    61,    67,   -20,    51,    85,   -20,    86,   162,
      91,  -193,  -193,  -193,  -193,  -193,  -193,  -193,   102,    84,
    -193,  -193,  -193,  -193,  -193,  -193,  -193,   140,   -20,   -20,
     -20,   -20,  -193,  -193,   -20,   -20,   142,  -193,  -193,   147,
    -193,  -193,  -193,  -193,  -193,     9,   207,   126,   -10,     1,
    -193,   160,   164,  -193,   212,   219,  -193,  -193,  -193,   -20,
     191,   213,  -193,   220,    -3,   262,   234,   236,   -23,   239,
     -21,   243,   -21,   250,    -6,   234,   288,  -193,  -193,   234,
     234,   234,   238,   234,   242,  -193,  -193,     8,  -193,   245,
    -193,    21,   262,  -193,   145,    45,  -193,   -21,   262,   264,
       4,   262,  -193,  -193,   -18,   253,   241,   257,  -193,   -21,
      76,  -193,   157,    98,  -193,   118,    94,   256,   123,  -193,
     300,   240,   234,  -193,   254,   235,   280,    31,   124,   284,
     -21,   277,   277,   303,   304,   277,   -21,   273,  -193,  -193,
     284,   274,   -21,   284,   242,   267,    45,    45,  -193,   234,
    -193,   269,  -193,  -193,  -193,   234,  -193,  -193,  -193,  -193,
    -193,   138,  -193,   270,   327,   242,  -193,  -193,  -193,  -193,
    -193,  -193,   259,  -193,  -193,   182,   326,     7,    14,   320,
     320,   297,  -193,   328,   296,   314,  -193,   317,   318,   -21,
     -21,   319,  -193,  -193,   296,  -193,   262,   296,   281,    45,
    -193,  -193,  -193,   286,  -193,  -193,    94,    94,   283,  -193,
    -193,  -193,  -193,    94,    94,  -193,   182,  -193,   -21,   -21,
     243,   -21,  -193,  -193,   -20,   242,   243,   336,   242,   -21,
     -21,   323,  -193,   -21,   336,   284,   336,   285,  -193,   287,
    -193,   155,    94,  -193,  -193,  -193,   262,   262,    20,   262,
     336,  -193,   289,   240,   240,  -193,   329,   338,   310,   300,
     331,   332,   242,   333,   310,   296,   310,    45,  -193,  -193,
     158,   284,   284,   -21,   284,   310,   242,   259,   259,   243,
     242,   301,  -193,   242,   242,   300,   242,  -193,   336,  -193,
    -193,  -193,   296,   296,   262,   296,  -193,  -193,  -193,  -193,
    -193,   104,   294,  -193,  -193,   300,   300,   300,   310,   310,
     310,   284,   310,  -193,  -193,  -193,   242,  -193,  -193,  -193,
     296,  -193,  -193,   310,  -193
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       9,     6,    10,     7,     8,    27,    15,     0,     0,     0,
       0,     0,   137,    20,     0,     0,     0,   135,   136,     0,
      92,    93,    94,    95,    96,   138,     0,    64,     0,     0,
      86,     0,     0,    63,     0,     0,    28,     1,     2,     0,
       0,     0,    19,     0,     0,    58,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    29,    16,     0,
       0,     0,     0,     0,     0,    25,   138,    58,    76,     0,
      17,     0,    58,   104,   101,    98,    66,     0,    58,    65,
       0,    58,    97,    87,     0,   138,     0,     0,    62,     0,
       0,    41,     0,     0,    43,     0,     0,    23,     0,    60,
      59,     0,     0,    26,     0,     0,     0,     0,     0,   115,
       0,   113,   113,     0,     0,   113,     0,     0,   100,   102,
     115,     0,     0,   115,     0,     0,    98,    98,    18,     0,
      46,     0,    48,    45,    21,     0,    22,    55,    53,    54,
      56,     0,    49,     0,     0,     0,    72,    71,    73,    68,
      69,    70,     0,    77,    78,    79,     0,     0,     0,     0,
       0,     0,   103,     0,   123,   106,   114,     0,     0,     0,
       0,     0,   112,    99,   123,    67,    58,   123,     0,    98,
      88,    89,    42,     0,    44,    51,     0,     0,     0,    61,
      74,    75,    57,     0,     0,    83,    80,    81,     0,     0,
       0,     0,    39,    40,     0,     0,     0,   126,     0,     0,
       0,     0,   111,     0,   126,   115,   126,     0,    90,     0,
      50,     0,     0,    84,    85,    82,    58,    58,     0,    58,
     126,   117,   116,     0,     0,   121,   124,     0,   133,   105,
       0,     0,     0,     0,   133,   123,   133,    98,    47,    52,
       0,   115,   115,     0,   115,   133,     0,     0,     0,     0,
       0,     0,    31,     0,     0,   107,     0,    32,   126,    33,
      91,    24,   123,   123,    58,   123,    30,   118,   119,   120,
     122,   132,   125,   127,   134,   108,   109,   110,   133,   133,
     133,   115,   133,   131,   130,   129,     0,    34,    35,    36,
     123,    37,   128,   133,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -193,  -193,  -193,  -193,  -193,  -193,  -193,  -193,     2,   -47,
    -193,  -193,   159,   209,  -193,  -192,  -193,  -106,   200,   -82,
    -180,    -9,   244,   -44,   -12,  -193,   246,  -193,   150,   -69,
     -72,  -193,  -124,   -59,  -104,    37,    13,  -134,  -193,    88,
    -193,  -173,  -119,  -193,    54,  -193,  -125,  -193,     0,   -28
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,    25,   126,
     127,   110,   113,   111,   153,   161,   117,   162,   119,    85,
     120,   121,    48,   172,   212,    87,    88,   216,   217,    49,
      50,    51,   139,    92,    93,    94,   187,   184,   252,   255,
     256,   227,   258,   302,   303,   315,   282,    39,    52,    53
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      47,   103,   100,    70,    33,   123,   194,    36,    82,   197,
     129,    98,    56,   101,    72,   241,   140,   142,   174,   143,
     219,   234,   200,   201,   236,    84,   185,   221,    60,    61,
      62,    63,   192,   273,    64,    65,    26,    32,    89,    32,
      32,   125,   105,   104,    91,    47,    97,   108,   259,   144,
     270,   112,   114,   114,   105,   114,   178,    77,    27,    78,
      71,    34,    99,   106,    83,   107,   210,    28,    95,   215,
      95,    73,    95,    30,    73,   238,   -97,   220,   122,   180,
    -137,    32,   285,   196,    73,   231,   232,    29,    97,   137,
      73,    95,   288,    31,    89,   145,   175,    95,    35,   181,
     240,   265,    57,   305,   306,   138,   307,   243,   244,    95,
     215,    54,   313,    55,   235,   264,    47,   266,   314,   309,
     310,   112,   312,    37,    38,   260,   261,   204,   128,   263,
      95,   275,   222,   223,   128,   198,    95,   292,   293,   287,
     295,   289,    95,   290,   148,   188,   149,   323,   191,    58,
     296,   248,   130,    59,   254,   157,   158,   159,   160,   246,
     247,    66,   249,   211,   271,   272,   154,   274,   155,   308,
      69,   210,   210,   130,   131,   132,   133,   320,   134,   135,
     150,   151,   152,   317,   318,   319,   156,   321,   155,    95,
      95,   164,   182,   155,   136,   131,   132,   133,   324,   134,
     135,    40,    41,    42,    43,    44,   205,   254,   206,   277,
     278,    99,   311,    67,   294,   136,   251,   253,    95,    95,
      68,    95,    45,   269,   250,   206,   291,    74,   206,    95,
      95,    46,     1,    95,     2,    75,     3,     4,     5,    54,
     115,     6,   118,   157,   158,   159,   160,     7,     8,     9,
      10,    40,    41,    42,    43,    44,   213,   214,    79,    11,
      12,    13,    14,    15,    16,   298,   299,   297,   211,   211,
     253,   301,    45,    95,    40,    41,    42,    43,    44,    84,
      80,    76,    40,    41,    42,    43,    44,    81,    17,    40,
      41,    42,    43,    44,    86,    45,   166,   167,   168,    96,
      90,   109,   105,    45,   176,   116,   169,   301,   141,   146,
     102,   124,   170,   171,    86,   157,   158,   159,   160,   105,
     157,   158,   159,   160,  -137,   147,   163,   165,   179,   183,
     186,   189,   190,   193,   195,   199,   203,   207,   208,   218,
     125,   224,   226,   228,   225,   229,   230,   233,   239,   237,
     242,   257,   262,   267,   280,   268,   279,   281,   202,   276,
     283,   284,   286,   304,   316,   209,   245,   300,   173,   177,
     322
};

static const yytype_int16 yycheck[] =
{
       9,    73,    71,    13,     4,    87,   140,     7,    11,   143,
      92,    70,    10,    72,    13,   207,    98,    13,   124,   101,
      13,   194,   146,   147,   197,    17,   130,    13,    28,    29,
      30,    31,   136,    13,    34,    35,     4,    60,    66,    60,
      60,    20,    60,    49,    67,    54,    67,    75,   228,    67,
     242,    79,    80,    81,    60,    83,   125,    55,    26,    59,
      70,    10,    71,    69,    67,    74,   172,     6,    68,   175,
      70,    70,    72,     6,    70,   199,    67,    70,    70,    48,
      71,    60,   262,   142,    70,   189,   190,    26,    67,    44,
      70,    91,   265,    26,   122,   104,   124,    97,    13,    68,
     206,   235,     0,   283,   284,    60,   286,   213,   214,   109,
     216,    20,     8,    22,   196,   234,   125,   236,    14,   292,
     293,   149,   295,    37,    38,   229,   230,   155,    91,   233,
     130,   250,   179,   180,    97,   144,   136,   271,   272,   264,
     274,   266,   142,   267,    68,   132,    70,   320,   135,    65,
     275,   220,    28,    13,   226,    61,    62,    63,    64,   218,
     219,    19,   221,   172,   246,   247,    68,   249,    70,   288,
      44,   277,   278,    28,    50,    51,    52,   311,    54,    55,
      23,    24,    25,   308,   309,   310,    68,   312,    70,   189,
     190,    68,    68,    70,    70,    50,    51,    52,   323,    54,
      55,    39,    40,    41,    42,    43,    68,   279,    70,   253,
     254,   220,   294,    66,   273,    70,   225,   226,   218,   219,
      13,   221,    60,    68,   224,    70,    68,    67,    70,   229,
     230,    69,     3,   233,     5,    71,     7,     8,     9,    20,
      81,    12,    83,    61,    62,    63,    64,    18,    19,    20,
      21,    39,    40,    41,    42,    43,    74,    75,    67,    30,
      31,    32,    33,    34,    35,   277,   278,   276,   277,   278,
     279,   280,    60,   273,    39,    40,    41,    42,    43,    17,
      67,    69,    39,    40,    41,    42,    43,    67,    59,    39,
      40,    41,    42,    43,    60,    60,    56,    57,    58,    60,
      64,    13,    60,    60,    69,    67,    66,   316,    44,    68,
      60,    66,    72,    73,    60,    61,    62,    63,    64,    60,
      61,    62,    63,    64,    71,    68,    70,    27,    48,    45,
      53,    28,    28,    60,    60,    68,    67,    67,    11,    13,
      20,    44,    46,    29,    16,    28,    28,    28,    62,    68,
      67,    15,    29,    68,    16,    68,    27,    47,   149,    70,
      29,    29,    29,    62,    70,   165,   216,   279,   122,   125,
     316
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    30,    31,    32,    33,    34,    35,    59,    77,    78,
      79,    80,    81,    82,    83,    84,     4,    26,     6,    26,
       6,    26,    60,   124,    10,    13,   124,    37,    38,   123,
      39,    40,    41,    42,    43,    60,    69,    97,    98,   105,
     106,   107,   124,   125,    20,    22,    84,     0,    65,    13,
     124,   124,   124,   124,   124,   124,    19,    66,    13,    44,
      13,    70,    13,    70,    67,    71,    69,    84,   124,    67,
      67,    67,    11,    67,    17,    95,    60,   101,   102,   125,
      64,    67,   109,   110,   111,   124,    60,    67,   109,    97,
     105,   109,    60,   106,    49,    60,    69,    97,   125,    13,
      87,    89,   125,    88,   125,    88,    67,    92,    88,    94,
      96,    97,    70,    95,    66,    20,    85,    86,   111,    95,
      28,    50,    51,    52,    54,    55,    70,    44,    60,   108,
      95,    44,    13,    95,    67,    97,    68,    68,    68,    70,
      23,    24,    25,    90,    68,    70,    68,    61,    62,    63,
      64,    91,    93,    70,    68,    27,    56,    57,    58,    66,
      72,    73,    99,   102,    93,   125,    69,    98,   105,    48,
      48,    68,    68,    45,   113,   110,    53,   112,   112,    28,
      28,   112,   110,    60,   113,    60,   109,   113,    97,    68,
     108,   108,    89,    67,   125,    68,    70,    67,    11,    94,
      93,    97,   100,    74,    75,    93,   103,   104,    13,    13,
      70,    13,    85,    85,    44,    16,    46,   117,    29,    28,
      28,   110,   110,    28,   117,    95,   117,    68,   108,    62,
      93,    91,    67,    93,    93,   104,   109,   109,   105,   109,
     124,    97,   114,    97,   106,   115,   116,    15,   118,    96,
     110,   110,    29,   110,   118,   113,   118,    68,    68,    68,
      91,    95,    95,    13,    95,   118,    70,    99,    99,    27,
      16,    47,   122,    29,    29,    96,    29,   122,   117,   122,
     108,    68,   113,   113,   109,   113,   122,    97,   100,   100,
     115,    97,   119,   120,    62,    96,    96,    96,   118,   117,
     117,    95,   117,     8,    14,   121,    70,   122,   122,   122,
     113,   122,   120,   117,   122
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    76,    77,    77,    77,    77,    78,    78,    78,    78,
      78,    79,    79,    79,    79,    80,    80,    81,    82,    82,
      82,    82,    82,    83,    83,    83,    83,    83,    83,    83,
      83,    84,    84,    84,    84,    85,    85,    85,    85,    86,
      86,    87,    87,    88,    88,    89,    90,    90,    90,    91,
      91,    92,    92,    93,    93,    93,    93,    94,    95,    95,
      96,    96,    97,    97,    98,    98,    98,    98,    99,    99,
      99,    99,    99,    99,   100,   100,   101,   101,   102,   102,
     102,   103,   103,   104,   104,   104,   105,   105,   106,   106,
     106,   106,   107,   107,   107,   107,   107,   107,   108,   108,
     108,   109,   110,   110,   111,   111,   111,   111,   111,   111,
     111,   111,   111,   112,   112,   113,   113,   114,   114,   115,
     115,   116,   116,   117,   117,   118,   118,   119,   119,   120,
     121,   121,   121,   122,   122,   123,   123,   124,   125
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
       6,     8,     1,     1,     1,     1,     1,     1,     0,     2,
       1,     1,     2,     3,     1,     5,     3,     6,     7,     7,
       7,     4,     3,     0,     1,     0,     3,     1,     3,     3,
       3,     1,     3,     0,     2,     3,     0,     1,     3,     2,
       1,     1,     0,     0,     2,     1,     1,     1,     1
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
#line 89 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1840 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 94 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1849 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 99 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1858 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 104 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1867 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 120 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1875 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 124 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1883 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 128 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1891 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 132 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1899 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 139 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1907 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 143 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1915 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 150 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1923 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 157 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1931 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 161 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1939 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 165 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1947 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 169 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1955 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 173 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1963 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 180 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 1971 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 184 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 1979 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 188 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1987 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 192 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1995 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: select_stmt  */
#line 196 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select);
    }
#line 2003 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN select_stmt  */
#line 200 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), false);
    }
#line 2011 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE select_stmt  */
#line 204 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), true);
    }
#line 2019 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 30: /* dml: SELECT '*' FROM '(' union_query ')' AS tbName opt_order_clause opt_limit  */
#line 208 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_selects), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2027 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 31: /* select_stmt: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 215 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2035 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 32: /* select_stmt: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 219 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2043 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 33: /* select_stmt: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 223 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2051 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 34: /* select_stmt: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 227 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-9].sv_cols), (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_from), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2059 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 35: /* union_branch: SELECT '*' FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 234 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2067 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 36: /* union_branch: SELECT colList FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 238 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-6].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2075 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 37: /* union_branch: SELECT agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 242 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2083 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 38: /* union_branch: SELECT colList ',' agg_list FROM from_clause optWhereClause opt_group_by opt_having opt_limit  */
#line 246 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-8].sv_cols), (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_from), (yyvsp[-3].sv_conds), (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2091 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 39: /* union_query: union_branch UNION union_branch  */
#line 253 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_selects) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select), (yyvsp[0].sv_select)};
    }
#line 2099 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 40: /* union_query: union_query UNION union_branch  */
#line 257 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_selects) = (yyvsp[-2].sv_selects);
        (yyval.sv_selects).push_back((yyvsp[0].sv_select));
    }
#line 2108 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 41: /* fieldList: field  */
#line 265 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2116 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 42: /* fieldList: fieldList ',' field  */
#line 269 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2124 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 43: /* colNameList: colName  */
#line 276 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2132 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 44: /* colNameList: colNameList ',' colName  */
#line 280 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2140 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 45: /* field: colName type  */
#line 287 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2148 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 46: /* type: INT  */
#line 294 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2156 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 47: /* type: CHAR '(' VALUE_INT ')'  */
#line 298 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2164 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 48: /* type: FLOAT  */
#line 302 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2172 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 49: /* valueList: value  */
#line 309 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2180 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 50: /* valueList: valueList ',' value  */
#line 313 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2188 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 51: /* valueRows: '(' valueList ')'  */
#line 320 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2196 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 52: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 324 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2205 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 53: /* value: VALUE_INT  */
#line 332 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2213 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_FLOAT  */
#line 336 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2221 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 55: /* value: VALUE_STRING  */
#line 340 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2229 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_BOOL  */
#line 344 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2237 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 57: /* condition: col op expr  */
#line 351 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2245 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 58: /* optWhereClause: %empty  */
#line 357 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2251 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 59: /* optWhereClause: WHERE whereClause  */
#line 359 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2259 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 60: /* whereClause: condition  */
#line 366 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2267 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 61: /* whereClause: whereClause AND condition  */
#line 370 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2275 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 62: /* col: tbName '.' colName  */
#line 377 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2283 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 63: /* col: colName  */
#line 381 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2291 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 64: /* colList: col  */
#line 388 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2299 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 65: /* colList: colList ',' col  */
#line 392 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2307 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 66: /* colList: col AS IDENTIFIER  */
#line 396 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2317 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 67: /* colList: colList ',' col AS IDENTIFIER  */
#line 402 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2326 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 68: /* op: '='  */
#line 410 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2334 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 69: /* op: '<'  */
#line 414 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2342 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 70: /* op: '>'  */
#line 418 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2350 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 71: /* op: NEQ  */
#line 422 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2358 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 72: /* op: LEQ  */
#line 426 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2366 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 73: /* op: GEQ  */
#line 430 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2374 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 74: /* expr: value  */
#line 437 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2382 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 75: /* expr: col  */
#line 441 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2390 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 76: /* setClauses: setClause  */
#line 448 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2398 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 77: /* setClauses: setClauses ',' setClause  */
#line 452 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2406 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 78: /* setClause: colName '=' value  */
#line 459 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2414 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 79: /* setClause: colName '=' colName  */
#line 463 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2426 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 80: /* setClause: colName '=' colName arith_chain  */
#line 471 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 题9 单项算术 v = v ± 1 与 决赛链式算术 v = v ± v1 ± v2 ...（左结合）。
         * 项均已带符号；单项与原三条产生式行为等价，多项存 chain 由 analyze/执行器
         * 逐步左结合求值（float 非结合，不能折叠常量）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_vals)[0], false);
        if ((yyvsp[0].sv_vals).size() > 1) (yyval.sv_set_clause)->chain = (yyvsp[0].sv_vals);
    }
#line 2438 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 81: /* arith_chain: arith_term  */
#line 482 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2446 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 82: /* arith_chain: arith_chain arith_term  */
#line 486 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2454 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 83: /* arith_term: value  */
#line 493 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 词法已把无空格 +1/-1 归并为带符号字面量 */
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2463 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 84: /* arith_term: '+' value  */
#line 498 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = (yyvsp[0].sv_val);
    }
#line 2471 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 85: /* arith_term: '-' value  */
#line 502 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_val) = negate_value((yyvsp[0].sv_val));
        if ((yyval.sv_val) == nullptr) YYERROR;
    }
#line 2480 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 86: /* agg_list: agg_func  */
#line 512 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2488 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 87: /* agg_list: agg_list ',' agg_func  */
#line 516 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2496 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 88: /* agg_func: agg_name '(' '*' ')' opt_alias  */
#line 523 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), nullptr, (yyvsp[0].sv_str), true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2505 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 89: /* agg_func: agg_name '(' col ')' opt_alias  */
#line 528 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-4].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str));
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2514 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 90: /* agg_func: agg_name '(' DISTINCT col ')' opt_alias  */
#line 533 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-5].sv_str), (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2524 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 91: /* agg_func: agg_name '(' DISTINCT '(' col ')' ')' opt_alias  */
#line 539 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = make_aggregate_expr((yyvsp[-7].sv_str), (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
        if ((yyval.sv_agg_expr) == nullptr) YYERROR;
    }
#line 2534 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 92: /* agg_name: COUNT  */
#line 547 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "count"; }
#line 2540 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 93: /* agg_name: MAX  */
#line 548 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "max"; }
#line 2546 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 94: /* agg_name: MIN  */
#line 549 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "min"; }
#line 2552 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 95: /* agg_name: SUM  */
#line 550 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "sum"; }
#line 2558 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 96: /* agg_name: AVG  */
#line 551 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
              { (yyval.sv_str) = "avg"; }
#line 2564 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 97: /* agg_name: IDENTIFIER  */
#line 553 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        if (find_aggregate((yyvsp[0].sv_str)) == nullptr) YYERROR;
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2573 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 98: /* opt_alias: %empty  */
#line 561 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2581 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 99: /* opt_alias: AS IDENTIFIER  */
#line 565 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2589 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 100: /* opt_alias: IDENTIFIER  */
#line 569 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2597 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 101: /* from_clause: joined_table  */
#line 576 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2605 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 102: /* table_ref: tbName opt_alias  */
#line 584 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<TableRef>((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2613 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 103: /* table_ref: '(' joined_table ')'  */
#line 588 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[-1].sv_from);
    }
#line 2621 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 104: /* joined_table: table_ref  */
#line 596 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = (yyvsp[0].sv_from);
    }
#line 2629 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 105: /* joined_table: joined_table JOIN table_ref ON whereClause  */
#line 600 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-4].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds));
    }
#line 2638 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 106: /* joined_table: joined_table JOIN table_ref  */
#line 605 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{});
    }
#line 2647 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 107: /* joined_table: joined_table INNER JOIN table_ref ON whereClause  */
#line 610 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            INNER_JOIN, (yyvsp[-5].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds));
    }
#line 2656 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 108: /* joined_table: joined_table LEFT opt_outer JOIN table_ref ON whereClause  */
#line 615 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            LEFT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds));
    }
#line 2665 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 109: /* joined_table: joined_table RIGHT opt_outer JOIN table_ref ON whereClause  */
#line 620 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            RIGHT_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds));
    }
#line 2674 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 110: /* joined_table: joined_table FULL opt_outer JOIN table_ref ON whereClause  */
#line 625 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            FULL_JOIN, (yyvsp[-6].sv_from), (yyvsp[-2].sv_from), (yyvsp[0].sv_conds));
    }
#line 2683 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 111: /* joined_table: joined_table CROSS JOIN table_ref  */
#line 630 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-3].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{});
    }
#line 2692 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 112: /* joined_table: joined_table ',' table_ref  */
#line 635 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_from) = std::make_shared<JoinExpr>(
            CROSS_JOIN, (yyvsp[-2].sv_from), (yyvsp[0].sv_from), std::vector<std::shared_ptr<BinaryExpr>>{});
    }
#line 2701 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 115: /* opt_group_by: %empty  */
#line 649 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 2709 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 116: /* opt_group_by: GROUP BY group_by_list  */
#line 653 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 2717 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 117: /* group_by_list: col  */
#line 660 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2725 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 118: /* group_by_list: group_by_list ',' col  */
#line 664 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2733 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 119: /* having_condition: col op expr  */
#line 671 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2741 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 120: /* having_condition: agg_func op expr  */
#line 675 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_agg_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2749 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 121: /* having_clause: having_condition  */
#line 682 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2757 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 122: /* having_clause: having_clause AND having_condition  */
#line 686 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2765 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 123: /* opt_having: %empty  */
#line 693 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = {};
    }
#line 2773 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 124: /* opt_having: HAVING having_clause  */
#line 697 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2781 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 125: /* opt_order_clause: ORDER BY order_list  */
#line 704 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 2789 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 126: /* opt_order_clause: %empty  */
#line 708 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 2797 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 127: /* order_list: order_clause  */
#line 715 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 2805 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 128: /* order_list: order_list ',' order_clause  */
#line 719 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 2813 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 129: /* order_clause: col opt_asc_desc  */
#line 726 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 2821 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 130: /* opt_asc_desc: ASC  */
#line 732 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2827 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 131: /* opt_asc_desc: DESC  */
#line 733 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2833 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 132: /* opt_asc_desc: %empty  */
#line 734 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2839 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 133: /* opt_limit: %empty  */
#line 739 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 2847 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 134: /* opt_limit: LIMIT VALUE_INT  */
#line 743 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 2855 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 135: /* set_knob_type: ENABLE_NESTLOOP  */
#line 749 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2861 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;

  case 136: /* set_knob_type: ENABLE_SORTMERGE  */
#line 750 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2867 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"
    break;


#line 2871 "/root/csc-db-learn/csc-db/src/parser/yacc.tab.cpp"

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

#line 756 "/root/csc-db-learn/csc-db/src/parser/yacc.y"
