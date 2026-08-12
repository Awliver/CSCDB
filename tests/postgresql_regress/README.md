# PostgreSQL regression provenance

The local SQL compatibility gates select tests from the official
[`postgres/postgres`](https://github.com/postgres/postgres) regression suite.
The pinned upstream revision is recorded in `UPSTREAM_COMMIT`, and PostgreSQL's
license is retained in `COPYRIGHT.postgresql`.

The UNION wrapper uses:

- [`src/test/regress/sql/union.sql`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/sql/union.sql), blob
  `c8de276c2b5719ee2fd81419d633652246edb90d`;
- [`src/test/regress/expected/union.out`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/expected/union.out), blob
  `84abcd6b14f9e17cd19556372ef24ec90527a918`;
- [`src/test/regress/data/tenk.data`](https://github.com/postgres/postgres/blob/b495c5685a1ac436a0006aba464efcf0edb6153d/src/test/regress/data/tenk.data), blob
  `c9064c9c0325fe639d3ff2079436b3489eac9f97`.

Exact selected statements, source line numbers, and adaptations are declared
in `tests/local/postgresql_union_cases.py`. Unsupported PostgreSQL features are
not counted as passing tests. In particular, the wrapper excludes
`INTERSECT`, `EXCEPT`, CTEs, arrays, row types, inheritance, `VALUES` query
operands, and PostgreSQL-specific casts because RMDB does not implement those
grammar or type-system features.

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
