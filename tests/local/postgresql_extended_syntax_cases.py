"""Provenance for RMDB's PostgreSQL-derived extended-SQL regression gate.

The source is PostgreSQL's official GitHub mirror, pinned to one commit.  The
gate uses small table-backed fixtures because RMDB does not support PostgreSQL's
catalog fixtures, SELECT without FROM, casts, expressions in a SELECT list, or
base-table NULL values.  Each adaptation keeps the operator and the semantic
property under test; expected rows are asserted in the executable gate.

No upstream PostgreSQL regression SQL at the pinned commit contains a
multi-argument ``COUNT(DISTINCT a, b)`` query.  The two such checks below are
therefore labelled RMDB extensions rather than being attributed to PostgreSQL.
"""

from __future__ import annotations

from dataclasses import dataclass


POSTGRESQL_REPOSITORY = "https://github.com/postgres/postgres"
POSTGRESQL_COMMIT = "aaf67e4825f32d764296fabfa3a86c4da90cec16"

UPSTREAM_SOURCES = {
    "select_distinct": {
        "sql": "src/test/regress/sql/select_distinct.sql",
        "sql_blob": "2ed1616b098f7eb57bb74d0f1b57e7fc91ead63e",
        "expected": "src/test/regress/expected/select_distinct.out",
        "expected_blob": "741f72387423e748ea0ebdd6c8cf4617917de173",
    },
    "join": {
        "sql": "src/test/regress/sql/join.sql",
        "sql_blob": "450bd5bbf2c417fa0f7ed3c8d2ebc5e1dc785925",
        "expected": "src/test/regress/expected/join.out",
        "expected_blob": "05f359d3aa79bb00f0713f72bd5c63775596da4a",
    },
    "limit": {
        "sql": "src/test/regress/sql/limit.sql",
        "sql_blob": "603910fe6d11c10350a0f6e148a1f50b52504268",
        "expected": "src/test/regress/expected/limit.out",
        "expected_blob": "e3bcc680653b35a3491dceb6fb6413a51268b98a",
    },
    "aggregates": {
        "sql": "src/test/regress/sql/aggregates.sql",
        "sql_blob": "b788152f0c24d596188c12f686587327de1fac34",
        "expected": "src/test/regress/expected/aggregates.out",
        "expected_blob": "5f0668382ed863b964d23b5c991184b5450daaea",
    },
    "union": {
        "sql": "src/test/regress/sql/union.sql",
        "sql_blob": "c8de276c2b5719ee2fd81419d633652246edb90d",
        "expected": "src/test/regress/expected/union.out",
        "expected_blob": "84abcd6b14f9e17cd19556372ef24ec90527a918",
    },
    "groupingsets": {
        "sql": "src/test/regress/sql/groupingsets.sql",
        "sql_blob": "c5d4ed2eb574e40216a3666df016bdaa38683545",
        "expected": "src/test/regress/expected/groupingsets.out",
        "expected_blob": "c3f00771f6e28b7b898879683cc1c1c596e1e28f",
    },
}


def upstream_url(source: str, line: int) -> str:
    path = UPSTREAM_SOURCES[source]["sql"]
    return f"{POSTGRESQL_REPOSITORY}/blob/{POSTGRESQL_COMMIT}/{path}#L{line}"


@dataclass(frozen=True)
class UpstreamCase:
    case_id: str
    source: str
    lines: str
    upstream_sql: str
    rmdb_sql: str
    adaptation: str


CASES = {
    "distinct_single": UpstreamCase(
        "distinct_single",
        "select_distinct",
        "8",
        "SELECT DISTINCT two FROM onek ORDER BY 1;",
        "select distinct a from pgx_distinct order by 1",
        "Replace PostgreSQL's onek fixture and column two with a small duplicate-bearing INT column.",
    ),
    "distinct_multi": UpstreamCase(
        "distinct_multi",
        "select_distinct",
        "24-26",
        "SELECT DISTINCT two, string4, ten FROM onek ORDER BY two, string4, ten;",
        "select distinct a,s,b from pgx_distinct order by a,s,b",
        "Use RMDB CHAR/INT columns and standard ORDER BY syntax; retain three-column row equality.",
    ),
    "distinct_repeated_projection": UpstreamCase(
        "distinct_repeated_projection",
        "select_distinct",
        "43-46",
        "SELECT count(*) FROM (SELECT DISTINCT two, four, two FROM tenk1) ss;",
        "select count(*) from (select distinct a,b,a from pgx_distinct) ss",
        "Keep the repeated output column and assert the derived relation's cardinality.",
    ),
    "distinct_order_limit": UpstreamCase(
        "distinct_order_limit",
        "select_distinct",
        "259-272",
        "SELECT DISTINCT y, x FROM distinct_tbl LIMIT 10; SELECT DISTINCT y, x FROM distinct_tbl ORDER BY y;",
        "select distinct a,b from pgx_distinct order by a,b limit 2 offset 1",
        "Combine the upstream LIMIT and deterministic ORDER BY variants and add OFFSET coverage.",
    ),
    "using_inner": UpstreamCase(
        "using_inner",
        "join",
        "115-119",
        "SELECT * FROM J1_TBL [INNER] JOIN J2_TBL USING (i);",
        "select * from pgx_l l join pgx_r r using (k) order by k,l.k2,r.k2",
        "Rename fixtures/columns; retain the requirement that the USING key appears once.",
    ),
    "using_left": UpstreamCase(
        "using_left",
        "join",
        "186-191",
        "SELECT * FROM J1_TBL LEFT [OUTER] JOIN J2_TBL USING (i) ORDER BY i,k,t;",
        "select * from pgx_l l left join pgx_r r using (k) order by k",
        "Use non-NULL base rows so only the outer join produces NULL values.",
    ),
    "using_right": UpstreamCase(
        "using_right",
        "join",
        "194-197",
        "SELECT * FROM J1_TBL RIGHT [OUTER] JOIN J2_TBL USING (i);",
        "select * from pgx_l l right join pgx_r r using (k) order by k",
        "Add ORDER BY to make the adapted result deterministic.",
    ),
    "using_full": UpstreamCase(
        "using_full",
        "join",
        "200-205",
        "SELECT * FROM J1_TBL FULL [OUTER] JOIN J2_TBL USING (i) ORDER BY i,k,t;",
        "select * from pgx_l l full join pgx_r r using (k) order by k",
        "Rename fixtures; retain FULL JOIN key coalescing and NULL extension.",
    ),
    "using_multiple_columns": UpstreamCase(
        "using_multiple_columns",
        "groupingsets",
        "135-137",
        "SELECT a,b,... FROM gstest1 t1 JOIN gstest2 t2 USING (a,b) GROUP BY ...;",
        "select * from pgx_l l join pgx_r r using (k,k2) order by k",
        "Retain the official two-column USING key while replacing grouping sets and fixtures with direct row-shape assertions.",
    ),
    "limit_offset": UpstreamCase(
        "limit_offset",
        "limit",
        "17-23",
        "SELECT ... FROM onek ... ORDER BY unique1 [DESC] LIMIT n OFFSET m;",
        "select id from pgx_limit order by id limit 3 offset 2",
        "Use a ten-row fixture and smaller boundaries while preserving ordered slicing.",
    ),
    "limit_offset_empty": UpstreamCase(
        "limit_offset_empty",
        "limit",
        "20",
        "SELECT ... WHERE unique1 < 50 ORDER BY unique1 DESC LIMIT 8 OFFSET 99;",
        "select id from pgx_limit order by id desc limit 8 offset 99",
        "Retain an OFFSET beyond the input cardinality and assert an empty result.",
    ),
    "offset_limit": UpstreamCase(
        "offset_limit",
        "limit",
        "27-29",
        "SELECT ... FROM onek ORDER BY unique1 OFFSET 990 LIMIT 5;",
        "select id from pgx_limit order by id offset 2 limit 3",
        "Use a ten-row fixture and smaller boundaries while retaining PostgreSQL's OFFSET-before-LIMIT order.",
    ),
    "count_distinct": UpstreamCase(
        "count_distinct",
        "aggregates",
        "197-198",
        "SELECT count(DISTINCT four) AS cnt_4 FROM onek;",
        "select count(distinct four) as cnt_4 from pgx_agg",
        "Replace onek with a duplicate-bearing aggregate fixture.",
    ),
    "sum_distinct_grouped": UpstreamCase(
        "sum_distinct_grouped",
        "aggregates",
        "203-204",
        "SELECT ten,count(four),sum(DISTINCT four) FROM onek GROUP BY ten ORDER BY ten;",
        "select ten,count(four),sum(distinct four) from pgx_agg group by ten order by ten",
        "Retain grouping, ordinary COUNT, and DISTINCT SUM on a smaller fixture.",
    ),
    "avg_distinct": UpstreamCase(
        "avg_distinct",
        "aggregates",
        "1261",
        "SELECT my_avg(DISTINCT one),my_sum(DISTINCT one) FROM (VALUES(1),(3),(1)) t(one);",
        "select avg(distinct four),sum(distinct four) from pgx_agg",
        "Use built-in AVG/SUM because RMDB has no CREATE AGGREGATE; retain DISTINCT input filtering.",
    ),
    "intersect": UpstreamCase(
        "intersect",
        "union",
        "97-99",
        "SELECT q2 FROM int8_tbl INTERSECT [ALL] SELECT q1 FROM int8_tbl ORDER BY 1;",
        "select v from pgx_set_a intersect select v from pgx_set_b order by 1",
        "Replace int8_tbl with duplicate-bearing INT fixtures; distinct and ALL are separately asserted.",
    ),
    "except": UpstreamCase(
        "except",
        "union",
        "101-109",
        "SELECT q2 FROM int8_tbl EXCEPT [ALL] SELECT q1 FROM int8_tbl ORDER BY 1;",
        "select v from pgx_set_a except select v from pgx_set_b order by 1",
        "Replace int8_tbl with duplicate-bearing INT fixtures; distinct and ALL are separately asserted.",
    ),
    "set_precedence": UpstreamCase(
        "set_precedence",
        "union",
        "282-292",
        "SELECT q1 FROM int8_tbl INTERSECT SELECT q2 FROM int8_tbl UNION ALL SELECT q2 FROM int8_tbl;",
        "select v from pgx_set_a where v=3 union select v from pgx_set_a where v=1 intersect select v from pgx_set_b where v=1 order by 1",
        "Use filtered table-backed one-value branches; retain INTERSECT precedence over UNION.",
    ),
    "is_null": UpstreamCase(
        "is_null",
        "join",
        "473-479",
        "SELECT ... LEFT JOIN ... WHERE t4.f1 IS NULL;",
        "select l.k from pgx_l l left join pgx_r r on l.k=r.k where r.rv is null order by l.k",
        "Generate NULL exclusively through LEFT JOIN because RMDB base records have no NULL bitmap.",
    ),
    "is_not_null": UpstreamCase(
        "is_not_null",
        "join",
        "343-360",
        "SELECT * FROM x LEFT JOIN y ON (...) WHERE y2 IS NOT NULL;",
        "select l.k from pgx_l l left join pgx_r r on l.k=r.k where r.rv is not null order by l.k",
        "Generate NULL exclusively through LEFT JOIN and retain the post-join null-rejecting predicate.",
    ),
}


RMDB_EXTENSION_CASES = {
    "count_distinct_multiple_columns": (
        "select count(distinct ten,four) from pgx_agg",
        "RMDB requested extension; PostgreSQL's built-in count accepts one expression, and the pinned official regress SQL has no such case.",
    ),
    "count_distinct_parenthesized_multiple_columns": (
        "select count(distinct (ten,four)) from pgx_agg",
        "RMDB requested parenthesized extension; explicitly not attributed to PostgreSQL regression SQL.",
    ),
}
