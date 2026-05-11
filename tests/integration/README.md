# Integration Tests

Direct request-dispatch coverage currently lives in `tests/unit/http_tests.c` because the default sandbox used for local automation may deny Unix-socket `bind(2)`.

Keep this folder reserved for:

- full Unix-socket HTTP smoke tests
- crash-recovery verification
- multi-process lifecycle scenarios
