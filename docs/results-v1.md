# Version 1 solve result

Rules: [`rules.md`](rules.md)  
Format: [`file-format.md`](file-format.md)

The first complete solve was run on an Apple M3 Pro with five worker threads
and chunk size 256.

| Result | Value |
| --- | ---: |
| Canonical boundary states | 36,864,000 |
| Backward calculation time | 112.8 seconds |
| Stored initial expected score (`float32`) | 383.305389 |
| Raw XOR-delta float32 table | 141 MiB |
| `zstd -3` table | 94.2 MiB |

The C++ inspector decoded every value and verified the embedded FNV-1a payload
checksum. The SHA-256 of `maxiyatzy-values-v1.mytz.zst` is:

```text
7de205b39ce323c7fb3099ab197cf0190257921286acd524955fca70a2515638
```

The stored initial expected score is rounded to float32. All dynamic-programming
calculations were performed in double precision.
