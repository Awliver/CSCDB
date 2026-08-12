/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_YACC_TAB_H_INCLUDED
# define YY_YY_YACC_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    SHOW = 258,                    /* SHOW  */
    TABLES = 259,                  /* TABLES  */
    CREATE = 260,                  /* CREATE  */
    TABLE = 261,                   /* TABLE  */
    DROP = 262,                    /* DROP  */
    DESC = 263,                    /* DESC  */
    INSERT = 264,                  /* INSERT  */
    INTO = 265,                    /* INTO  */
    VALUES = 266,                  /* VALUES  */
    DELETE = 267,                  /* DELETE  */
    FROM = 268,                    /* FROM  */
    ASC = 269,                     /* ASC  */
    ORDER = 270,                   /* ORDER  */
    BY = 271,                      /* BY  */
    WHERE = 272,                   /* WHERE  */
    UPDATE = 273,                  /* UPDATE  */
    SET = 274,                     /* SET  */
    SELECT = 275,                  /* SELECT  */
    EXPLAIN = 276,                 /* EXPLAIN  */
    ANALYZE = 277,                 /* ANALYZE  */
    INT = 278,                     /* INT  */
    CHAR = 279,                    /* CHAR  */
    FLOAT = 280,                   /* FLOAT  */
    INDEX = 281,                   /* INDEX  */
    AND = 282,                     /* AND  */
    OR = 283,                      /* OR  */
    NOT = 284,                     /* NOT  */
    JOIN = 285,                    /* JOIN  */
    ON = 286,                      /* ON  */
    EXIT = 287,                    /* EXIT  */
    HELP = 288,                    /* HELP  */
    TXN_BEGIN = 289,               /* TXN_BEGIN  */
    TXN_COMMIT = 290,              /* TXN_COMMIT  */
    TXN_ABORT = 291,               /* TXN_ABORT  */
    TXN_ROLLBACK = 292,            /* TXN_ROLLBACK  */
    ORDER_BY = 293,                /* ORDER_BY  */
    ENABLE_NESTLOOP = 294,         /* ENABLE_NESTLOOP  */
    ENABLE_SORTMERGE = 295,        /* ENABLE_SORTMERGE  */
    COUNT = 296,                   /* COUNT  */
    MAX = 297,                     /* MAX  */
    MIN = 298,                     /* MIN  */
    SUM = 299,                     /* SUM  */
    AVG = 300,                     /* AVG  */
    AS = 301,                      /* AS  */
    GROUP = 302,                   /* GROUP  */
    HAVING = 303,                  /* HAVING  */
    LIMIT = 304,                   /* LIMIT  */
    UNION = 305,                   /* UNION  */
    DISTINCT = 306,                /* DISTINCT  */
    LEFT = 307,                    /* LEFT  */
    RIGHT = 308,                   /* RIGHT  */
    INNER = 309,                   /* INNER  */
    OUTER = 310,                   /* OUTER  */
    CROSS = 311,                   /* CROSS  */
    FULL = 312,                    /* FULL  */
    NATURAL = 313,                 /* NATURAL  */
    SEMI = 314,                    /* SEMI  */
    ANTI = 315,                    /* ANTI  */
    LATERAL = 316,                 /* LATERAL  */
    LEQ = 317,                     /* LEQ  */
    NEQ = 318,                     /* NEQ  */
    GEQ = 319,                     /* GEQ  */
    T_EOF = 320,                   /* T_EOF  */
    IDENTIFIER = 321,              /* IDENTIFIER  */
    VALUE_STRING = 322,            /* VALUE_STRING  */
    VALUE_INT = 323,               /* VALUE_INT  */
    VALUE_FLOAT = 324,             /* VALUE_FLOAT  */
    VALUE_BOOL = 325               /* VALUE_BOOL  */
  };
  typedef enum yytokentype yytoken_kind_t;
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


#endif /* !YY_YY_YACC_TAB_H_INCLUDED  */
