"""PostgreSQL predicate regression cases adapted by predicate_keywords_gate.

This is an offline, pinned manifest rather than a runtime network dependency.
The source SQL was pulled from the official postgres/postgres GitHub mirror at
POSTGRESQL_COMMIT.  RMDB's wrapper turns literal-only SELECTs into table-backed
queries and maps PostgreSQL date BETWEEN cases to INT while preserving the
predicate semantics under test.
"""

from __future__ import annotations

from dataclasses import dataclass


POSTGRESQL_COMMIT = "b495c5685a1ac436a0006aba464efcf0edb6153d"
POSTGRESQL_REPOSITORY = "https://github.com/postgres/postgres"

UPSTREAM_SOURCES = {
    "like": {
        "path": "src/test/regress/sql/strings.sql",
        "blob_sha": "38946e8954df2a8490526e8aad8b5d9353b52a10",
        "lines": "418-534",
    },
    "between": {
        "path": "src/test/regress/sql/horology.sql",
        "blob_sha": "8978249a5dc1edabee673f23a3bcc349421172da",
        "lines": "384-409",
    },
    "in_list": {
        "path": "src/test/regress/sql/expressions.sql",
        "blob_sha": "3b3048f973137cfcf2be0bddabee02cf2e7e4f19",
        "lines": "118-133",
    },
    "subquery": {
        "path": "src/test/regress/sql/subselect.sql",
        "blob_sha": "0b18e0132aadbabecc73a2570fbd2e49f6f1f1c2",
        "lines": "5-9, 47-83, 333-341",
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
class LikeCase:
    line: int
    upstream_sql: str
    value: str
    pattern: str
    expected: bool


# strings.sql asks that every LIKE case be paired with NOT LIKE.  The local
# wrapper does that automatically for every entry below.  PostgreSQL-specific
# ::name/::bytea casts, ILIKE and ESCAPE are outside the RMDB grammar and are
# intentionally not included.
LIKE_CASES = (
    LikeCase(424, "SELECT 'hawkeye' LIKE 'h%' AS \"true\";", "hawkeye", "h%", True),
    LikeCase(427, "SELECT 'hawkeye' LIKE 'H%' AS \"false\";", "hawkeye", "H%", False),
    LikeCase(430, "SELECT 'hawkeye' LIKE 'indio%' AS \"false\";", "hawkeye", "indio%", False),
    LikeCase(433, "SELECT 'hawkeye' LIKE 'h%eye' AS \"true\";", "hawkeye", "h%eye", True),
    LikeCase(436, "SELECT 'indio' LIKE '_ndio' AS \"true\";", "indio", "_ndio", True),
    LikeCase(439, "SELECT 'indio' LIKE 'in__o' AS \"true\";", "indio", "in__o", True),
    LikeCase(442, "SELECT 'indio' LIKE 'in_o' AS \"false\";", "indio", "in_o", False),
    LikeCase(445, "SELECT 'abc'::name LIKE '_b_' AS \"true\";", "abc", "_b_", True),
    LikeCase(528, "SELECT 'foo' LIKE '_%' as t, 'f' LIKE '_%' as t, '' LIKE '_%' as f;", "foo", "_%", True),
    LikeCase(528, "SELECT 'foo' LIKE '_%' as t, 'f' LIKE '_%' as t, '' LIKE '_%' as f;", "f", "_%", True),
    LikeCase(528, "SELECT 'foo' LIKE '_%' as t, 'f' LIKE '_%' as t, '' LIKE '_%' as f;", "", "_%", False),
    LikeCase(529, "SELECT 'foo' LIKE '%_' as t, 'f' LIKE '%_' as t, '' LIKE '%_' as f;", "foo", "%_", True),
    LikeCase(529, "SELECT 'foo' LIKE '%_' as t, 'f' LIKE '%_' as t, '' LIKE '%_' as f;", "f", "%_", True),
    LikeCase(529, "SELECT 'foo' LIKE '%_' as t, 'f' LIKE '%_' as t, '' LIKE '%_' as f;", "", "%_", False),
    LikeCase(531, "SELECT 'foo' LIKE '__%' as t, 'foo' LIKE '___%' as t, 'foo' LIKE '____%' as f;", "foo", "__%", True),
    LikeCase(531, "SELECT 'foo' LIKE '__%' as t, 'foo' LIKE '___%' as t, 'foo' LIKE '____%' as f;", "foo", "___%", True),
    LikeCase(531, "SELECT 'foo' LIKE '__%' as t, 'foo' LIKE '___%' as t, 'foo' LIKE '____%' as f;", "foo", "____%", False),
    LikeCase(532, "SELECT 'foo' LIKE '%__' as t, 'foo' LIKE '%___' as t, 'foo' LIKE '%____' as f;", "foo", "%__", True),
    LikeCase(532, "SELECT 'foo' LIKE '%__' as t, 'foo' LIKE '%___' as t, 'foo' LIKE '%____' as f;", "foo", "%___", True),
    LikeCase(532, "SELECT 'foo' LIKE '%__' as t, 'foo' LIKE '%___' as t, 'foo' LIKE '%____' as f;", "foo", "%____", False),
    LikeCase(534, "SELECT 'jack' LIKE '%____%' AS t;", "jack", "%____%", True),
)


@dataclass(frozen=True)
class AdaptedCase:
    keyword: str
    source: str
    lines: str
    upstream_sql: str
    adaptation: str


ADAPTED_CASES = (
    AdaptedCase(
        "BETWEEN",
        "between",
        "388-397",
        "select count(*) from date_tbl where f1 [not] between '1997-01-01' and '1998-01-01';",
        "Use deterministic INT values; retain inclusive endpoints and the NOT complement.",
    ),
    AdaptedCase(
        "IN",
        "in_list",
        "118-133",
        "select return_int_input(1) [not] in (10, 9, 2, 8, 3, 7, 4, 6, 5, 1);",
        "Use an indexed INT column because RMDB does not expose PostgreSQL test helper functions.",
    ),
    AdaptedCase(
        "IN",
        "subquery",
        "47-83",
        "SELECT f1 FROM SUBSELECT_TBL WHERE f1 IN (SELECT f2 FROM SUBSELECT_TBL);",
        "Run both correlated and uncorrelated forms against generated tables.",
    ),
    AdaptedCase(
        "EXISTS",
        "subquery",
        "333-341",
        "select ... from tenk1 a where [not] exists (select 1 from tenk1 b where ...);",
        "Keep EXISTS/NOT EXISTS and correlation while selecting a real column instead of literal 1.",
    ),
)
