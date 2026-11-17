# Brother TD-4D raster protocol notes

This document records the subset of Brother's raster command language used by
the filter, and the reasoning behind the encoding. The authoritative source is
Brother's *Software Developer's Manual — Raster Command Reference* for the
TD-4D series:

- <https://download.brother.com/welcome/docp100429/cv_td4000d_eng_raster_103.pdf>

The original utility this project grew from is
[`scne59/brother-td`](https://github.com/scne59/brother-td).

## Job structure

A job consists of:

1. An invalidate sequence (350 or 661 NUL bytes, depending on the model
   generation), followed by `ESC @`.
2. For each page (and each copy of each page):
   - `ESC i a 01` — switch to raster mode
   - `ESC i ! 00` — disable automatic status notification
   - `ESC i z` + 10 bytes — print information
   - `ESC i M` + 1 byte — various mode (peeler/auto-cut)
   - `ESC i d` + 2 bytes — feed margin in dots
   - `ESC i K` + 1 byte — expanded mode (cut at end)
   - `M` + 1 byte — compression mode
   - raster rows, each either `g 00 n <data>` or `Z`
   - `FF` (`0x0c`) for every page except the last, `Ctrl-Z` (`0x1a`) for the
     last page
3. `ESC i a ff` — return the printer to its default command mode.

NUL is a no-op/skipped command, so the invalidate length is not data. The
older TD-4D generation uses 350 NULs; the 2024 generation and linerless
variants use 661. If no explicit `TDModel` option is supplied, the filter uses
the 661-byte form for both generations. Extra NULs are skipped, so this is
safe and ensures newer models are invalidated correctly.

## Print information: `ESC i z`

```text
1b 69 7a n1 n2 n3 n4 n5 n6 n7 n8 n9 n10
```

| Field | Meaning | Value used here |
|---|---|---|
| `n1` | Valid flags | `0x8e` = printer recovery + media type + width + length |
| `n2` | Media type | `0x0b` die-cut, `0x0a` continuous |
| `n3` | Media width in mm | rounded page width |
| `n4` | Media length in mm | die-cut: rounded page length; continuous: `0` |
| `n5..n8` | Number of raster rows | little-endian 32-bit |
| `n9` | Starting page | `0` for the first page/copy, `1` otherwise |
| `n10` | Fixed | `0` |

For continuous media the physical length is determined by the number of rows
sent plus the feed margin, so `n4` is zero. For die-cut media `n4` is the
label pitch and `n5` is the printable row count (label length minus the
documented 6 mm end allowance).

## Feed margin: `ESC i d`

```text
1b 69 64 n1 n2       margin = n1 + 256*n2 dots
```

- Die-cut labels: always `0`, as required by Brother's documentation.
- Continuous media: 3 mm minimum, 127 mm maximum. The filter computes
  `ceil(mm * dpi / 25.4)`.

## Various mode and expanded mode

```text
1b 69 4d n     bit 4 = peeler, bit 6 = auto-cut
1b 69 4b n     bit 3 = cut at end (1 = cut, 0 = do not cut)
```

Auto-cut and peeler are disabled for die-cut media. `TDCutAtEnd` maps to bit 3
of `ESC i K`.

## Raster rows

The print head has 832 pins at 203 dpi (104 bytes per row) and 1280 pins at
300 dpi (160 bytes per row). Each row is sent with:

```text
67 00 n d1 ... dn
```

where `n` is the number of bytes of raster data. With no compression,
`n` is 104 or 160.

The first byte's most significant bit corresponds to the right-hand end of
the physical print head, and the last byte's least significant bit to the
left-hand end. A conventional left-to-right image therefore has to be emitted
right-to-left and packed MSB-first. The filter does this directly when it
builds each line:

```text
output bit k  ->  head pin (head_pins - 1 - k)
```

The imageable area is centered on the head. For example 102 mm media at
203 dpi has 788 imageable pins, leaving 22 pins on each side. If
`TDBorder=True`, the side-margin pins are printed black; otherwise they are
white. `TDInvert` flips the complete row, including margins.

`Z` (`0x5a`) is the zero-raster command and is used for rows that are entirely
zero after margins and inversion.

## TIFF/PackBits compression

`M 02` selects TIFF/PackBits compression. Each row is compressed
independently over the full 104/160-byte row. The PackBits rules are:

- A control byte `0..127` means `control + 1` literal bytes follow.
- A control byte `-1..-127` (`0xff..0x81`) means the next byte is repeated
  `1 - control` times.
- `0x80` is a no-op.

The filter never emits a compressed row longer than the uncompressed row. If
compression does not help, it sends the documented uncompressed fallback:
a one-byte length (`104` or `160`) followed by the raw row, with `n =
length + 1`.

## Media geometry

Brother documents the print-area pin counts for common media widths. The
driver uses these values when available:

| Media width | 203 dpi print pins | 300 dpi print pins |
|---:|---:|---:|
| 39 mm | 288 | 425 |
| 51 mm | 382 | 564 |
| 58 mm | 440 | 649 |
| 76 mm | 583 | 861 |
| 80 mm | 615 | 909 |
| 90 mm | 695 | 1027 |
| 102 mm | 788 | 1164 |
| 106 mm | 823 | 1216 |

Other widths use a conservative 3 mm total edge allowance. The driver warns
and clamps when the requested width exceeds the model's maximum or falls
below 19 mm.

## Differences from `brother-td.pl`

The original Perl utility is a good starting point, but its hard-coded
assumptions break on 300 dpi models and with non-standard media. This driver
corrects the following:

| Original | This driver |
|---|---|
| Always writes `g 00 68` (104-byte row) | Writes 104 bytes at 203 dpi and 160 bytes (`0xa0`) at 300 dpi |
| Subtracts 48 dots from the label length at every resolution | Uses the documented 6 mm die-cut allowance, converted at the actual dpi |
| Sizes content to the full media width | Uses the documented print-area width, centered on the head |
| Sends a 3 mm feed margin for die-cut labels | Sends zero, as required for die-cut media |
| Sends `FF` after every copy and `Ctrl-Z` at the end | Sends `FF` only between pages; `Ctrl-Z` only on the last page |
| Does not reset the command mode | Sends `ESC i a ff` at the end of the job |
| No compression | Optional TIFF/PackBits with the documented fallback |
| No auto-cut/peeler | `ESC i M` and `ESC i K` control |
| Image is only ever shrunk | Fit, fill/crop, actual-size, rotation, alignment, mirror, invert |
| Fixed 350-byte invalidate | Per-model 350/661-byte invalidate; 661-byte fallback when no `TDModel` is supplied |
| Relies on ImageMagick/rsvg for PDF/SVG | Uses the normal CUPS filter chain (`gstoraster`, etc.) |

## Verification

`make check` runs:

- PackBits vectors from Brother's manual and round-trip tests,
- geometry tests against the pin-count tables,
- image rotation/scaling/trim tests,
- a byte-level parser for the generated command stream,
- synthetic CUPS raster jobs for 203/300 dpi, copies, multi-page,
  continuous trimming, compression, inversion, rotation, and custom sizes,
- `cupstestppd` over every generated PPD.

The tests do not need a physical printer. A real job can be captured with
`BROTHER_TD_DEBUG_DIR` and compared against the same parser.
