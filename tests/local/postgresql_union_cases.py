"""Pinned PostgreSQL UNION regression cases supported by RMDB.

The statements below were selected from PostgreSQL's official ``union.sql``
regression test at POSTGRESQL_COMMIT.  ``upstream_sql`` is retained verbatim for
traceability.  ``rmdb_sql`` only replaces PostgreSQL features that RMDB does
not implement (most importantly SELECT without FROM and float casts) with
equivalent table-backed queries; the UNION operator, quantifier, association,
ordering, and expected multiplicity are unchanged.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


POSTGRESQL_COMMIT = "b495c5685a1ac436a0006aba464efcf0edb6153d"
POSTGRESQL_REPOSITORY = "https://github.com/postgres/postgres"

UPSTREAM_SOURCES = {
    "union_sql": {
        "path": "src/test/regress/sql/union.sql",
        "blob_sha": "c8de276c2b5719ee2fd81419d633652246edb90d",
        "lines": "1-676",
    },
    "union_expected": {
        "path": "src/test/regress/expected/union.out",
        "blob_sha": "84abcd6b14f9e17cd19556372ef24ec90527a918",
        "lines": "1-1708",
    },
    "tenk_data": {
        "path": "src/test/regress/data/tenk.data",
        "blob_sha": "c9064c9c0325fe639d3ff2079436b3489eac9f97",
        "lines": "1-10000",
    },
}


def upstream_url(source: str, line: int | None = None) -> str:
    path = UPSTREAM_SOURCES[source]["path"]
    url = f"{POSTGRESQL_REPOSITORY}/blob/{POSTGRESQL_COMMIT}/{path}"
    return f"{url}#L{line}" if line is not None else url


@dataclass(frozen=True)
class UnionCase:
    case_id: str
    line: int
    upstream_sql: str
    rmdb_sql: str
    expected_rows: Sequence[Sequence[object]]
    output_name: str
    output_type: str
    adaptation: str


# pgu_i contains the three rows 1, 2, 3 and pgu_f contains 1.0, 1.1, 2.2.
# WHERE turns each branch into the one-row relation used by the upstream
# literal-only SELECT.  This is necessary because RMDB currently requires FROM.
SIMPLE_UNION_CASES = (
    UnionCase(
        "simple_distinct",
        7,
        "SELECT 1 AS two UNION SELECT 2 ORDER BY 1;",
        "select v as two from pgu_i where v=1 union "
        "select v from pgu_i where v=2 order by 1",
        ((1,), (2,)),
        "two",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "distinct_duplicate",
        9,
        "SELECT 1 AS one UNION SELECT 1 ORDER BY 1;",
        "select v as one from pgu_i where v=1 union "
        "select v from pgu_i where v=1 order by 1",
        ((1,),),
        "one",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "all_distinct_values",
        11,
        "SELECT 1 AS two UNION ALL SELECT 2;",
        "select v as two from pgu_i where v=1 union all "
        "select v from pgu_i where v=2",
        ((1,), (2,)),
        "two",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "all_duplicate",
        13,
        "SELECT 1 AS two UNION ALL SELECT 1;",
        "select v as two from pgu_i where v=1 union all "
        "select v from pgu_i where v=1",
        ((1,), (1,)),
        "two",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "three_distinct_operands",
        15,
        "SELECT 1 AS three UNION SELECT 2 UNION SELECT 3 ORDER BY 1;",
        "select v as three from pgu_i where v=1 union "
        "select v from pgu_i where v=2 union "
        "select v from pgu_i where v=3 order by 1",
        ((1,), (2,), (3,)),
        "three",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "three_operands_duplicate",
        17,
        "SELECT 1 AS two UNION SELECT 2 UNION SELECT 2 ORDER BY 1;",
        "select v as two from pgu_i where v=1 union "
        "select v from pgu_i where v=2 union "
        "select v from pgu_i where v=2 order by 1",
        ((1,), (2,)),
        "two",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "mixed_distinct_then_all",
        19,
        "SELECT 1 AS three UNION SELECT 2 UNION ALL SELECT 2 ORDER BY 1;",
        "select v as three from pgu_i where v=1 union "
        "select v from pgu_i where v=2 union all "
        "select v from pgu_i where v=2 order by 1",
        ((1,), (2,), (2,)),
        "three",
        "int",
        "Replace literal SELECT operands with one-row table-backed operands.",
    ),
    UnionCase(
        "float_distinct_values",
        21,
        "SELECT 1.1 AS two UNION SELECT 2.2 ORDER BY 1;",
        "select v as two from pgu_f where v=1.1 union "
        "select v from pgu_f where v=2.2 order by 1",
        ((1.1,), (2.2,)),
        "two",
        "float",
        "Replace literal SELECT operands with typed one-row table-backed operands.",
    ),
    UnionCase(
        "mixed_numeric_left",
        25,
        "SELECT 1.1 AS two UNION SELECT 2 ORDER BY 1;",
        "select v as two from pgu_f where v=1.1 union "
        "select v from pgu_i where v=2 order by 1",
        ((1.1,), (2.0,)),
        "two",
        "float",
        "Use typed FLOAT and INT source tables instead of literal type inference.",
    ),
    UnionCase(
        "mixed_numeric_right",
        27,
        "SELECT 1 AS two UNION SELECT 2.2 ORDER BY 1;",
        "select v as two from pgu_i where v=1 union "
        "select v from pgu_f where v=2.2 order by 1",
        ((1.0,), (2.2,)),
        "two",
        "float",
        "Use typed INT and FLOAT source tables instead of literal type inference.",
    ),
    UnionCase(
        "mixed_numeric_equal",
        29,
        "SELECT 1 AS one UNION SELECT 1.0::float8 ORDER BY 1;",
        "select v as one from pgu_i where v=1 union "
        "select v from pgu_f where v=1.0 order by 1",
        ((1.0,),),
        "one",
        "float",
        "Use a typed FLOAT source table instead of PostgreSQL's ::float8 cast.",
    ),
    UnionCase(
        "mixed_numeric_all",
        31,
        "SELECT 1.1 AS two UNION ALL SELECT 2 ORDER BY 1;",
        "select v as two from pgu_f where v=1.1 union all "
        "select v from pgu_i where v=2 order by 1",
        ((1.1,), (2.0,)),
        "two",
        "float",
        "Use typed FLOAT and INT source tables instead of literal type inference.",
    ),
    UnionCase(
        "mixed_numeric_all_equal",
        33,
        "SELECT 1.0::float8 AS two UNION ALL SELECT 1 ORDER BY 1;",
        "select v as two from pgu_f where v=1.0 union all "
        "select v from pgu_i where v=1 order by 1",
        ((1.0,), (1.0,)),
        "two",
        "float",
        "Use a typed FLOAT source table instead of PostgreSQL's ::float8 cast.",
    ),
    UnionCase(
        "mixed_numeric_three_distinct",
        35,
        "SELECT 1.1 AS three UNION SELECT 2 UNION SELECT 3 ORDER BY 1;",
        "select v as three from pgu_f where v=1.1 union "
        "select v from pgu_i where v=2 union "
        "select v from pgu_i where v=3 order by 1",
        ((1.1,), (2.0,), (3.0,)),
        "three",
        "float",
        "Use typed FLOAT and INT source tables instead of literal type inference.",
    ),
    UnionCase(
        "mixed_numeric_chain_duplicate",
        37,
        "SELECT 1.1::float8 AS two UNION SELECT 2 UNION SELECT 2.0::float8 ORDER BY 1;",
        "select v as two from pgu_f where v=1.1 union "
        "select v from pgu_i where v=2 union "
        "select v from pgu_f where v=2.0 order by 1",
        ((1.1,), (2.0,)),
        "two",
        "float",
        "Use typed FLOAT tables instead of PostgreSQL's ::float8 casts.",
    ),
    UnionCase(
        "mixed_numeric_distinct_then_all",
        39,
        "SELECT 1.1 AS three UNION SELECT 2 UNION ALL SELECT 2 ORDER BY 1;",
        "select v as three from pgu_f where v=1.1 union "
        "select v from pgu_i where v=2 union all "
        "select v from pgu_i where v=2 order by 1",
        ((1.1,), (2.0,), (2.0,)),
        "three",
        "float",
        "Use typed FLOAT and INT source tables instead of literal type inference.",
    ),
    UnionCase(
        "right_parenthesized_all",
        41,
        "SELECT 1.1 AS two UNION (SELECT 2 UNION ALL SELECT 2) ORDER BY 1;",
        "select v as two from pgu_f where v=1.1 union "
        "(select v from pgu_i where v=2 union all "
        "select v from pgu_i where v=2) order by 1",
        ((1.1,), (2.0,)),
        "two",
        "float",
        "Use table-backed operands while retaining the parenthesized set-op boundary.",
    ),
)


@dataclass(frozen=True)
class AdaptedLargeCase:
    case_id: str
    lines: str
    upstream_sql: str
    adaptation: str


LARGE_UNION_CASES = (
    AdaptedLargeCase(
        "tenk_union_cardinality",
        "127-129,156-158",
        "select count(*) from "
        "(select unique1 from tenk1 union select fivethous from tenk1) ss;",
        "Run the same query on a generated relation of at least tenk scale; "
        "the default wrapper scale is 1,000,000 rows.",
    ),
    AdaptedLargeCase(
        "same_table_distinct_and_all",
        "46-54",
        "SELECT f1 FROM FLOAT8_TBL UNION [ALL] SELECT f1 FROM FLOAT8_TBL;",
        "Use the large generated INT/FLOAT columns and assert both set cardinality "
        "and bag multiplicity.",
    ),
    AdaptedLargeCase(
        "empty_union_inputs",
        "463-488",
        "SELECT two FROM tenk1 WHERE 1=2 UNION SELECT four FROM tenk1 ORDER BY 1;",
        "RMDB requires a column on the left of comparisons, so id<0 supplies the "
        "same provably empty input.",
    ),
    AdaptedLargeCase(
        "derived_union_filter",
        "542-586",
        "SELECT * FROM (SELECT ... UNION SELECT ...) ss WHERE ... ORDER BY ...;",
        "Keep the derived UNION, outer predicate, and ordering while replacing "
        "unsupported literal projections/generate_series with real columns.",
    ),
    AdaptedLargeCase(
        "union_join",
        "672-676",
        "select * from tenk1 t join "
        "(select ten from tenk1 union select ten from onek) s "
        "on s.ten = t.unique1;",
        "Use the generated large and 10,000-row relations and assert the join count.",
    ),
)
