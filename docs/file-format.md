# MYTZDP1 lookup format

All integers use the producing machine's little-endian representation. Version
1 files are intended for little-endian systems, including Apple Silicon and
current Node.js platforms.

## Header

| Field | Type | Meaning |
| --- | --- | --- |
| Magic | 8 bytes | `MYTZDP1\0` |
| Version | `uint32` | `1` |
| Rules flags | `uint32` | Bit 0: voluntary crossing allowed |
| Mask count | `uint32` | `2^20` |
| Upper slots | `uint32` | `77` |
| Encoding | `uint32` | `2`: per-mask XOR-delta float32 |
| Value count | `uint64` | `36,864,000` |
| Payload checksum | `uint64` | FNV-1a 64-bit over encoded value bytes |

The fixed fields are followed by 64 upper-mask records. Each record contains a
one-byte state count and 77 one-byte canonical upper states. Unused entries are
`255`. This metadata determines the number and order of values for every full
20-bit scorecard mask.

## Values

Masks occur in ascending numeric order. Within a mask, values follow the
canonical upper-state order recorded for `mask & 63`.

Each optimal expected future value is first rounded from the solver's `double`
to IEEE-754 `float32`. Let `bits` be its raw 32-bit representation. The stored
word is:

```text
encoded = bits XOR previous_bits
```

where `previous_bits` starts at zero for each scorecard mask. Decoding performs
the same XOR and updates `previous_bits`. This transform is exactly reversible
and improves general-purpose compression without adding error beyond the single
documented `double` to `float32` conversion.

The whole `.mytz` file may be compressed with Zstandard. On the first computed
table, `zstd -3` reduced 141 MiB to about 94 MiB.

