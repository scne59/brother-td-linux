#!/usr/bin/env python3
"""Exercise the real CUPS raster chain: gstoraster -> brother-td-filter.

This is skipped automatically when gstoraster is not installed.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILTER = os.environ.get("BROTHER_TD_FILTER", str(ROOT / "build" / "brother-td-filter"))
GSTORASTER = shutil.which("gstoraster")
if GSTORASTER is None:
    for candidate in (
        "/usr/lib/cups/filter/gstoraster",
        "/usr/libexec/cups/filter/gstoraster",
    ):
        if os.access(candidate, os.X_OK):
            GSTORASTER = candidate
            break

if GSTORASTER is None:
    print("real-raster: gstoraster not found; skipping")
    raise SystemExit(0)

if "CUPS_DATADIR" not in os.environ:
    try:
        os.environ["CUPS_DATADIR"] = subprocess.check_output(
            ["cups-config", "--datadir"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        os.environ["CUPS_DATADIR"] = "/usr/share/cups"

# integration.py expects these to be present when imported.
os.environ.setdefault("BROTHER_TD_FILTER", FILTER)
os.environ.setdefault("BROTHER_TD_RASTER", str(ROOT / "build" / "make_raster"))
sys.path.insert(0, str(ROOT / "tests"))
import integration  # noqa: E402

POSTSCRIPT = b"""%!PS-Adobe-3.0
/Helvetica findfont 28 scalefont setfont
72 300 moveto (Brother TD CUPS driver) show
0 setgray
36 36 120 120 rectfill
showpage
"""


def rasterize(ppd: pathlib.Path, resolution: str, page_size: str,
              source: pathlib.Path) -> bytes:
    environment = dict(os.environ)
    environment["PPD"] = str(ppd)
    environment["CUPS_DATADIR"] = os.environ.get("CUPS_DATADIR", "/usr/share/cups")
    command = [
        GSTORASTER,
        "1",
        "test",
        "real-raster",
        "1",
        f"PageSize={page_size} Resolution={resolution}",
        str(source),
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=environment, check=False)
    if result.returncode != 0:
        raise AssertionError(
            f"gstoraster failed with {result.returncode}: "
            f"{result.stderr.decode(errors='replace')[-1000:]}"
        )
    return result.stdout


def run_case(ppd_name: str, resolution: str, head_bytes: int, expected_lines: int,
             page_size: str = "102x152mm") -> None:
    ppd = ROOT / "ppd" / ppd_name
    if not ppd.exists():
        raise AssertionError(f"missing PPD {ppd}")

    with tempfile.TemporaryDirectory(prefix="td-real-") as tmp:
        source = pathlib.Path(tmp) / "input.ps"
        source.write_bytes(POSTSCRIPT)
        raster = rasterize(ppd, resolution, page_size, source)

    model = "TD-4410D" if head_bytes == 104 else "TD-4510D"
    output, stderr = integration.run_filter(
        raster,
        f"TDModel={model} TDMediaType=DieCut TDCompression=None",
    )
    pages = integration.parse_stream(output, head_bytes)
    if len(pages) != 1:
        raise AssertionError(f"expected one page, got {len(pages)}: {stderr}")
    actual_lines = len(pages[0]["lines"])
    if abs(actual_lines - expected_lines) > 5:
        raise AssertionError(
            f"expected about {expected_lines} rows, got {actual_lines}"
        )
    if not any(any(line) for line in pages[0]["lines"]):
        raise AssertionError("rendered label is blank")
    print(f"real-raster {ppd_name} @ {resolution}: OK")


def main() -> int:
    run_case("brother-td-4410d.ppd", "203dpi", 104, 1170)
    run_case("brother-td-4210d.ppd", "203dpi", 104, 967, page_size="76x127mm")
    run_case("brother-td-4510d.ppd", "300dpi", 160, 1729)
    print("real-raster: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
