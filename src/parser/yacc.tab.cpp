/* A Bison parser, made by GNU Bison 3.5.1.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.5.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

#line 85 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"

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

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
#ifndef YY_YY_HOME_NEO_CSC_DB_DB2026_SRC_PARSER_YACC_TAB_H_INCLUDED
# define YY_YY_HOME_NEO_CSC_DB_DB2026_SRC_PARSER_YACC_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    SHOW = 258,
    TABLES = 259,
    CREATE = 260,
    TABLE = 261,
    DROP = 262,
    DESC = 263,
    INSERT = 264,
    INTO = 265,
    VALUES = 266,
    DELETE = 267,
    FROM = 268,
    ASC = 269,
    ORDER = 270,
    BY = 271,
    WHERE = 272,
    UPDATE = 273,
    SET = 274,
    SELECT = 275,
    EXPLAIN = 276,
    ANALYZE = 277,
    INT = 278,
    CHAR = 279,
    FLOAT = 280,
    INDEX = 281,
    AND = 282,
    JOIN = 283,
    ON = 284,
    EXIT = 285,
    HELP = 286,
    TXN_BEGIN = 287,
    TXN_COMMIT = 288,
    TXN_ABORT = 289,
    TXN_ROLLBACK = 290,
    ORDER_BY = 291,
    ENABLE_NESTLOOP = 292,
    ENABLE_SORTMERGE = 293,
    COUNT = 294,
    MAX = 295,
    MIN = 296,
    SUM = 297,
    AVG = 298,
    AS = 299,
    GROUP = 300,
    HAVING = 301,
    LIMIT = 302,
    UNION = 303,
    DISTINCT = 304,
    LEQ = 305,
    NEQ = 306,
    GEQ = 307,
    T_EOF = 308,
    IDENTIFIER = 309,
    VALUE_STRING = 310,
    VALUE_INT = 311,
    VALUE_FLOAT = 312,
    VALUE_BOOL = 313
  };
#endif

/* Value type.  */

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



int yyparse (void);

#endif /* !YY_YY_HOME_NEO_CSC_DB_DB2026_SRC_PARSER_YACC_TAB_H_INCLUDED  */



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
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
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

#if ! defined yyoverflow || YYERROR_VERBOSE

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
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


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
#define YYLAST   330

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  44
/* YYNRULES -- Number of rules.  */
#define YYNRULES  120
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  298

#define YYUNDEFTOK  2
#define YYMAXUTOK   313


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

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
     363,   367,   374,   378,   385,   389,   396,   400,   404,   408,
     412,   416,   423,   427,   434,   438,   445,   449,   454,   459,
     469,   473,   480,   484,   488,   493,   498,   502,   506,   510,
     518,   521,   525,   546,   551,   556,   561,   571,   574,   581,
     585,   592,   596,   604,   608,   616,   619,   626,   631,   637,
     641,   648,   655,   656,   657,   662,   665,   672,   673,   676,
     678
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "SHOW", "TABLES", "CREATE", "TABLE",
  "DROP", "DESC", "INSERT", "INTO", "VALUES", "DELETE", "FROM", "ASC",
  "ORDER", "BY", "WHERE", "UPDATE", "SET", "SELECT", "EXPLAIN", "ANALYZE",
  "INT", "CHAR", "FLOAT", "INDEX", "AND", "JOIN", "ON", "EXIT", "HELP",
  "TXN_BEGIN", "TXN_COMMIT", "TXN_ABORT", "TXN_ROLLBACK", "ORDER_BY",
  "ENABLE_NESTLOOP", "ENABLE_SORTMERGE", "COUNT", "MAX", "MIN", "SUM",
  "AVG", "AS", "GROUP", "HAVING", "LIMIT", "UNION", "DISTINCT", "LEQ",
  "NEQ", "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT",
  "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='", "'('", "')'", "'*'", "','",
  "'.'", "'<'", "'>'", "'+'", "'-'", "$accept", "start", "stmt", "txnStmt",
  "dbStmt", "setStmt", "ddl", "dml", "select_stmt", "union_branch",
  "union_query", "fieldList", "colNameList", "field", "type", "valueList",
  "valueRows", "value", "condition", "optWhereClause", "whereClause",
  "col", "colList", "op", "expr", "setClauses", "setClause", "agg_list",
  "agg_func", "opt_alias", "tableList", "opt_group_by", "group_by_list",
  "having_condition", "having_clause", "opt_having", "opt_order_clause",
  "order_list", "order_clause", "opt_asc_desc", "opt_limit",
  "set_knob_type", "tbName", "colName", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,    59,
      61,    40,    41,    42,    44,    46,    60,    62,    43,    45
};
# endif

#define YYPACT_NINF (-173)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-120)

#define yytable_value_is_error(Yyn) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     214,    14,    31,    33,   -21,    46,    50,   -21,   159,   125,
      80,  -173,  -173,  -173,  -173,  -173,  -173,  -173,    51,    44,
    -173,  -173,  -173,  -173,  -173,  -173,  -173,    99,   -21,   -21,
     -21,   -21,  -173,  -173,   -21,   -21,    87,  -173,  -173,    63,
      69,    73,    85,    91,    97,   122,   198,  -173,    -6,     1,
    -173,   150,  -173,   135,   236,  -173,  -173,  -173,   -21,   203,
     207,  -173,   213,     9,   259,   228,   225,    65,   230,   230,
     230,   230,    43,   -21,   151,   -21,   220,   228,   272,  -173,
    -173,   228,   228,   228,   226,   228,   230,  -173,  -173,    19,
    -173,   229,  -173,    61,   224,   231,   232,   233,   234,   235,
     268,    21,  -173,    21,  -173,     2,    21,  -173,  -173,   -21,
      67,  -173,   102,    89,  -173,    93,   172,   227,   109,  -173,
     263,   191,   228,  -173,   182,   230,   237,    -2,    -2,    -2,
      -2,    -2,    -2,   141,   244,    -7,   -21,   -21,   253,   253,
     -21,   253,  -173,   228,  -173,   239,  -173,  -173,  -173,   228,
    -173,  -173,  -173,  -173,  -173,   146,  -173,   240,   291,   230,
    -173,  -173,  -173,  -173,  -173,  -173,   215,  -173,  -173,   197,
     241,    -2,   250,  -173,  -173,  -173,  -173,  -173,  -173,  -173,
     292,     8,    10,   268,   268,   262,   278,  -173,   293,   264,
     264,    21,   264,  -173,   252,  -173,  -173,   172,   172,   251,
    -173,  -173,  -173,  -173,   172,   172,  -173,   249,  -173,  -173,
     -21,   -21,   151,   -21,  -173,  -173,   -21,   230,   230,   151,
     298,   298,   253,   298,   254,  -173,   152,   172,  -173,  -173,
      -2,    21,    21,    16,    21,   298,   263,  -173,   255,   191,
     191,  -173,   287,   299,   270,   270,   264,   270,  -173,  -173,
     156,  -173,   253,   253,   -21,   253,   270,   230,   215,   215,
     151,   230,   265,  -173,  -173,   298,  -173,  -173,   264,   264,
      21,   264,  -173,  -173,  -173,  -173,  -173,    68,   256,  -173,
    -173,   270,   270,   270,   253,   270,  -173,  -173,  -173,   230,
    -173,  -173,  -173,   264,  -173,  -173,   270,  -173
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,     3,    11,    12,    13,    14,     5,     0,     0,
       9,     6,    10,     7,     8,    27,    15,     0,     0,     0,
       0,     0,   119,    20,     0,     0,     0,   117,   118,     0,
       0,     0,     0,     0,     0,   120,     0,    64,     0,     0,
      80,     0,    63,     0,     0,    28,     1,     2,     0,     0,
       0,    19,     0,     0,    58,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    29,
      16,     0,     0,     0,     0,     0,     0,    25,   120,    58,
      74,     0,    17,     0,     0,     0,     0,     0,     0,     0,
       0,    58,    93,    58,    65,     0,    58,    81,    62,     0,
       0,    41,     0,     0,    43,     0,     0,    23,     0,    60,
      59,     0,     0,    26,     0,     0,     0,    90,    90,    90,
      90,    90,    90,     0,     0,     0,     0,     0,    97,    97,
       0,    97,    18,     0,    46,     0,    48,    45,    21,     0,
      22,    55,    53,    54,    56,     0,    49,     0,     0,     0,
      70,    69,    71,    66,    67,    68,     0,    75,    76,     0,
       0,    90,     0,    92,    82,    83,    86,    87,    88,    89,
       0,     0,     0,     0,     0,     0,    95,    94,     0,   105,
     105,    58,   105,    42,     0,    44,    51,     0,     0,     0,
      61,    72,    73,    57,     0,     0,    77,     0,    84,    91,
       0,     0,     0,     0,    39,    40,     0,     0,     0,     0,
     108,   108,    97,   108,     0,    50,     0,     0,    78,    79,
      90,    58,    58,     0,    58,   108,    96,    99,    98,     0,
       0,   103,   106,     0,   115,   115,   105,   115,    47,    52,
       0,    85,    97,    97,     0,    97,   115,     0,     0,     0,
       0,     0,     0,    31,    32,   108,    33,    24,   105,   105,
      58,   105,    30,   100,   101,   102,   104,   114,   107,   109,
     116,   115,   115,   115,    97,   115,   113,   112,   111,     0,
      34,    35,    36,   105,    37,   110,   115,    38
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -173,  -173,  -173,  -173,  -173,  -173,  -173,  -173,    38,    41,
    -173,  -173,   192,   175,  -173,  -134,  -173,  -116,   163,   -84,
     106,   -58,   193,    39,    22,  -173,   202,   -72,   -75,   -85,
     -41,  -135,  -173,    70,  -173,  -130,  -144,  -173,    36,  -173,
    -172,  -173,    -4,    13
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,    18,    19,    20,    21,    22,    23,    24,    25,   134,
     135,   110,   113,   111,   147,   155,   117,   156,   119,    87,
     120,    47,    48,   166,   203,    89,    90,    49,    50,   174,
     101,   189,   238,   241,   242,   220,   244,   278,   279,   288,
     263,    39,    51,    52
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      33,   107,   105,    36,   190,   123,   192,    73,   168,    95,
      96,    97,    98,    99,    75,   140,   104,   138,    26,   139,
      84,   211,   141,   213,    59,    60,    61,    62,   121,   254,
      63,    64,   103,    32,   106,   126,    86,    28,    86,    30,
      27,   184,   172,   175,   176,   177,   178,   179,    55,   136,
     201,    56,   173,   206,    80,   185,    34,    29,    74,    31,
     221,   182,   223,    35,   226,    76,    76,   170,   102,   102,
      85,   102,   212,   264,    76,   266,   286,   245,    91,   247,
      76,   225,   287,   122,   272,   137,   208,   246,   228,   229,
     108,   256,    79,   250,   112,   114,   114,    32,   114,   191,
      53,   121,    54,    57,   100,   102,    65,   222,   202,   290,
     291,   292,    58,   294,    93,    45,   265,   268,   269,    45,
     271,   281,   125,    66,   297,   144,   145,   146,    94,   142,
      67,   143,   186,   187,    68,    91,   102,   169,   282,   283,
     233,   285,   201,   201,   240,   251,    69,   252,   253,   293,
     255,   148,    70,   149,   104,   150,   112,   149,    71,   121,
     237,   239,   195,   296,    40,    41,    42,    43,    44,   231,
     232,   158,   234,   149,    40,    41,    42,    43,    44,    45,
      40,    41,    42,    43,    44,   240,   284,  -119,    46,    45,
      40,    41,    42,    43,    44,    45,    37,    38,    78,   273,
     202,   202,   239,   277,   180,    45,   102,   102,   196,   102,
     197,    72,   235,   270,   249,    77,   197,     1,   267,     2,
     197,     3,     4,     5,   214,   215,     6,   151,   152,   153,
     154,   277,     7,     8,     9,    10,    88,   151,   152,   153,
     154,   160,   161,   162,    11,    12,    13,    14,    15,    16,
     102,   163,   151,   152,   153,   154,    53,   164,   165,    40,
      41,    42,    43,    44,    81,   204,   205,    17,    82,    45,
     151,   152,   153,   154,    83,   115,    86,   118,   258,   259,
     274,   275,    88,    92,    45,   109,   127,   116,   133,   124,
     159,   157,   183,   128,   129,   130,   131,   132,   188,   171,
     194,   198,   199,   207,   209,   210,   216,   217,   224,   218,
     219,   230,   227,   243,   260,   261,   248,   262,   193,   257,
     289,   280,   200,   236,   167,   295,   181,     0,     0,     0,
     276
};

static const yytype_int16 yycheck[] =
{
       4,    76,    74,     7,   139,    89,   141,    13,   124,    67,
      68,    69,    70,    71,    13,    13,    74,   101,     4,   103,
      11,    13,   106,    13,    28,    29,    30,    31,    86,    13,
      34,    35,    73,    54,    75,    93,    17,     6,    17,     6,
      26,    48,    44,   128,   129,   130,   131,   132,    10,    28,
     166,     0,    54,   169,    58,    62,    10,    26,    64,    26,
     190,   133,   192,    13,   198,    64,    64,   125,    72,    73,
      61,    75,    64,   245,    64,   247,     8,   221,    65,   223,
      64,   197,    14,    64,   256,    64,   171,   222,   204,   205,
      77,   235,    54,   227,    81,    82,    83,    54,    85,   140,
      20,   159,    22,    59,    61,   109,    19,   191,   166,   281,
     282,   283,    13,   285,    49,    54,   246,   252,   253,    54,
     255,   265,    61,    60,   296,    23,    24,    25,    63,    62,
      61,    64,   136,   137,    61,   122,   140,   124,   268,   269,
     212,   271,   258,   259,   219,   230,    61,   231,   232,   284,
     234,    62,    61,    64,   212,    62,   143,    64,    61,   217,
     218,   219,   149,   293,    39,    40,    41,    42,    43,   210,
     211,    62,   213,    64,    39,    40,    41,    42,    43,    54,
      39,    40,    41,    42,    43,   260,   270,    65,    63,    54,
      39,    40,    41,    42,    43,    54,    37,    38,    63,   257,
     258,   259,   260,   261,    63,    54,   210,   211,    62,   213,
      64,    13,   216,   254,    62,    65,    64,     3,    62,     5,
      64,     7,     8,     9,   183,   184,    12,    55,    56,    57,
      58,   289,    18,    19,    20,    21,    54,    55,    56,    57,
      58,    50,    51,    52,    30,    31,    32,    33,    34,    35,
     254,    60,    55,    56,    57,    58,    20,    66,    67,    39,
      40,    41,    42,    43,    61,    68,    69,    53,    61,    54,
      55,    56,    57,    58,    61,    83,    17,    85,   239,   240,
     258,   259,    54,    58,    54,    13,    62,    61,    20,    60,
      27,    64,    48,    62,    62,    62,    62,    62,    45,    62,
      61,    61,    11,    62,    54,    13,    44,    29,    56,    16,
      46,    62,    61,    15,    27,    16,    62,    47,   143,    64,
      64,    56,   159,   217,   122,   289,   133,    -1,    -1,    -1,
     260
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      21,    30,    31,    32,    33,    34,    35,    53,    71,    72,
      73,    74,    75,    76,    77,    78,     4,    26,     6,    26,
       6,    26,    54,   112,    10,    13,   112,    37,    38,   111,
      39,    40,    41,    42,    43,    54,    63,    91,    92,    97,
      98,   112,   113,    20,    22,    78,     0,    59,    13,   112,
     112,   112,   112,   112,   112,    19,    60,    61,    61,    61,
      61,    61,    13,    13,    64,    13,    64,    65,    63,    78,
     112,    61,    61,    61,    11,    61,    17,    89,    54,    95,
      96,   113,    58,    49,    63,    91,    91,    91,    91,    91,
      61,   100,   112,   100,    91,    97,   100,    98,   113,    13,
      81,    83,   113,    82,   113,    82,    61,    86,    82,    88,
      90,    91,    64,    89,    60,    61,    91,    62,    62,    62,
      62,    62,    62,    20,    79,    80,    28,    64,    89,    89,
      13,    89,    62,    64,    23,    24,    25,    84,    62,    64,
      62,    55,    56,    57,    58,    85,    87,    64,    62,    27,
      50,    51,    52,    60,    66,    67,    93,    96,    87,   113,
      91,    62,    44,    54,    99,    99,    99,    99,    99,    99,
      63,    92,    97,    48,    48,    62,   112,   112,    45,   101,
     101,   100,   101,    83,    61,   113,    62,    64,    61,    11,
      88,    87,    91,    94,    68,    69,    87,    62,    99,    54,
      13,    13,    64,    13,    79,    79,    44,    29,    16,    46,
     105,   105,    89,   105,    56,    87,    85,    61,    87,    87,
      62,   100,   100,    97,   100,   112,    90,    91,   102,    91,
      98,   103,   104,    15,   106,   106,   101,   106,    62,    62,
      85,    99,    89,    89,    13,    89,   106,    64,    93,    93,
      27,    16,    47,   110,   110,   105,   110,    62,   101,   101,
     100,   101,   110,    91,    94,    94,   103,    91,   107,   108,
      56,   106,   105,   105,    89,   105,     8,    14,   109,    64,
     110,   110,   110,   101,   110,   108,   105,   110
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_int8 yyr1[] =
{
       0,    70,    71,    71,    71,    71,    72,    72,    72,    72,
      72,    73,    73,    73,    73,    74,    74,    75,    76,    76,
      76,    76,    76,    77,    77,    77,    77,    77,    77,    77,
      77,    78,    78,    78,    78,    79,    79,    79,    79,    80,
      80,    81,    81,    82,    82,    83,    84,    84,    84,    85,
      85,    86,    86,    87,    87,    87,    87,    88,    89,    89,
      90,    90,    91,    91,    92,    92,    93,    93,    93,    93,
      93,    93,    94,    94,    95,    95,    96,    96,    96,    96,
      97,    97,    98,    98,    98,    98,    98,    98,    98,    98,
      99,    99,    99,   100,   100,   100,   100,   101,   101,   102,
     102,   103,   103,   104,   104,   105,   105,   106,   106,   107,
     107,   108,   109,   109,   109,   110,   110,   111,   111,   112,
     113
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     5,    10,     4,     5,     1,     2,     3,
      10,     9,     9,     9,    11,     8,     8,     8,    10,     3,
       3,     1,     3,     1,     3,     2,     1,     4,     1,     1,
       3,     3,     5,     1,     1,     1,     1,     3,     0,     2,
       1,     3,     3,     1,     1,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     3,     4,     5,     5,
       1,     3,     5,     5,     6,     8,     5,     5,     5,     5,
       0,     2,     1,     1,     3,     3,     5,     0,     3,     1,
       3,     3,     3,     1,     3,     0,     2,     3,     0,     1,
       3,     2,     1,     1,     0,     0,     2,     1,     1,     1,
       1
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


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

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256


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


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

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

#  define YY_LOCATION_PRINT(File, Loc)          \
  yy_location_print_ (File, &(Loc))

# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YYUSE (yyoutput);
  YYUSE (yylocationp);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yytype], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  YY_LOCATION_PRINT (yyo, *yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yytype, yyvaluep, yylocationp);
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
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
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
                       yystos[+yyssp[yyi + 1 - yynrhs]],
                       &yyvsp[(yyi + 1) - (yynrhs)]
                       , &(yylsp[(yyi + 1) - (yynrhs)])                       );
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
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
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


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
#  else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
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
#  endif
# endif

# ifndef yytnamerr
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
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                yy_state_t *yyssp, int yytoken)
{
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Actual size of YYARG. */
  int yycount = 0;
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

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
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[+*yyssp];
      YYPTRDIFF_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
      yysize = yysize0;
      yyarg[yycount++] = yytname[yytoken];
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
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYPTRDIFF_T yysize1
                    = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
                    yysize = yysize1;
                  else
                    return 2;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
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
# undef YYCASE_
    }

  {
    /* Don't count the "%s"s in the final size, but reserve room for
       the terminator.  */
    YYPTRDIFF_T yysize1 = yysize + (yystrlen (yyformat) - 2 * yycount) + 1;
    if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
      yysize = yysize1;
    else
      return 2;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
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
          yyp += yytnamerr (yyp, yyarg[yyi++]);
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
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
/* The lookahead symbol.  */
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
    int yynerrs;

    yy_state_fast_t yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.
       'yyls': related to locations.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss;
    yy_state_t *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    /* The location stack.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls;
    YYLTYPE *yylsp;

    /* The locations where the error started and ended.  */
    YYLTYPE yyerror_range[3];

    YYPTRDIFF_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yylsp = yyls = yylsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
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

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
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
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
# undef YYSTACK_RELOCATE
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

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
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
  case 2:
#line 71 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1693 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 3:
#line 76 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1702 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 4:
#line 81 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1711 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 5:
#line 86 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1720 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 11:
#line 102 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1728 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 12:
#line 106 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1736 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 13:
#line 110 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1744 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 14:
#line 114 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1752 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 15:
#line 121 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1760 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 16:
#line 125 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1768 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 17:
#line 132 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1776 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 18:
#line 139 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1784 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 19:
#line 143 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1792 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 20:
#line 147 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1800 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 21:
#line 151 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1808 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 22:
#line 155 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1816 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 23:
#line 162 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-2].sv_str), (yyvsp[0].sv_vals));
    }
#line 1824 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 24:
#line 166 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-7].sv_str), (yyvsp[-5].sv_strs), (yyvsp[-1].sv_vals));
    }
#line 1832 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 25:
#line 170 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1840 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 26:
#line 174 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1848 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 27:
#line 178 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select);
    }
#line 1856 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 28:
#line 182 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), false);
    }
#line 1864 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 29:
#line 186 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ExplainStmt>((yyvsp[0].sv_select), true);
    }
#line 1872 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 30:
#line 190 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_selects), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1880 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 31:
#line 197 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1890 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 32:
#line 203 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-7].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1900 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 33:
#line 209 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1910 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 34:
#line 215 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-5].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-4].sv_conds).begin(), (yyvsp[-4].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-9].sv_cols), (yyvsp[-7].sv_agg_exprs), (yyvsp[-5].sv_table_list)->tabs, conds, (yyvsp[-3].sv_cols), (yyvsp[-2].sv_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1920 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 35:
#line 224 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1930 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 36:
#line 230 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-6].sv_cols), std::vector<std::shared_ptr<AggExpr>>{}, (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1940 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 37:
#line 236 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>(std::vector<std::shared_ptr<Col>>{}, (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1950 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 38:
#line 242 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-4].sv_table_list)->join_conds;
        conds.insert(conds.end(), (yyvsp[-3].sv_conds).begin(), (yyvsp[-3].sv_conds).end());
        (yyval.sv_select) = std::make_shared<SelectStmt>((yyvsp[-8].sv_cols), (yyvsp[-6].sv_agg_exprs), (yyvsp[-4].sv_table_list)->tabs, conds, (yyvsp[-2].sv_cols), (yyvsp[-1].sv_conds), std::vector<std::shared_ptr<OrderBy>>{}, (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int) < 0 ? 0 : (yyvsp[0].sv_int));
    }
#line 1960 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 39:
#line 251 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_selects) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select), (yyvsp[0].sv_select)};
    }
#line 1968 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 40:
#line 255 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_selects) = (yyvsp[-2].sv_selects);
        (yyval.sv_selects).push_back((yyvsp[0].sv_select));
    }
#line 1977 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 41:
#line 263 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 1985 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 42:
#line 267 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 1993 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 43:
#line 274 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2001 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 44:
#line 278 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2009 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 45:
#line 285 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2017 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 46:
#line 292 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2025 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 47:
#line 296 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2033 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 48:
#line 300 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2041 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 49:
#line 307 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2049 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 50:
#line 311 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2057 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 51:
#line 318 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-1].sv_vals);
    }
#line 2065 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 52:
#line 322 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = (yyvsp[-4].sv_vals);
        for (auto &v : (yyvsp[-1].sv_vals)) (yyval.sv_vals).push_back(v);
    }
#line 2074 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 53:
#line 330 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2082 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 54:
#line 334 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2090 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 55:
#line 338 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2098 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 56:
#line 342 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2106 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 57:
#line 349 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2114 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 58:
#line 355 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2120 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 59:
#line 357 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2128 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 60:
#line 364 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2136 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 61:
#line 368 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2144 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 62:
#line 375 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2152 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 63:
#line 379 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2160 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 64:
#line 386 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2168 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 65:
#line 390 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2176 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 66:
#line 397 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2184 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 67:
#line 401 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2192 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 68:
#line 405 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2200 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 69:
#line 409 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2208 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 70:
#line 413 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2216 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 71:
#line 417 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2224 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 72:
#line 424 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2232 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 73:
#line 428 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2240 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 74:
#line 435 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2248 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 75:
#line 439 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2256 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 76:
#line 446 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2264 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 77:
#line 450 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        /* 题9：算术增量 v=v+1（词法把 +1/-1 归并为带符号 VALUE_INT，无空格情形） */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_val), false);
    }
#line 2273 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 78:
#line 455 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        /* 带空格加号 v = v + 1 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), (yyvsp[0].sv_val), false);
    }
#line 2282 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 79:
#line 460 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        /* 带空格减号 v = v - 1：字面量为正、置 neg 取负 */
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), (yyvsp[0].sv_val), true);
    }
#line 2291 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 80:
#line 470 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs) = std::vector<std::shared_ptr<AggExpr>>{(yyvsp[0].sv_agg_expr)};
    }
#line 2299 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 81:
#line 474 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_exprs).push_back((yyvsp[0].sv_agg_expr));
    }
#line 2307 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 82:
#line 481 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, nullptr, (yyvsp[0].sv_str), true);
    }
#line 2315 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 83:
#line 485 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2323 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 84:
#line 489 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        /* 决赛：原生 COUNT(DISTINCT col) */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2332 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 85:
#line 494 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        /* 决赛：COUNT(DISTINCT (col)) 括号变体 */
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_COUNT, (yyvsp[-3].sv_col), (yyvsp[0].sv_str), false, true);
    }
#line 2341 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 86:
#line 499 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MAX, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2349 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 87:
#line 503 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_MIN, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2357 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 88:
#line 507 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_SUM, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2365 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 89:
#line 511 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_agg_expr) = std::make_shared<AggExpr>(AGG_AVG, (yyvsp[-2].sv_col), (yyvsp[0].sv_str), false);
    }
#line 2373 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 90:
#line 518 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2381 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 91:
#line 522 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2389 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 92:
#line 526 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2397 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 93:
#line 547 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = std::make_shared<TableListInfo>();
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2406 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 94:
#line 552 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-2].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2415 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 95:
#line 557 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-2].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[0].sv_str));
    }
#line 2424 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 96:
#line 562 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_list) = (yyvsp[-4].sv_table_list);
        (yyval.sv_table_list)->tabs.push_back((yyvsp[-2].sv_str));
        (yyval.sv_table_list)->join_conds.insert((yyval.sv_table_list)->join_conds.end(), (yyvsp[0].sv_conds).begin(), (yyvsp[0].sv_conds).end());
    }
#line 2434 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 97:
#line 571 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 2442 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 98:
#line 575 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 2450 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 99:
#line 582 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2458 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 100:
#line 586 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2466 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 101:
#line 593 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2474 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 102:
#line 597 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        auto col = std::make_shared<Col>("", (yyvsp[-2].sv_agg_expr)->to_string());
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(col, (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2483 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 103:
#line 605 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2491 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 104:
#line 609 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2499 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 105:
#line 616 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = {};
    }
#line 2507 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 106:
#line 620 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2515 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 107:
#line 627 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 2523 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 108:
#line 631 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {};
    }
#line 2531 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 109:
#line 638 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 2539 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 110:
#line 642 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 2547 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 111:
#line 649 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 2555 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 112:
#line 655 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2561 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 113:
#line 656 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2567 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 114:
#line 657 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2573 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 115:
#line 662 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 2581 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 116:
#line 666 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 2589 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 117:
#line 672 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2595 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;

  case 118:
#line 673 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2601 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"
    break;


#line 2605 "/home/neo/CSC_DB/db2026/src/parser/yacc.tab.cpp"

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
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

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
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (&yylloc, YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *, YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (&yylloc, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
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

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
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
                  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the lookahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, yyerror_range, 2);
  *++yylsp = yyloc;

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif


/*-----------------------------------------------------.
| yyreturn -- parsing is finished, return the result.  |
`-----------------------------------------------------*/
yyreturn:
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
                  yystos[+*yyssp], yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 679 "/home/neo/CSC_DB/db2026/src/parser/yacc.y"

