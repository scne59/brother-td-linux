#!/usr/bin/env python3
"""End-to-end tests for the CUPS filter using synthetic CUPS raster input."""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile

FILTER = os.environ.get("BROTHER_TD_FILTER")
RASTER = os.environ.get("BROTHER_TD_RASTER")

if not FILTER or not RASTER:
    print("BROTHER_TD_FILTER and BROTHER_TD_RASTER must be set", file=sys.stderr)
    raise SystemExit(2)


class ParseError(Exception):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def u8(self) -> int:
        if self.offset >= len(self.data):
            raise ParseError("unexpected end of stream")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def take(self, length: int) -> bytes:
        if self.offset + length > len(self.data):
            raise ParseError("unexpected end of stream")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def expect(self, value: bytes) -> None:
        actual = self.take(len(value))
        if actual != value:
            raise ParseError(f"expected {value.hex()} at {self.offset - len(value)}, got {actual.hex()}")


def unpackbits(data: bytes) -> bytes:
    output = bytearray()
    i = 0
    while i < len(data):
        control = data[i]
        i += 1
        if control < 128:
            count = control + 1
            output += data[i:i + count]
            i += count
        elif control > 128:
            count = 257 - control
            if i >= len(data):
                raise ParseError("bad PackBits repeat")
            output += bytes([data[i]]) * count
            i += 1
        # 128 is a no-op
    return bytes(output)


def leading_zeros(data: bytes) -> int:
    count = 0
    while count < len(data) and data[count] == 0:
        count += 1
    return count


def parse_stream(data: bytes, head_bytes: int) -> list[dict]:
    reader = Reader(data)

    zeros = 0
    while reader.offset < len(reader.data) and reader.data[reader.offset] == 0:
        reader.offset += 1
        zeros += 1
    if zeros not in (350, 661):
        raise ParseError(f"unexpected invalidate length {zeros}")
    reader.expect(b"\x1b\x40")

    pages: list[dict] = []
    while reader.offset < len(reader.data):
        if reader.data[reader.offset:reader.offset + 4] == b"\x1b\x69\x61\xff":
            reader.take(4)
            if reader.offset != len(reader.data):
                raise ParseError("trailing data after mode reset")
            break

        reader.expect(b"\x1b\x69\x61\x01")
        reader.expect(b"\x1b\x69\x21\x00")
        reader.expect(b"\x1b\x69\x7a")
        info = reader.take(10)

        reader.expect(b"\x1b\x69\x4d")
        various = reader.u8()

        reader.expect(b"\x1b\x69\x64")
        margin = reader.u8() | (reader.u8() << 8)

        reader.expect(b"\x1b\x69\x4b")
        expanded = reader.u8()

        reader.expect(b"\x4d")
        compression = reader.u8()

        lines: list[bytes] = []
        zero_lines = 0
        while True:
            command = reader.u8()
            if command in (0x0C, 0x1A):
                terminator = command
                break
            if command == 0x5A:
                lines.append(bytes(head_bytes))
                zero_lines += 1
                continue
            if command != 0x67:
                raise ParseError(f"unexpected raster command 0x{command:02x}")
            reader.expect(b"\x00")
            length = reader.u8()
            payload = reader.take(length)
            if compression == 0:
                if length != head_bytes:
                    raise ParseError(f"raw line length {length}, expected {head_bytes}")
                line = payload
            else:
                if length == head_bytes + 1 and payload[0] == head_bytes:
                    line = payload[1:]
                else:
                    line = unpackbits(payload)
                if len(line) != head_bytes:
                    raise ParseError(f"decoded line length {len(line)}, expected {head_bytes}")
            lines.append(line)

        if terminator not in (0x0C, 0x1A):
            raise ParseError(f"unexpected page terminator 0x{terminator:02x}")

        pages.append(
            {
                "info": info,
                "various": various,
                "margin": margin,
                "expanded": expanded,
                "compression": compression,
                "lines": lines,
                "zero_lines": zero_lines,
                "terminator": terminator,
            }
        )

    return pages


def run_filter(raster: bytes, options: str, copies: int = 1, expect: int = 0) -> tuple[bytes, str]:
    command = [FILTER, "42", "test", "integration", str(copies), options]
    result = subprocess.run(command, input=raster, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode != expect:
        raise AssertionError(
            f"filter returned {result.returncode}, expected {expect}\n"
            f"stdout={result.stdout[:80]!r}\nstderr={result.stderr.decode(errors='replace')}"
        )
    return result.stdout, result.stderr.decode(errors="replace")


def make_raster(arguments: list[str]) -> bytes:
    with tempfile.NamedTemporaryFile(prefix="td-raster-", suffix=".ras", delete=False) as handle:
        path = handle.name
    try:
        command = [RASTER, "--output", path] + arguments
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return pathlib.Path(path).read_bytes()
    finally:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass


def check_diecut_203() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square", "--colorspace", "K",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDMediaType=DieCut TDDither=Threshold "
        "TDCompression=None TDMargin=3",
    )
    assert leading_zeros(output) == 350  # explicit TDModel uses 350 NULs
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    page = pages[0]
    info = page["info"]
    assert info[1] == 0x0B, info
    assert info[2] == 102
    assert info[3] == 152
    assert info[8] == 0
    lines = info[4] | (info[5] << 8) | (info[6] << 16) | (info[7] << 24)
    assert lines == 1170, lines
    assert len(page["lines"]) == 1170
    assert page["margin"] == 0
    assert page["terminator"] == 0x1A
    assert any(any(line) for line in page["lines"])
    assert all(len(line) == 104 for line in page["lines"])


def check_76x127_diecut() -> None:
    raster = make_raster([
        "--width", "608", "--height", "1016", "--dpi", "203",
        "--page-width-mm", "76", "--page-length-mm", "127",
        "--pattern", "square",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4210D TDMediaType=DieCut TDCompression=None",
    )
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    info = pages[0]["info"]
    assert info[2] == 76
    assert info[3] == 127
    # 127 mm label minus the documented 6 mm die-cut allowance at 203 dpi.
    assert len(pages[0]["lines"]) == 967
    assert len(pages[0]["lines"][0]) == 104
    assert any(any(line) for line in pages[0]["lines"])


def check_diecut_300() -> None:
    raster = make_raster([
        "--width", "1200", "--height", "1800", "--dpi", "300",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square", "--colorspace", "K",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4510D TDMediaType=DieCut TDCompression=None",
    )
    pages = parse_stream(output, 160)
    assert len(pages) == 1
    info = pages[0]["info"]
    lines = info[4] | (info[5] << 8) | (info[6] << 16) | (info[7] << 24)
    assert lines == 1729, lines
    assert len(pages[0]["lines"]) == lines
    assert all(len(line) == 160 for line in pages[0]["lines"])


def check_copies() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square",
    ])
    output, _ = run_filter(raster, "TDModel=TD-4410D TDCompression=None", copies=3)
    pages = parse_stream(output, 104)
    assert len(pages) == 3
    assert [page["terminator"] for page in pages] == [0x0C, 0x0C, 0x1A]
    assert [page["info"][8] for page in pages] == [0, 1, 1]


def check_multipage() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "stripes", "--pages", "2",
    ])
    output, _ = run_filter(raster, "TDModel=TD-4410D TDCompression=None")
    pages = parse_stream(output, 104)
    assert len(pages) == 2
    assert [page["terminator"] for page in pages] == [0x0C, 0x1A]
    assert [page["info"][8] for page in pages] == [0, 1]


def check_continuous_trim() -> None:
    raster = make_raster([
        "--width", "816", "--height", "800", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "100",
        "--pattern", "square",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDMediaType=Continuous TDAutoTrim=True "
        "TDCompression=None TDMargin=10",
    )
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    page = pages[0]
    assert page["info"][1] == 0x0A
    assert page["info"][3] == 0
    assert page["margin"] == 80  # ceil(10 mm * 203 dpi / 25.4)
    assert 100 < len(page["lines"]) < 800


def check_continuous_trim_inverted() -> None:
    raster = make_raster([
        "--width", "816", "--height", "800", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "100",
        "--pattern", "white",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDMediaType=Continuous TDAutoTrim=True "
        "TDInvert=True TDCompression=None",
    )
    pages = parse_stream(output, 104)
    # A white image becomes solid black when inverted, so nothing is blank and
    # auto-trim must leave the full page intact.
    assert len(pages[0]["lines"]) == 800
    assert all(line[52] == 0xFF for line in pages[0]["lines"])

    raster = make_raster([
        "--width", "788", "--height", "800", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "100",
        "--pattern", "black",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDMediaType=Continuous TDAutoTrim=True "
        "TDInvert=True TDCompression=None",
    )
    pages = parse_stream(output, 104)
    # A black image becomes blank when inverted, so auto-trim reduces it to
    # the 12 mm minimum.
    assert len(pages[0]["lines"]) == 96
    # The side margins are black after inversion; the imageable area is white.
    assert all(line[52] == 0x00 for line in pages[0]["lines"])


def check_compression() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square",
    ])
    output, _ = run_filter(raster, "TDModel=TD-4410D TDCompression=TIFF")
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    assert pages[0]["zero_lines"] > 0
    assert any(any(line) for line in pages[0]["lines"])


def check_blank_lines() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "white",
    ])
    output, _ = run_filter(
        raster, "TDModel=TD-4410D TDDither=Threshold TDCompression=None"
    )
    pages = parse_stream(output, 104)
    assert pages[0]["zero_lines"] == 1170
    assert all(not any(line) for line in pages[0]["lines"])


def check_invert_border() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "white",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDDither=Threshold TDCompression=None "
        "TDInvert=True TDBorder=True",
    )
    pages = parse_stream(output, 104)
    line = pages[0]["lines"][0]
    # White image + black border, inverted: border becomes white and the
    # imageable area becomes black.
    assert line[0] == 0x00
    assert line[52] == 0xFF


def check_rotation() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "stripes",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDRotate=90 TDScaling=Crop TDCompression=None",
    )
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    assert len(pages[0]["lines"]) == 1170


def check_auto_cut() -> None:
    raster = make_raster([
        "--width", "816", "--height", "800", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "100",
        "--pattern", "square",
    ])
    output, _ = run_filter(
        raster,
        "TDModel=TD-4410D TDMediaType=Continuous TDAutoCut=True "
        "TDCutAtEnd=False TDPeeler=True TDCompression=None",
    )
    pages = parse_stream(output, 104)
    assert pages[0]["various"] == 0x50
    assert pages[0]["expanded"] == 0x00


def check_auto_model() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square",
    ])
    output, _ = run_filter(raster, "TDCompression=None")
    assert leading_zeros(output) == 661
    pages = parse_stream(output, 104)
    assert len(pages) == 1

    raster = make_raster([
        "--width", "1200", "--height", "1800", "--dpi", "300",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square",
    ])
    output, _ = run_filter(raster, "TDCompression=None")
    assert leading_zeros(output) == 661
    pages = parse_stream(output, 160)
    assert len(pages) == 1


def check_custom_size() -> None:
    raster = make_raster([
        "--width", "800", "--height", "400", "--dpi", "203",
        "--page-width-mm", "100", "--page-length-mm", "50",
        "--pattern", "square",
    ])
    output, _ = run_filter(raster, "TDModel=TD-4410D TDCompression=None")
    pages = parse_stream(output, 104)
    assert pages[0]["info"][2] == 100
    assert pages[0]["info"][3] == 50


def check_bilevel() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square", "--colorspace", "K", "--bits", "1",
    ])
    output, _ = run_filter(
        raster, "TDModel=TD-4410D TDDither=Threshold TDCompression=None"
    )
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    assert len(pages[0]["lines"]) == 1170
    assert any(any(line) for line in pages[0]["lines"])


def check_16bit() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "square", "--colorspace", "K", "--bits", "16",
    ])
    output, _ = run_filter(
        raster, "TDModel=TD-4410D TDDither=Threshold TDCompression=None"
    )
    pages = parse_stream(output, 104)
    assert len(pages) == 1
    assert any(any(line) for line in pages[0]["lines"])


def check_rgb() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--pattern", "gradient", "--colorspace", "RGB",
    ])
    output, _ = run_filter(raster, "TDModel=TD-4410D TDDither=Floyd")
    pages = parse_stream(output, 104)
    assert len(pages[0]["lines"]) == 1170


def check_errors() -> None:
    raster = make_raster([
        "--width", "816", "--height", "1216", "--dpi", "203",
        "--page-width-mm", "102", "--page-length-mm", "152.4",
        "--colorspace", "LAB",
    ])
    _, stderr = run_filter(raster, "TDModel=TD-4410D", expect=1)
    assert "colorspace" in stderr

    _, stderr = run_filter(b"this is not a CUPS raster", "TDModel=TD-4410D", expect=1)
    assert "raster" in stderr.lower()


def main() -> int:
    checks = [
        check_diecut_203,
        check_76x127_diecut,
        check_diecut_300,
        check_copies,
        check_multipage,
        check_continuous_trim,
        check_continuous_trim_inverted,
        check_compression,
        check_blank_lines,
        check_invert_border,
        check_rotation,
        check_auto_cut,
        check_auto_model,
        check_custom_size,
        check_bilevel,
        check_16bit,
        check_rgb,
        check_errors,
    ]
    for check in checks:
        check()
        print(f"{check.__name__}: OK")
    print("integration: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
