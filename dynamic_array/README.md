# dynamic_array

Tiny C11 dynamic array for fixed-size elements.

- Type-erased storage: initialize once with `element_size`.
- `reserve` lets hot loops avoid heap churn.
- `push_uninit` returns a writable slot without an extra temporary copy.
- `resize` grows with zero-filled new elements.
- `data` and `get` expose direct contiguous storage.

Build and test:

```powershell
.\build.ps1
.\build\dynamic_array_tests.exe
```

