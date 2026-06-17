# AGENTS.md

Preferred workspace root:

```txt
C:\Users\mrmoe\Documents
```

Secrets:

```txt
C:\Users\mrmoe\Documents\_secrets
```

Never commit or expose secrets.

## Project Direction

hash-forge is a tiny native C project for discovering fast 64-bit hash
functions through an in-memory evolutionary VM.

Keep the codebase small. Prefer direct C, simple files, deterministic behavior,
and fast RAM-resident loops over framework-heavy structure.

## Coding Defaults

- Use C11.
- Target native Windows x64.
- Prefer MSYS2 UCRT64 GCC.
- Keep the hot loop free of file I/O and heap churn.
- Add comments only where they clarify non-obvious logic.
- Do not copy the old `hash64` implementation; use it only as inspiration.
- Do not touch `C:\Users\mrmoe\Documents\hash64`.
