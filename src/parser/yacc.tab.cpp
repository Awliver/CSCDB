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
#line 1 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

#line 86 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"

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
  YYSYMBOL_LEQ = 50,                       /* LEQ  */
  YYSYMBOL_NEQ = 51,                       /* NEQ  */
  YYSYMBOL_GEQ = 52,                       /* GEQ  */
  YYSYMBOL_T_EOF = 53,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 54,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 55,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 56,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 57,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 58,                /* VALUE_BOOL  */
  YYSYMBOL_59_ = 59,                       /* ';'  */
  YYSYMBOL_60_ = 60,                       /* '='  */
  YYSYMBOL_61_ = 61,                       /* '('  */
  YYSYMBOL_62_ = 62,                       /* ')'  */
  YYSYMBOL_63_ = 63,                       /* '*'  */
  YYSYMBOL_64_ = 64,                       /* ','  */
  YYSYMBOL_65_ = 65,                       /* '.'  */
  YYSYMBOL_66_ = 66,                       /* '<'  */
  YYSYMBOL_67_ = 67,                       /* '>'  */
  YYSYMBOL_68_ = 68,                       /* '+'  */
  YYSYMBOL_69_ = 69,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 70,                  /* $accept  */
  YYSYMBOL_start = 71,                     /* start  */
  YYSYMBOL_stmt = 72,                      /* stmt  */
  YYSYMBOL_txnStmt = 73,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 74,                    /* dbStmt  */
  YYSYMBOL_setStmt = 75,                   /* setStmt  */
  YYSYMBOL_ddl = 76,                       /* ddl  */
  YYSYMBOL_dml = 77,                       /* dml  */
  YYSYMBOL_select_stmt = 78,               /* select_stmt  */
  YYSYMBOL_union_branch = 79,              /* union_branch  */
  YYSYMBOL_union_query = 80,               /* union_query  */
  YYSYMBOL_fieldList = 81,                 /* fieldList  */
  YYSYMBOL_colNameList = 82,               /* colNameList  */
  YYSYMBOL_field = 83,                     /* field  */
  YYSYMBOL_type = 84,                      /* type  */
  YYSYMBOL_valueList = 85,                 /* valueList  */
  YYSYMBOL_valueRows = 86,                 /* valueRows  */
  YYSYMBOL_value = 87,                     /* value  */
  YYSYMBOL_condition = 88,                 /* condition  */
  YYSYMBOL_optWhereClause = 89,            /* optWhereClause  */
  YYSYMBOL_whereClause = 90,               /* whereClause  */
  YYSYMBOL_col = 91,                       /* col  */
  YYSYMBOL_colList = 92,                   /* colList  */
  YYSYMBOL_op = 93,                        /* op  */
  YYSYMBOL_expr = 94,                      /* expr  */
  YYSYMBOL_setClauses = 95,                /* setClauses  */
  YYSYMBOL_setClause = 96,                 /* setClause  */
  YYSYMBOL_agg_list = 97,                  /* agg_list  */
  YYSYMBOL_agg_func = 98,                  /* agg_func  */
  YYSYMBOL_opt_alias = 99,                 /* opt_alias  */
  YYSYMBOL_tableList = 100,                /* tableList  */
  YYSYMBOL_opt_group_by = 101,             /* opt_group_by  */
  YYSYMBOL_group_by_list = 102,            /* group_by_list  */
  YYSYMBOL_having_condition = 103,         /* having_condition  */
  YYSYMBOL_having_clause = 104,            /* having_clause  */
  YYSYMBOL_opt_having = 105,               /* opt_having  */
  YYSYMBOL_opt_order_clause = 106,         /* opt_order_clause  */
  YYSYMBOL_order_list = 107,               /* order_list  */
  YYSYMBOL_order_clause = 108,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 109,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit = 110,                /* opt_limit  */
  YYSYMBOL_set_knob_type = 111,            /* set_knob_type  */
  YYSYMBOL_tbName = 112,                   /* tbName  */
  YYSYMBOL_colName = 113                   /* colName  */
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
#define YYLAST   332

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  44
/* YYNRULES -- Number of rules.  */
#define YYNRULES  123
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  302

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   313


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
      61,    62,    63,    68,    64,    69,    65,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    59,
      66,    60,    67,     2,     2,     2,     2,     2,     2,     2,
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
      55,    56,    57,    58
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    70,    70,    75,    80,    85,    93,    94,    95,    96,
      97,   101,   105,   109,   113,   120,   124,   131,   138,   142,
     146,   150,   154,   161,   165,   169,   173,   177,   181,   185,
     189,   196,   202,   208,   214,   223,   229,   235,   241,   250,
     254,   262,   266,   273,   277,   284,   291,   295,   299,   306,
     310,   317,   321,   329,   333,   337,   341,   348,   355,   356,
     363,   367,   374,   378,   385,   389,   393,   399,   407,   411,
     415,   419,   423,   427,   434,   438,   445,   449,   456,   460,
     468,   473,   478,   488,   492,   499,   503,   507,   512,   517,
     521,   525,   529,   537,   540,   544,   565,   570,   575,   580,
     590,   593,   600,   604,   611,   615,   623,   627,   635,   638,
     645,   650,   656,   660,   667,   674,   675,   676,   681,   684,
     691,   692,   695,   697
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
  "UNION", "DISTINCT", "LEQ", "NEQ", "GEQ", "T_EOF", "IDENTIFIER",
  "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='",
  "'('", "')'", "'*'", "','", "'.'", "'<'", "'>'", "'+'", "'-'", "$accept",
  "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl", "dml",
  "select_stmt", "union_branch", "union_query", "fieldList", "colNameList",
  "field", "type", "valueList", "valueRows", "value", "condition",
  "optWhereClause", "whereClause", "col", "colList", "op", "expr",
  "setClauses", "setClause", "agg_list", "agg_func", "opt_alias",
  "tableList", "opt_group_by", "group_by_list", "having_condition",
  "having_clause", "opt_having", "opt_order_clause", "order_list",
  "order_clause", "opt_asc_desc", "opt_limit", "set_knob_type", "tbName",
  "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-185)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-123)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     212,    15,    32,    33,   -17,    35,    61,   -17,    94,    75,
     134,  -185,  -185,  -185,  -185,  -185,  -185,  -185,   113,    60,
    -185,  -185,  -185,  -185,  -185,  -185,  -185,   112,   -17,   -17,
     -17,   -17,  -185,  -185,   -17,   -17,   138,  -185,  -185,    89,
     110,   111,   114,   141,   147,   101,   196,   170,     2,     9,
    -185,   153,  -185,   186,   202,  -185,  -185,  -185,   -17,   162,
     177,  -185,   180,    -5,   234,   198,   195,    38,   213,   213,
     213,   213,    49,   214,   -17,   221,   -17,   158,   198,   253,
    -185,  -185,   198,   198,   198,   208,   198,   213,  -185,  -185,
      -1,  -185,   211,  -185,    51,   210,   215,   227,   228,   229,
     238,   254,    27,  -185,  -185,    27,   232,    19,    27,  -185,
    -185,   -17,   105,  -185,   154,   121,  -185,   125,   104,   209,
     129,  -185,   251,    28,   198,  -185,   230,   213,   239,    54,
      54,    54,    54,    54,    54,   216,   244,    13,   -17,   -17,
     257,   257,   249,   -17,   257,  -185,   198,  -185,   243,  -185,
    -185,  -185,   198,  -185,  -185,  -185,  -185,  -185,   130,  -185,
     245,   294,   213,  -185,  -185,  -185,  -185,  -185,  -185,   241,
    -185,  -185,   225,   246,    54,   255,  -185,  -185,  -185,  -185,
    -185,  -185,  -185,   297,    20,    21,   254,   254,   263,   282,
    -185,   296,   267,   267,  -185,    27,   267,  -185,   258,  -185,
    -185,   104,   104,   256,  -185,  -185,  -185,  -185,   104,   104,
    -185,   259,  -185,  -185,   -17,   -17,   221,   -17,  -185,  -185,
     -17,   213,   213,   221,   300,   300,   257,   300,   260,  -185,
     172,   104,  -185,  -185,    54,    27,    27,    22,    27,   300,
     251,  -185,   252,    28,    28,  -185,   291,   303,   273,   273,
     267,   273,  -185,  -185,   175,  -185,   257,   257,   -17,   257,
     273,   213,   241,   241,   221,   213,   268,  -185,  -185,   300,
    -185,  -185,   267,   267,    27,   267,  -185,  -185,  -185,  -185,
    -185,    34,   261,  -185,  -185,   273,   273,   273,   257,   273,
    -185,  -185,  -185,   213,  -185,  -185,  -185,   267,  -185,  -185,
     273,  -185
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       9,     6,    10,     7,     8,    27,    15,     0,     0,     0,
       0,     0,   122,    20,     0,     0,     0,   120,   121,     0,
       0,     0,     0,     0,     0,   123,     0,    64,     0,     0,
      83,     0,    63,     0,     0,    28,     1,     2,     0,     0,
       0,    19,     0,     0,    58,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      29,    16,     0,     0,     0,     0,     0,     0,    25,   123,
      58,    76,     0,    17,     0,     0,     0,     0,     0,     0,
       0,     0,    58,    96,    66,    58,    65,     0,    58,    84,
      62,     0,     0,    41,     0,     0,    43,     0,     0,    23,
       0,    60,    59,     0,     0,    26,     0,     0,     0,    93,
      93,    93,    93,    93,    93,     0,     0,     0,     0,     0,
     100,   100,     0,     0,   100,    18,     0,    46,     0,    48,
      45,    21,     0,    22,    55,    53,    54,    56,     0,    49,
       0,     0,     0,    72,    71,    73,    68,    69,    70,     0,
      77,    78,    79,     0,    93,     0,    95,    85,    86,    89,
      90,    91,    92,     0,     0,     0,     0,     0,     0,    98,
      97,     0,   108,   108,    67,    58,   108,    42,     0,    44,
      51,     0,     0,     0,    61,    74,    75,    57,     0,     0,
      80,     0,    87,    94,     0,     0,     0,     0,    39,    40,
       0,     0,     0,     0,   111,   111,   100,   111,     0,    50,
       0,     0,    81,    82,    93,    58,    58,     0,    58,   111,
      99,   102,   101,     0,     0,   106,   109,     0,   118,   118,
     108,   118,    47,    52,     0,    88,   100,   100,     0,   100,
     118,     0,     0,     0,     0,     0,     0,    31,    32,   111,
      33,    24,   108,   108,    58,   108,    30,   103,   104,   105,
     107,   117,   110,   112,   119,   118,   118,   118,   100,   118,
     116,   115,   114,     0,    34,    35,    36,   108,    37,   113,
     118,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -185,  -185,  -185,  -185,  -185,  -185,  -185,  -185,    11,   -50,
    -185,  -185,   164,   181,  -185,  -184,  -185,  -112,   161,   -62,
     107,   -58,   191,  -162,  -122,  -185,   205,   -68,   -76,   -81,
     -72,  -136,  -185,    66,  -185,  -173,  -163,  -185,    39,  -185,
    -105,  -185,    -4,    44
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,    25,   136,
     137,   112,   115,   113,   150,   158,   119,   159,   121,    88,
     122,    47,    48,   169,   207,    90,    91,    49,    50,   177,
     102,   192,   242,   245,   246,   224,   248,   282,   283,   292,
     267,    39,    51,    52
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      33,   109,   105,    36,   108,   193,    85,   107,   196,    96,
      97,    98,    99,   100,   171,    74,    87,   106,   230,    26,
     225,    55,    76,   227,    59,    60,    61,    62,   125,   123,
      63,    64,   143,   215,   217,   258,   128,    32,    28,    30,
     140,    27,   290,   141,    87,    34,   144,   254,   291,   178,
     179,   180,   181,   182,    81,   138,    86,   205,    29,    31,
     210,   187,   249,   124,   251,    80,    75,   185,   103,   173,
     103,   195,   103,    77,    35,   188,   260,   269,   163,   164,
     165,   262,   263,    77,   216,    77,    77,    94,   166,   229,
     250,   139,    45,   212,   167,   168,   232,   233,   175,   286,
     287,    95,   289,    32,   123,    45,   285,   103,   176,    92,
     101,   206,   127,    56,    40,    41,    42,    43,    44,    57,
     272,   273,   110,   275,   300,    58,   114,   116,   116,    45,
     116,    37,    38,   226,   189,   190,   218,   219,    46,   103,
     278,   279,   235,   236,   268,   238,   270,   244,   237,    66,
     205,   205,   297,   255,    53,   276,    54,    65,   106,   154,
     155,   156,   157,   123,   241,   243,  -122,   145,    92,   146,
     172,    67,    68,   256,   257,    69,   259,   147,   148,   149,
     294,   295,   296,   151,   298,   152,   274,   153,   244,   152,
     114,   161,   200,   152,   201,   301,   199,    40,    41,    42,
      43,    44,    70,   277,   206,   206,   243,   281,    71,    72,
     103,   103,   288,   103,    73,     1,   239,     2,    78,     3,
       4,     5,    53,    82,     6,    40,    41,    42,    43,    44,
       7,     8,     9,    10,   253,   281,   201,   271,    83,   201,
      45,    84,    11,    12,    13,    14,    15,    16,   117,    79,
     120,    87,    89,    93,   103,    40,    41,    42,    43,    44,
      40,    41,    42,    43,    44,    17,   111,    45,   104,   118,
      45,   126,   129,   160,   135,    45,   142,   130,   162,   183,
     154,   155,   156,   157,    89,   154,   155,   156,   157,   131,
     132,   133,   186,   208,   209,    45,   154,   155,   156,   157,
     134,   174,   191,   194,   198,   203,   202,   220,   211,   213,
     214,   221,   222,   223,   228,   247,   261,   231,   264,   265,
     266,   234,   252,   204,   284,   293,   184,   197,   240,   170,
     280,     0,   299
};

static const yytype_int16 yycheck[] =
{
       4,    77,    74,     7,    76,   141,    11,    75,   144,    67,
      68,    69,    70,    71,   126,    13,    17,    75,   202,     4,
     193,    10,    13,   196,    28,    29,    30,    31,    90,    87,
      34,    35,    13,    13,    13,    13,    94,    54,     6,     6,
     102,    26,     8,   105,    17,    10,   108,   231,    14,   130,
     131,   132,   133,   134,    58,    28,    61,   169,    26,    26,
     172,    48,   225,    64,   227,    54,    64,   135,    72,   127,
      74,   143,    76,    64,    13,    62,   239,   250,    50,    51,
      52,   243,   244,    64,    64,    64,    64,    49,    60,   201,
     226,    64,    54,   174,    66,    67,   208,   209,    44,   272,
     273,    63,   275,    54,   162,    54,   269,   111,    54,    65,
      61,   169,    61,     0,    39,    40,    41,    42,    43,    59,
     256,   257,    78,   259,   297,    13,    82,    83,    84,    54,
      86,    37,    38,   195,   138,   139,   186,   187,    63,   143,
     262,   263,   214,   215,   249,   217,   251,   223,   216,    60,
     262,   263,   288,   234,    20,   260,    22,    19,   216,    55,
      56,    57,    58,   221,   222,   223,    65,    62,   124,    64,
     126,    61,    61,   235,   236,    61,   238,    23,    24,    25,
     285,   286,   287,    62,   289,    64,   258,    62,   264,    64,
     146,    62,    62,    64,    64,   300,   152,    39,    40,    41,
      42,    43,    61,   261,   262,   263,   264,   265,    61,    13,
     214,   215,   274,   217,    44,     3,   220,     5,    65,     7,
       8,     9,    20,    61,    12,    39,    40,    41,    42,    43,
      18,    19,    20,    21,    62,   293,    64,    62,    61,    64,
      54,    61,    30,    31,    32,    33,    34,    35,    84,    63,
      86,    17,    54,    58,   258,    39,    40,    41,    42,    43,
      39,    40,    41,    42,    43,    53,    13,    54,    54,    61,
      54,    60,    62,    64,    20,    54,    44,    62,    27,    63,
      55,    56,    57,    58,    54,    55,    56,    57,    58,    62,
      62,    62,    48,    68,    69,    54,    55,    56,    57,    58,
      62,    62,    45,    54,    61,    11,    61,    44,    62,    54,
      13,    29,    16,    46,    56,    15,    64,    61,    27,    16,
      47,    62,    62,   162,    56,    64,   135,   146,   221,   124,
     264,    -1,   293
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    30,    31,    32,    33,    34,    35,    53,    71,    72,
      73,    74,    75,    76,    77,    78,     4,    26,     6,    26,
       6,    26,    54,   112,    10,    13,   112,    37,    38,   111,
      39,    40,    41,    42,    43,    54,    63,    91,    92,    97,
      98,   112,   113,    20,    22,    78,     0,    59,    13,   112,
     112,   112,   112,   112,   112,    19,    60,    61,    61,    61,
      61,    61,    13,    44,    13,    64,    13,    64,    65,    63,
      78,   112,    61,    61,    61,    11,    61,    17,    89,    54,
      95,    96,   113,    58,    49,    63,    91,    91,    91,    91,
      91,    61,   100,   112,    54,   100,    91,    97,   100,    98,
     113,    13,    81,    83,   113,    82,   113,    82,    61,    86,
      82,    88,    90,    91,    64,    89,    60,    61,    91,    62,
      62,    62,    62,    62,    62,    20,    79,    80,    28,    64,
      89,    89,    44,    13,    89,    62,    64,    23,    24,    25,
      84,    62,    64,    62,    55,    56,    57,    58,    85,    87,
      64,    62,    27,    50,    51,    52,    60,    66,    67,    93,
      96,    87,   113,    91,    62,    44,    54,    99,    99,    99,
      99,    99,    99,    63,    92,    97,    48,    48,    62,   112,
     112,    45,   101,   101,    54,   100,   101,    83,    61,   113,
      62,    64,    61,    11,    88,    87,    91,    94,    68,    69,
      87,    62,    99,    54,    13,    13,    64,    13,    79,    79,
      44,    29,    16,    46,   105,   105,    89,   105,    56,    87,
      85,    61,    87,    87,    62,   100,   100,    97,   100,   112,
      90,    91,   102,    91,    98,   103,   104,    15,   106,   106,
     101,   106,    62,    62,    85,    99,    89,    89,    13,    89,
     106,    64,    93,    93,    27,    16,    47,   110,   110,   105,
     110,    62,   101,   101,   100,   101,   110,    91,    94,    94,
     103,    91,   107,   108,    56,   106,   105,   105,    89,   105,
       8,    14,   109,    64,   110,   110,   110,   101,   110,   108,
     105,   110
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    70,    71,    71,    71,    71,    72,    72,    72,    72,
      72,    73,    73,    73,    73,    74,    74,    75,    76,    76,
      76,    76,    76,    77,    77,    77,    77,    77,    77,    77,
      77,    78,    78,    78,    78,    79,    79,    79,    79,    80,
      80,    81,    81,    82,    82,    83,    84,    84,    84,    85,
      85,    86,    86,    87,    87,    87,    87,    88,    89,    89,
      90,    90,    91,    91,    92,    92,    92,    92,    93,    93,
      93,    93,    93,    93,    94,    94,    95,    95,    96,    96,
      96,    96,    96,    97,    97,    98,    98,    98,    98,    98,
      98,    98,    98,    99,    99,    99,   100,   100,   100,   100,
     101,   101,   102,   102,   103,   103,   104,   104,   105,   105,
     106,   106,   107,   107,   108,   109,   109,   109,   110,   110,
     111,   111,   112,   113
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
       4,     5,     5,     1,     3,     5,     5,     6,     8,     5,
       5,     5,     5,     0,     2,     1,     1,     3,     3,     5,
       0,     3,     1,     3,     3,     3,     1,     3,     0,     2,
       3,     0,     1,     3,     2,     1,     1,     0,     0,     2,
       1,     1,     1,     1
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
#line 71 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1793 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 76 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1802 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 81 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1811 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 86 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1820 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 102 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1828 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 106 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1836 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 110 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1844 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 114 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1852 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 121 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1860 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 125 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1868 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 132 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1876 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 139 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1884 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 143 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1892 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 147 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1900 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 151 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1908 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 155 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1916 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES valueRows  */
#line 162 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 1924 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName '(' colNameList ')' VALUES '(' valueList ')'  */
#line 166 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 1932 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 170 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1940 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 174 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1948 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: select_stmt  */
#line 178 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select);
    }
#line 1956 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN select_stmt  */
#line 182 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), false);
    }
#line 1964 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: EXPLAIN ANALYZE select_stmt  */
#line 186 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), true);
    }
#line 1972 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 30: /* dml: SELECT '*' FROM '(' union_query ')' AS tbName opt_order_clause opt_limit  */
#line 190 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_selects), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1980 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 31: /* select_stmt: SELECT '*' FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 197 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1990 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 32: /* select_stmt: SELECT colList FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 203 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2000 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 33: /* select_stmt: SELECT agg_list FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 209 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2010 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 34: /* select_stmt: SELECT colList ',' agg_list FROM tableList optWhereClause opt_group_by opt_having opt_order_clause opt_limit  */
#line 215 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-9].sv_cols), (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2020 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 35: /* union_branch: SELECT '*' FROM tableList optWhereClause opt_group_by opt_having opt_limit  */
#line 224 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2030 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 36: /* union_branch: SELECT colList FROM tableList optWhereClause opt_group_by opt_having opt_limit  */
#line 230 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-6].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2040 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 37: /* union_branch: SELECT agg_list FROM tableList optWhereClause opt_group_by opt_having opt_limit  */
#line 236 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2050 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 38: /* union_branch: SELECT colList ',' agg_list FROM tableList optWhereClause opt_group_by opt_having opt_limit  */
#line 242 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-8].sv_cols), (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 2060 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 39: /* union_query: union_branch UNION union_branch  */
#line 251 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_selects) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select), (yyvsp[0].sv_select)};
    }
#line 2068 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 40: /* union_query: union_query UNION union_branch  */
#line 255 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_selects) = (yyvsp[-2].sv_selects);
        (yyval.sv_selects).push_back((yyvsp[0].sv_select));
    }
#line 2077 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 41: /* fieldList: field  */
#line 263 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2085 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 42: /* fieldList: fieldList ',' field  */
#line 267 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2093 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 43: /* colNameList: colName  */
#line 274 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2101 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 44: /* colNameList: colNameList ',' colName  */
#line 278 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2109 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 45: /* field: colName type  */
#line 285 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2117 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 46: /* type: INT  */
#line 292 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2125 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 47: /* type: CHAR '(' VALUE_INT ')'  */
#line 296 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2133 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 48: /* type: FLOAT  */
#line 300 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2141 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 49: /* valueList: value  */
#line 307 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2149 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 50: /* valueList: valueList ',' value  */
#line 311 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2157 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 51: /* valueRows: '(' valueList ')'  */
#line 318 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2165 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 52: /* valueRows: valueRows ',' '(' valueList ')'  */
#line 322 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2174 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 53: /* value: VALUE_INT  */
#line 330 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2182 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_FLOAT  */
#line 334 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2190 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 55: /* value: VALUE_STRING  */
#line 338 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2198 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 56: /* value: VALUE_BOOL  */
#line 342 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2206 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 57: /* condition: col op expr  */
#line 349 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2214 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 58: /* optWhereClause: %empty  */
#line 355 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2220 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 59: /* optWhereClause: WHERE whereClause  */
#line 357 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2228 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 60: /* whereClause: condition  */
#line 364 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2236 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 61: /* whereClause: whereClause AND condition  */
#line 368 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2244 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 62: /* col: tbName '.' colName  */
#line 375 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2252 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 63: /* col: colName  */
#line 379 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2260 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 64: /* colList: col  */
#line 386 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2268 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 65: /* colList: colList ',' col  */
#line 390 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2276 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 66: /* colList: col AS IDENTIFIER  */
#line 394 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 决赛：SELECT 列别名 col AS alias（输出列名须改为别名） */
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[-2].sv_col)};
    }
#line 2286 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 67: /* colList: colList ',' col AS IDENTIFIER  */
#line 400 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_col)->alias = (yyvsp[0].sv_str);
        (yyval.sv_cols).push_back((yyvsp[-2].sv_col));
    }
#line 2295 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 68: /* op: '='  */
#line 408 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2303 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 69: /* op: '<'  */
#line 412 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2311 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 70: /* op: '>'  */
#line 416 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2319 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 71: /* op: NEQ  */
#line 420 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2327 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 72: /* op: LEQ  */
#line 424 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2335 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 73: /* op: GEQ  */
#line 428 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2343 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 74: /* expr: value  */
#line 435 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2351 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 75: /* expr: col  */
#line 439 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2359 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 76: /* setClauses: setClause  */
#line 446 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2367 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 77: /* setClauses: setClauses ',' setClause  */
#line 450 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2375 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 78: /* setClause: colName '=' value  */
#line 457 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2383 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 79: /* setClause: colName '=' colName  */
#line 461 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 决赛：SET col = col（自赋值，须保留写冲突/回滚语义）与 col = 其他列。
         * 复用算术增量表示（delta 0）；同名列打 self_copy 标记，char 列由
         * analyze/executor 按恒等拷贝处理（不走数值 delta 路径）。 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str), std::make_shared<IntLit>(0), false);
        (yyval.sv_set_clause)->self_copy = ((yyvsp[-2].sv_str) == (yyvsp[0].sv_str));
    }
#line 2395 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 80: /* setClause: colName '=' colName value  */
#line 469 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 题9：算术增量 v=v+1（词法把 +1/-1 归并为带符号 VALUE_INT，无空格情形） */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_val), false);
    }
#line 2404 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 81: /* setClause: colName '=' colName '+' value  */
#line 474 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 带空格加号 v = v + 1 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), (yyvsp[0].sv_val), false);
    }
#line 2413 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 82: /* setClause: colName '=' colName '-' value  */
#line 479 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 带空格减号 v = v - 1：字面量为正、置 neg 取负 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), (yyvsp[0].sv_val), true);
    }
#line 2422 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 83: /* agg_list: agg_func  */
#line 489 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2430 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 84: /* agg_list: agg_list ',' agg_func  */
#line 493 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2438 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 85: /* agg_func: COUNT '(' '*' ')' opt_alias  */
#line 500 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, nullptr, (yyvsp[0].sv_str), true);
    }
#line 2446 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 86: /* agg_func: COUNT '(' col ')' opt_alias  */
#line 504 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2454 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 87: /* agg_func: COUNT '(' DISTINCT col ')' opt_alias  */
#line 508 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2463 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 88: /* agg_func: COUNT '(' DISTINCT '(' col ')' ')' opt_alias  */
#line 513 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2472 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 89: /* agg_func: MAX '(' col ')' opt_alias  */
#line 518 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MAX, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2480 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 90: /* agg_func: MIN '(' col ')' opt_alias  */
#line 522 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MIN, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2488 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 91: /* agg_func: SUM '(' col ')' opt_alias  */
#line 526 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_SUM, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2496 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 92: /* agg_func: AVG '(' col ')' opt_alias  */
#line 530 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_AVG, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2504 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 93: /* opt_alias: %empty  */
#line 537 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2512 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 94: /* opt_alias: AS IDENTIFIER  */
#line 541 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2520 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 95: /* opt_alias: IDENTIFIER  */
#line 545 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2528 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 96: /* tableList: tbName  */
#line 566 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = std::make_shared<TableListInfo>();
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2537 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 97: /* tableList: tableList ',' tbName  */
#line 571 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-2].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2546 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 98: /* tableList: tableList JOIN tbName  */
#line 576 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-2].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2555 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 99: /* tableList: tableList JOIN tbName ON whereClause  */
#line 581 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-4].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[-2].sv_str));
        (yyval.sv_table_list)->join_conds.insert((yyval.sv_table_list)->join_conds.end(), (yyvsp[0].sv_conds).begin(), (yyvsp[0].sv_conds).end());
    }
#line 2565 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 100: /* opt_group_by: %empty  */
#line 590 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 2573 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 101: /* opt_group_by: GROUP BY group_by_list  */
#line 594 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 2581 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 102: /* group_by_list: col  */
#line 601 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2589 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 103: /* group_by_list: group_by_list ',' col  */
#line 605 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2597 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 104: /* having_condition: col op expr  */
#line 612 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2605 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 105: /* having_condition: agg_func op expr  */
#line 616 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        auto col = std::make_shared<Col>("", (yyvsp[-2].sv_agg_expr)->to_string());
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(col, (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2614 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 106: /* having_clause: having_condition  */
#line 624 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2622 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 107: /* having_clause: having_clause AND having_condition  */
#line 628 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2630 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 108: /* opt_having: %empty  */
#line 635 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = {};
    }
#line 2638 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 109: /* opt_having: HAVING having_clause  */
#line 639 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2646 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 110: /* opt_order_clause: ORDER BY order_list  */
#line 646 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 2654 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 111: /* opt_order_clause: %empty  */
#line 650 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 2662 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 112: /* order_list: order_clause  */
#line 657 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 2670 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 113: /* order_list: order_list ',' order_clause  */
#line 661 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 2678 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 114: /* order_clause: col opt_asc_desc  */
#line 668 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 2686 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 115: /* opt_asc_desc: ASC  */
#line 674 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2692 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 116: /* opt_asc_desc: DESC  */
#line 675 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2698 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 117: /* opt_asc_desc: %empty  */
#line 676 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2704 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 118: /* opt_limit: %empty  */
#line 681 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 2712 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 119: /* opt_limit: LIMIT VALUE_INT  */
#line 685 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 2720 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 120: /* set_knob_type: ENABLE_NESTLOOP  */
#line 691 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2726 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;

  case 121: /* set_knob_type: ENABLE_SORTMERGE  */
#line 692 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2732 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"
    break;


#line 2736 "/home/smart/workspace/2026/db2026/src/parser/yacc.tab.cpp"

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

#line 698 "/home/smart/workspace/2026/db2026/src/parser/yacc.y"

