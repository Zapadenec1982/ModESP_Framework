# Output: Commits, Documentation, Workflow

## Git Convention (MANDATORY)

After every completed task:
1. `git add` — changed files
2. `git commit` — conventional commits, Ukrainian body
3. `git push origin main` — ALWAYS push, don't defer

### Commit message format
```
feat(module): short description

- Detail 1
- Detail 2
```

### Prefixes
| Prefix | When |
|---|---|
| `feat(module)` | New feature or capability |
| `fix(module)` | Bug fix |
| `refactor(module)` | Code restructure without behavior change |
| `perf(module)` | Performance improvement |
| `docs` | Documentation only |
| `test` | Tests only |
| `chore` | Build, config, maintenance |

## Documentation Updates (after each session)

| File | What | When to update |
|---|---|---|
| `CLAUDE.md` | How the project works NOW | Architecture/API/structure changes |
| `ACTION_PLAN.md` | What to do next | After each session |
| `docs/CHANGELOG.md` | Full changelog | After each session |
| `docs/06_roadmap.md` | Where we're going | Phase completion or priority change |

### Rule: Documentation = mirror of code
- If feature works — it's documented
- If feature doesn't work — not documented as ready
- Roadmap = FUTURE, CLAUDE.md = PRESENT
- Don't describe in CLAUDE.md what doesn't exist in code

## Build & Deploy

| Action | Command |
|---|---|
| ESP-IDF build | `powershell -ExecutionPolicy Bypass -File run_build.ps1` |
| Flash + monitor | `idf.py -p COM15 flash monitor` |
| WebUI build | `cd webui && npm run build` |
| WebUI deploy | `cd webui && npm run deploy` |
| Run generator | `python tools/generate_ui.py` |
| Run pytest | `cd tools && python -m pytest tests/ -v` |
| Run host tests | Build + run from `tests/host/build/` |

## Response Style

- Comments in code: **Ukrainian**
- Doxygen: **English**
- Commit messages: prefix English, body Ukrainian
- Communication with user: match user's language
