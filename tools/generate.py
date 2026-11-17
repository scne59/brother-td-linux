#!/usr/bin/env python3
"""Generate the C model table and one PPD per supported printer.

The single source of truth is models.json.  Run this script after changing it:

    ./tools/generate.py

Generated files are committed so that the driver can be built without Python.
"""

from __future__ import annotations

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MODELS = ROOT / "models.json"
PPD_DIR = ROOT / "ppd"
C_TABLE = ROOT / "src" / "td_models.c"

VERSION = "1.0.0"

# Page sizes offered in the PPD.  Widths larger than the model's maximum are
# omitted automatically.  Height is the physical label length for die-cut
# labels and the nominal page length for continuous media.
PAGE_SIZES = [
    (102, 152, "102 x 152 mm (4 x 6 in)"),
    (102, 102, "102 x 102 mm (4 x 4 in)"),
    (102, 100, "102 x 100 mm"),
    (102, 76, "102 x 76 mm (4 x 3 in)"),
    (102, 50, "102 x 50 mm (4 x 2 in)"),
    (102, 38, "102 x 38 mm (4 x 1.5 in)"),
    (102, 25, "102 x 25 mm (4 x 1 in)"),
    (102, 127, "102 x 127 mm (4 x 5 in)"),
    (102, 114, "102 x 114 mm (4 x 4.5 in)"),
    (106, 152, "106 x 152 mm"),
    (106, 100, "106 x 100 mm"),
    (90, 100, "90 x 100 mm"),
    (80, 100, "80 x 100 mm"),
    (76, 127, "76 x 127 mm (3 x 5 in)"),
    (76, 100, "76 x 100 mm"),
    (76, 76, "76 x 76 mm (3 x 3 in)"),
    (76, 64, "76 x 64 mm (3 x 2.5 in)"),
    (76, 51, "76 x 51 mm (3 x 2 in)"),
    (76, 38, "76 x 38 mm (3 x 1.5 in)"),
    (76, 26, "76 x 26 mm (3 x 1 in)"),
    (58, 100, "58 x 100 mm"),
    (51, 76, "51 x 76 mm (2 x 3 in)"),
    (51, 51, "51 x 51 mm (2 x 2 in)"),
    (51, 38, "51 x 38 mm (2 x 1.5 in)"),
    (51, 26, "51 x 26 mm (2 x 1 in)"),
    (39, 100, "39 x 100 mm"),
]

DITHER = [
    ("Floyd", "Floyd-Steinberg (recommended)"),
    ("Jarvis", "Jarvis-Judice-Ninke"),
    ("Stucki", "Stucki"),
    ("Atkinson", "Atkinson"),
    ("Sierra", "Sierra"),
    ("SierraLite", "Sierra Lite"),
    ("Burkes", "Burkes"),
    ("Bayer4", "Ordered 4 x 4"),
    ("Bayer8", "Ordered 8 x 8"),
    ("Threshold", "Threshold (no dithering)"),
]

THRESHOLDS = [
    (64, "25%"),
    (96, "37.5%"),
    (128, "50% (default)"),
    (160, "62.5%"),
    (192, "75%"),
]

MARGINS = [
    (3, "3 mm (minimum)"),
    (5, "5 mm"),
    (10, "10 mm"),
    (15, "15 mm"),
    (20, "20 mm"),
    (30, "30 mm"),
]


def mm_to_pt(mm: float) -> float:
    return mm * 72.0 / 25.4


def fmt_pt_int(mm: float) -> str:
    """PPD page dimensions are conventionally integer points."""
    return str(int(round(mm_to_pt(mm))))


def page_id(width: int, height: int) -> str:
    # cupstestppd expects page size names to encode their dimensions.
    return f"{width}x{height}mm"


def ppd_filename(model: dict) -> str:
    return "brother-" + model["id"].lower() + ".ppd"


def generate_ppd(model: dict) -> str:
    mid = model["id"]
    dpi = model["dpi"]
    max_w = model["max_width_mm"]
    lines: list[str] = []

    def add(s: str = "") -> None:
        lines.append(s)

    add('*PPD-Adobe: "4.3"')
    add(f'*FormatVersion: "4.3"')
    add(f'*FileVersion: "{VERSION}"')
    add('*LanguageVersion: English')
    add('*LanguageEncoding: ISOLatin1')
    add(f'*PCFileName: "{model["id"].replace("-", "").upper()}.PPD"')
    add('*Manufacturer: "Brother"')
    add(f'*Product: "({mid})"')
    add(f'*ModelName: "Brother {mid}"')
    add(f'*ShortNickName: "Brother {mid}"')
    add(f'*NickName: "Brother {mid} label printer, {dpi} dpi"')
    add('*PSVersion: "(3010.000) 0"')
    add('*LanguageLevel: "3"')
    add('*ColorDevice: False')
    add('*DefaultColorSpace: Gray')
    add('*cupsBitsPerColor: 8')
    add('*FileSystem: False')
    add('*Throughput: "6"')
    add('*LandscapeOrientation: Plus90')
    add(f'*1284DeviceID: "MFG:Brother;MDL:{mid};"')
    add('*cupsVersion: 2.2')
    add('*cupsManualCopies: True')
    add('*cupsFilter: "application/vnd.cups-raster 0 brother-td-filter"')
    add('')
    add('*%')
    add('*% The filter receives a CUPS raster page and emits Brother TD')
    add('*% raster commands.  It owns all margins/scaling decisions, so the')
    add('*% page is described as fully imageable here.')
    add('*%')
    add('*HWMargins: 0 0 0 0')
    add(f'*MaxMediaWidth: "{fmt_pt_int(max_w)}"')
    add(f'*MaxMediaHeight: "{fmt_pt_int(3000)}"')
    add('*CenterRegistered: False')
    add('*NonUIOrderDependency: 10.0 AnySetup *CustomPageSize')
    add('*CustomPageSize True: ""')
    add(f'*ParamCustomPageSize Width: 1 points {fmt_pt_int(19)} {fmt_pt_int(max_w)}')
    add(f'*ParamCustomPageSize Height: 2 points {fmt_pt_int(12)} {fmt_pt_int(3000)}')
    add('*ParamCustomPageSize WidthOffset: 3 points 0 0')
    add('*ParamCustomPageSize HeightOffset: 4 points 0 0')
    add('*ParamCustomPageSize Orientation: 5 int 0 3')
    add('')

    # Page sizes ---------------------------------------------------------
    sizes = [(w, h, label) for (w, h, label) in PAGE_SIZES if w <= max_w]
    default_size = page_id(102, 152)
    if not any(page_id(w, h) == default_size for (w, h, _) in sizes):
        default_size = page_id(sizes[0][0], sizes[0][1])

    add('*OpenGroup: General/General')
    add('*OpenUI *PageSize/Media Size: PickOne')
    add('*OrderDependency: 10.0 AnySetup *PageSize')
    add(f'*DefaultPageSize: {default_size}')
    for width, height, label in sizes:
        pid = page_id(width, height)
        add(f'*PageSize {pid}/{label}: "<</PageSize[{fmt_pt_int(width)} {fmt_pt_int(height)}]>>setpagedevice"')
    add('*CloseUI: *PageSize')
    add(f'*DefaultPaperDimension: {default_size}')
    add(f'*DefaultImageableArea: {default_size}')
    for width, height, _ in sizes:
        pid = page_id(width, height)
        add(f'*PaperDimension {pid}: "{fmt_pt_int(width)} {fmt_pt_int(height)}"')
        add(f'*ImageableArea {pid}: "0 0 {fmt_pt_int(width)} {fmt_pt_int(height)}"')
    add('')
    add('*OpenUI *PageRegion/Page Region: PickOne')
    add('*OrderDependency: 10.0 AnySetup *PageRegion')
    add(f'*DefaultPageRegion: {default_size}')
    for width, height, label in sizes:
        pid = page_id(width, height)
        add(f'*PageRegion {pid}/{label}: "<</PageSize[{fmt_pt_int(width)} {fmt_pt_int(height)}]>>setpagedevice"')
    add('*CloseUI: *PageRegion')
    add('')

    # Resolution ---------------------------------------------------------
    add('*OpenUI *ColorModel/Color Model: PickOne')
    add('*OrderDependency: 20.0 AnySetup *ColorModel')
    add('*DefaultColorModel: Gray')
    add('*ColorModel Gray/Grayscale: "<</cupsColorSpace 3/cupsBitsPerColor 8/cupsColorOrder 0>>setpagedevice"')
    add('*CloseUI: *ColorModel')
    add('')
    add('*OpenUI *Resolution/Output Resolution: PickOne')
    add('*OrderDependency: 20.0 AnySetup *Resolution')
    add(f'*DefaultResolution: {dpi}dpi')
    add(f'*Resolution {dpi}dpi/{dpi} dpi: "<</HWResolution[{dpi} {dpi}]>>setpagedevice"')
    add('*CloseUI: *Resolution')
    add('*CloseGroup: General')
    add('')

    # Media / model ------------------------------------------------------
    add('*OpenGroup: Media/Media')
    add('*OpenUI *TDModel/Printer Model: PickOne')
    add('*OrderDependency: 20.0 AnySetup *TDModel')
    add(f'*DefaultTDModel: {mid}')
    add(f'*TDModel {mid}/{mid}: ""')
    add('*CloseUI: *TDModel')
    add('')
    add('*OpenUI *TDMediaType/Media Type: PickOne')
    add('*OrderDependency: 20.0 AnySetup *TDMediaType')
    add('*DefaultTDMediaType: DieCut')
    add('*TDMediaType DieCut/Die-cut labels: ""')
    add('*TDMediaType Continuous/Continuous roll: ""')
    add('*CloseUI: *TDMediaType')
    add('*CloseGroup: Media')
    add('')

    # Image quality ------------------------------------------------------
    add('*OpenGroup: ImageQuality/Image Quality')
    add('*OpenUI *TDDither/Dithering: PickOne')
    add('*OrderDependency: 30.0 AnySetup *TDDither')
    add('*DefaultTDDither: Floyd')
    for key, label in DITHER:
        add(f'*TDDither {key}/{label}: ""')
    add('*CloseUI: *TDDither')
    add('')
    add('*OpenUI *TDThreshold/Threshold: PickOne')
    add('*OrderDependency: 30.0 AnySetup *TDThreshold')
    add('*DefaultTDThreshold: 128')
    for value, label in THRESHOLDS:
        add(f'*TDThreshold {value}/{label}: ""')
    add('*CloseUI: *TDThreshold')
    add('')
    add('*OpenUI *TDScaling/Scaling: PickOne')
    add('*OrderDependency: 30.0 AnySetup *TDScaling')
    add('*DefaultTDScaling: Fit')
    add('*TDScaling Fit/Fit (shrink only): ""')
    add('*TDScaling FitEnlarge/Fit (allow enlargement): ""')
    add('*TDScaling Crop/Fill and crop: ""')
    add('*TDScaling None/Actual size (crop if needed): ""')
    add('*CloseUI: *TDScaling')
    add('')
    add('*OpenUI *TDRotate/Rotation: PickOne')
    add('*OrderDependency: 30.0 AnySetup *TDRotate')
    add('*DefaultTDRotate: 0')
    add('*TDRotate 0/0 degrees: ""')
    add('*TDRotate 90/90 degrees clockwise: ""')
    add('*TDRotate 180/180 degrees: ""')
    add('*TDRotate 270/90 degrees counter-clockwise: ""')
    add('*CloseUI: *TDRotate')
    add('')
    add('*OpenUI *TDInvert/Invert Image: Boolean')
    add('*OrderDependency: 30.0 AnySetup *TDInvert')
    add('*DefaultTDInvert: False')
    add('*TDInvert False/No: ""')
    add('*TDInvert True/Yes: ""')
    add('*CloseUI: *TDInvert')
    add('')
    add('*OpenUI *TDMirror/Mirror Image: Boolean')
    add('*OrderDependency: 30.0 AnySetup *TDMirror')
    add('*DefaultTDMirror: False')
    add('*TDMirror False/No: ""')
    add('*TDMirror True/Yes: ""')
    add('*CloseUI: *TDMirror')
    add('')
    add('*OpenUI *TDBorder/Black Side Margins: Boolean')
    add('*OrderDependency: 30.0 AnySetup *TDBorder')
    add('*DefaultTDBorder: False')
    add('*TDBorder False/No: ""')
    add('*TDBorder True/Yes: ""')
    add('*CloseUI: *TDBorder')
    add('*CloseGroup: ImageQuality')
    add('')

    # Finishing ----------------------------------------------------------
    add('*OpenGroup: Finishing/Finishing')
    add('*OpenUI *TDAlignX/Horizontal Alignment: PickOne')
    add('*OrderDependency: 40.0 AnySetup *TDAlignX')
    add('*DefaultTDAlignX: Center')
    add('*TDAlignX Left/Left: ""')
    add('*TDAlignX Center/Center: ""')
    add('*TDAlignX Right/Right: ""')
    add('*CloseUI: *TDAlignX')
    add('')
    add('*OpenUI *TDAlignY/Vertical Alignment: PickOne')
    add('*OrderDependency: 40.0 AnySetup *TDAlignY')
    add('*DefaultTDAlignY: Center')
    add('*TDAlignY Top/Top: ""')
    add('*TDAlignY Center/Center: ""')
    add('*TDAlignY Bottom/Bottom: ""')
    add('*CloseUI: *TDAlignY')
    add('')
    add('*OpenUI *TDMargin/Feed Margin: PickOne')
    add('*OrderDependency: 40.0 AnySetup *TDMargin')
    add('*DefaultTDMargin: 3')
    for value, label in MARGINS:
        add(f'*TDMargin {value}/{label}: ""')
    add('*CloseUI: *TDMargin')
    add('')
    add('*OpenUI *TDAutoTrim/Auto-trim Continuous Media: Boolean')
    add('*OrderDependency: 40.0 AnySetup *TDAutoTrim')
    add('*DefaultTDAutoTrim: True')
    add('*TDAutoTrim False/No: ""')
    add('*TDAutoTrim True/Yes: ""')
    add('*CloseUI: *TDAutoTrim')
    add('')
    add('*OpenUI *TDAutoCut/Auto Cut: Boolean')
    add('*OrderDependency: 40.0 AnySetup *TDAutoCut')
    add('*DefaultTDAutoCut: False')
    add('*TDAutoCut False/No: ""')
    add('*TDAutoCut True/Yes: ""')
    add('*CloseUI: *TDAutoCut')
    add('')
    add('*OpenUI *TDCutAtEnd/Cut at End: Boolean')
    add('*OrderDependency: 40.0 AnySetup *TDCutAtEnd')
    add('*DefaultTDCutAtEnd: True')
    add('*TDCutAtEnd False/No: ""')
    add('*TDCutAtEnd True/Yes: ""')
    add('*CloseUI: *TDCutAtEnd')
    add('')
    add('*OpenUI *TDPeeler/Peeler: Boolean')
    add('*OrderDependency: 40.0 AnySetup *TDPeeler')
    add('*DefaultTDPeeler: False')
    add('*TDPeeler False/No: ""')
    add('*TDPeeler True/Yes: ""')
    add('*CloseUI: *TDPeeler')
    add('')
    add('*OpenUI *TDCompression/Raster Compression: PickOne')
    add('*OrderDependency: 40.0 AnySetup *TDCompression')
    add('*DefaultTDCompression: TIFF')
    add('*TDCompression None/None: ""')
    add('*TDCompression TIFF/TIFF PackBits: ""')
    add('*CloseUI: *TDCompression')
    add('*CloseGroup: Finishing')
    add('')
    add('*cupsUIConstraints: "*TDMediaType DieCut *TDAutoCut True"')
    add('*cupsUIConstraints: "*TDMediaType DieCut *TDPeeler True"')
    add('')
    add('*% End of PPD')
    return "\n".join(lines) + "\n"


def generate_c_table(models: list[dict]) -> str:
    out: list[str] = []
    out.append("/* Generated by tools/generate.py - do not edit by hand. */")
    out.append("/*")
    out.append(" * SPDX-License-Identifier: MIT")
    out.append(" * Copyright (c) 2026 brother-td-cups contributors")
    out.append(" */")
    out.append("")
    out.append('#include "td_models.h"')
    out.append("")
    out.append("const td_model_t td_models[] = {")
    for m in models:
        out.append("    {")
        out.append(f'        .id = "{m["id"]}",')
        out.append(f'        .dpi = {m["dpi"]},')
        out.append(f'        .head_pins = {m["head_pins"]},')
        out.append(f'        .init_nuls = {m["init_nuls"]},')
        out.append(f'        .max_width_mm = {m["max_width_mm"]},')
        if m.get("usb_pid"):
            out.append(f'        .usb_vid = 0x{m["usb_vid"]},')
            out.append(f'        .usb_pid = 0x{m["usb_pid"]},')
        else:
            out.append("        .usb_vid = 0,")
            out.append("        .usb_pid = 0,")
        out.append("    },")
    out.append("};")
    out.append("")
    out.append("const size_t td_num_models = sizeof(td_models) / sizeof(td_models[0]);")
    out.append("")
    return "\n".join(out)


def main() -> int:
    try:
        models = json.loads(MODELS.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"generate.py: cannot read {MODELS}: {exc}", file=sys.stderr)
        return 1

    # Basic validation so a bad models.json cannot silently produce bad PPDs.
    seen: set[str] = set()
    for m in models:
        for key in ("id", "dpi", "head_pins", "init_nuls", "max_width_mm"):
            if key not in m:
                print(f"generate.py: model is missing {key!r}: {m!r}", file=sys.stderr)
                return 1
        if m["id"] in seen:
            print(f"generate.py: duplicate model {m['id']}", file=sys.stderr)
            return 1
        seen.add(m["id"])
        if m["dpi"] not in (203, 300):
            print(f"generate.py: unsupported dpi {m['dpi']} for {m['id']}", file=sys.stderr)
            return 1
        if m["head_pins"] not in (832, 1280):
            print(f"generate.py: unsupported head width {m['head_pins']} for {m['id']}", file=sys.stderr)
            return 1
        if m["init_nuls"] not in (350, 661):
            print(f"generate.py: unsupported invalidate length {m['init_nuls']} for {m['id']}", file=sys.stderr)
            return 1

    PPD_DIR.mkdir(parents=True, exist_ok=True)
    for old in PPD_DIR.glob("brother-td-*.ppd"):
        old.unlink()
    for model in models:
        path = PPD_DIR / ppd_filename(model)
        path.write_text(generate_ppd(model), encoding="utf-8")
        print(f"generated {path.relative_to(ROOT)}")

    C_TABLE.parent.mkdir(parents=True, exist_ok=True)
    C_TABLE.write_text(generate_c_table(models), encoding="utf-8")
    print(f"generated {C_TABLE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
