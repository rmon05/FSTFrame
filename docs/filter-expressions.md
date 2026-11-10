# Filter expressions

## API

Keep the current C++ `DataFrame::filter(const Predicate&)` and Python
`df.filter(column, op, value)` calls. Add a C++ `FilterExpr` value type with a
predicate leaf, `FilterExpr::all(...)` for AND, and `FilterExpr::any(...)` for
OR. Add `DataFrame::filter(const FilterExpr&)`. A `Predicate` converts to a
leaf.

```cpp
auto result = df.filter(FilterExpr::all({
    Predicate{"points", CompareOp::GreaterEqual, std::int64_t{25}},
    FilterExpr::any({
        Predicate{"team", CompareOp::Equal, std::string{"A"}},
        Predicate{"minutes", CompareOp::Less, std::int64_t{80}}
    })
})).execute();
```

In Python, add `ff.col(name)`. Comparisons create predicate leaves. `&`
creates AND and `|` creates OR. Parenthesize each comparison and each group.
Python `and` and `or` cannot combine these expressions; converting an
expression to `bool` raises `TypeError`.

```python
result = df.filter(
    (ff.col("points") >= 25)
    & ((ff.col("team") == "A") | (ff.col("minutes") < 80))
).execute()
```

## Rules

- Explicit groups determine evaluation order. Do not define a separate AND/OR
  precedence rule.
- Evaluate children left to right. Stop an AND group at the first false child
  and an OR group at the first true child.
- Validate every predicate when `filter` is called, including predicates in
  branches that could be skipped during evaluation. Keep the current checks
  for missing columns and incompatible types.
- A null cell makes its comparison false for every operator, including `!=`.
  A row that matches multiple OR branches appears once.
- Reject empty groups. A group with one child has the result of that child.
- Consecutive filters remain AND. `df.filter(A).filter(B | C)` means
  `A AND (B OR C)`. Keep filter operations in their queued order relative to
  `select`, `limit`, and `aggregate`.
- Keep the current six comparison operators and mixed numeric comparison
  behavior. Negation is outside this feature.

## Implementation checks

Test nested AND/OR groups, short circuit behavior, nulls, duplicate OR
matches, early validation, and combinations with `select`, `limit`, and
`aggregate`. Check the C++ and Python interfaces against the same cases.
