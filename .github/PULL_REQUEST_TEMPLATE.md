<!-- Thanks for contributing to MakHosFish! Please read CONTRIBUTING.md first. -->

## Summary

What does this change do, and why?

## Type of change

- [ ] Bug fix (rules, perft, protocol, tooling)
- [ ] New feature
- [ ] Strength / evaluation change
- [ ] Performance
- [ ] Documentation / build / CI
- [ ] Refactor (no behaviour change)

## Related issues

Closes #

## Testing

Commands run and their results:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/makhosfish_tests
ctest --test-dir build --output-on-failure
```

- [ ] Unit + perft tests pass
- [ ] ASan/UBSan build passes
- [ ] Thai perft(9) still **114530830**

## Strength evidence (for eval/search changes)

| Matchup | Games | Depth | Result |
|---------|-------|-------|--------|
| new vs old | | | Elo |

## Checklist

- [ ] New behaviour ships with a test
- [ ] Frozen formats (FEN, notation, `.mhpw`, TCI v1) unchanged — or `spec.md` updated too
- [ ] Thai rules untouched (variants are cross-check only)
- [ ] Comments explain *why*, not *what*
- [ ] Contribution is my own work, licensed under GPL-3.0-or-later
