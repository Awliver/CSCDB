# PostgreSQL regression provenance

The local SQL compatibility gates select tests from the official
[`postgres/postgres`](https://github.com/postgres/postgres) regression suite.
The current pinned upstream revision is
`aaf67e4825f32d764296fabfa3a86c4da90cec16`, recorded in `UPSTREAM_COMMIT`,
and PostgreSQL's license is retained in `COPYRIGHT.postgresql`.

The pre-existing large-data UNION wrapper remains pinned to its original
revision, `b495c5685a1ac436a0006aba464efcf0edb6153d`, recorded separately in
`UNION_UPSTREAM_COMMIT`, and uses:

- [`src/test/regress/sql/union.sql`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/sql/union.sql), blob
  `c8de276c2b5719ee2fd81419d633652246edb90d`;
- [`src/test/regress/expected/union.out`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/expected/union.out), blob
  `84abcd6b14f9e17cd19556372ef24ec90527a918`;
- [`src/test/regress/data/tenk.data`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/data/tenk.data), blob
  `c9064c9c0325fe639d3ff2079436b3489eac9f97`.

Exact selected statements, source line numbers, and adaptations are declared
in `tests/local/postgresql_union_cases.py`. Unsupported PostgreSQL features are
not counted as passing tests. In particular, this large-data UNION wrapper excludes
`INTERSECT`, `EXCEPT`, CTEs, arrays, row types, inheritance, `VALUES` query
operands, and PostgreSQL-specific casts because RMDB does not implement those
features within that wrapper's compatibility boundary.

The extended-syntax wrapper is pinned to the current revision and uses these
official SQL/expected pairs (SQL blob, expected blob):

- [`select_distinct.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/select_distinct.sql) /
  [`select_distinct.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/select_distinct.out):
  `2ed1616b098f7eb57bb74d0f1b57e7fc91ead63e`,
  `741f72387423e748ea0ebdd6c8cf4617917de173`;
- [`join.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/join.sql) /
  [`join.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/join.out):
  `450bd5bbf2c417fa0f7ed3c8d2ebc5e1dc785925`,
  `05f359d3aa79bb00f0713f72bd5c63775596da4a`;
- [`limit.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/limit.sql) /
  [`limit.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/limit.out):
  `603910fe6d11c10350a0f6e148a1f50b52504268`,
  `e3bcc680653b35a3491dceb6fb6413a51268b98a`;
- [`aggregates.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/aggregates.sql) /
  [`aggregates.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/aggregates.out):
  `b788152f0c24d596188c12f686587327de1fac34`,
  `5f0668382ed863b964d23b5c991184b5450daaea`;
- [`union.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/union.sql) /
  [`union.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/union.out):
  `c8de276c2b5719ee2fd81419d633652246edb90d`,
  `84abcd6b14f9e17cd19556372ef24ec90527a918`;
- [`groupingsets.sql`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/sql/groupingsets.sql) /
  [`groupingsets.out`](https://github.com/postgres/postgres/blob/aaf67e4825f32d764296fabfa3a86c4da90cec16/src/test/regress/expected/groupingsets.out):
  `c5d4ed2eb574e40216a3666df016bdaa38683545`,
  `c3f00771f6e28b7b898879683cc1c1c596e1e28f`.

Exact upstream excerpts, source line ranges, adapted RMDB SQL, and adaptation
reasons are declared in `tests/local/postgresql_extended_syntax_cases.py`.
`tests/local/postgresql_extended_syntax_gate.py` asserts 31 deterministic
results covering `SELECT DISTINCT`, `JOIN ... USING`, `LIMIT/OFFSET`, distinct
aggregates, `INTERSECT`/`EXCEPT`, and `IS [NOT] NULL`. PostgreSQL's catalog
fixtures, casts, `VALUES` query operands, and base-table NULL literals are
replaced with small table-backed fixtures without dropping the semantic
assertion. The multi-key `USING` case comes from `groupingsets.sql`; the
multi-argument `COUNT(DISTINCT ...)` spelling has no direct same-shape case in
the pinned regression SQL and is explicitly labelled an RMDB extension rather
than attributed to PostgreSQL.

The canonical upstream `tenk` relation has 10,000 rows. The local UNION gate
generates the same cardinality patterns at a default scale of 1,000,000 rows,
plus a 10,000-row secondary relation, so duplicate elimination and bag
multiplicity are exercised beyond small fixtures without vendoring a large
generated data file.

`tests/local/postgresql_union_differential.py` additionally generates bounded,
reproducible random UNION query expressions and evaluates each one against a
real PostgreSQL server. It uses this pinned regression grammar as its
compatibility boundary, but does not claim that generated statements are
verbatim upstream cases. Each query is checked before and after an index is
created in both engines.

`tests/local/extended_syntax_index_gate.py` loads a 1,000,000-row primary
relation by default and verifies independent result oracles, result metadata,
and `SeqScan` to `IndexScan` plan changes for all requested syntax families.
`tests/local/postgresql_extended_syntax_differential.py` uses the same default
scale and runs reproducible random SQL against RMDB and a real PostgreSQL
instance, both before and after indexes are created. Its default matrix is
three seeds times eight rounds times eight SQL families (192 SQL statements,
768 engine executions); failures include a JSON artifact and an exact replay
command. Both gates are part of `tests/syntax_functional_test.py`, and the
ASan/UBSan wrapper keeps the extended stress relation at 1,000,000 rows by
default.
