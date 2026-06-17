# hash_table

Small C11 `uint64_t -> uint64_t` hash table for hash-forge internals.

- Open-addressed with linear probing.
- Power-of-two capacity and a 70% grow/rehash threshold.
- Supports zero keys, `UINT64_MAX`, updates, removal, clear, and reuse after tombstones.
- No file I/O in table operations.

Builds with the project script:

```powershell
.\build.ps1
.\build\hash_table_tests.exe
```

