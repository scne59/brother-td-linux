#!/usr/bin/env python3
"""End-to-end test for the PAPPL printer application.

The test is skipped when the application, ipptool, or Ghostscript is not
available.
"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
APP = os.environ.get("BROTHER_TD_APP", str(ROOT / "build" / "brother-td-app"))
RASTER = os.environ.get("BROTHER_TD_RASTER", str(ROOT / "build" / "make_raster"))

os.environ.setdefault("BROTHER_TD_FILTER", str(ROOT / "build" / "brother-td-filter"))
os.environ.setdefault("BROTHER_TD_RASTER", RASTER)
sys.path.insert(0, str(ROOT / "tests"))
import integration  # noqa: E402


def skip(reason: str) -> None:
    print(f"pappl-integration: skipping ({reason})")
    raise SystemExit(0)


if not pathlib.Path(APP).is_file():
    skip("brother-td-app is not built")
if shutil.which("ipptool") is None:
    skip("ipptool is not installed")

PORT = None
CAPTURE = None
SPOOL = None
LOG = None
OUT = None
PROCESS = None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_port(port: int, timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"brother-td-app did not listen on port {port}")


def start_app(tmp: pathlib.Path) -> None:
    global PORT, CAPTURE, SPOOL, LOG, OUT, PROCESS

    PORT = free_port()
    CAPTURE = tmp / "capture.prn"
    SPOOL = tmp / "spool"
    LOG = tmp / "app.log"
    OUT = tmp / "app.out"
    SPOOL.mkdir()

    environment = dict(os.environ)
    ldpath = os.environ.get("PAPPL_LD_LIBRARY_PATH")
    if ldpath:
        environment["LD_LIBRARY_PATH"] = ldpath + (
            ":" + environment["LD_LIBRARY_PATH"] if environment.get("LD_LIBRARY_PATH") else ""
        )

    PROCESS = subprocess.Popen(
        [
            APP,
            "--model", "TD-4410D",
            "--device", f"file:{CAPTURE}",
            "--printer", "TD4410",
            "--port", str(PORT),
            "--spool", str(SPOOL),
            "--log", str(LOG),
        ],
        stdout=OUT.open("wb"),
        stderr=subprocess.STDOUT,
        env=environment,
    )
    wait_for_port(PORT)


def stop_app() -> None:
    if PROCESS is not None:
        PROCESS.terminate()
        try:
            PROCESS.wait(timeout=5)
        except subprocess.TimeoutExpired:
            PROCESS.kill()
            PROCESS.wait(timeout=5)


def ipptool_job(tmp: pathlib.Path, name: str, filename: pathlib.Path,
                document_format: str, extra: str = "",
                media: str = "na_index-3x5_3x5in") -> None:
    test = tmp / "test.ipp"
    test.write_text(
        "{\n"
        f'  NAME "{name}"\n'
        "  OPERATION Print-Job\n"
        "  GROUP operation-attributes-tag\n"
        "  ATTR charset attributes-charset utf-8\n"
        "  ATTR language attributes-natural-language en\n"
        "  ATTR uri printer-uri $uri\n"
        "  ATTR name requesting-user-name test\n"
        f"  ATTR mimeMediaType document-format {document_format}\n"
        f"  FILE {filename}\n"
        "  GROUP job-attributes-tag\n"
        f"  ATTR keyword media {media}\n"
        "  ATTR keyword print-scaling fit\n"
        f"{extra}"
        "  STATUS successful-ok\n"
        "}\n",
        encoding="utf-8",
    )
    if CAPTURE.exists():
        CAPTURE.unlink()

    result = subprocess.run(
        ["ipptool", "-tv", f"ipp://localhost:{PORT}/ipp/print/TD4410", str(test)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0 or "[PASS]" not in result.stdout:
        raise AssertionError(f"ipptool failed:\n{result.stdout[-2000:]}")

    deadline = time.time() + 15
    last_size = -1
    stable = 0
    while time.time() < deadline:
        if CAPTURE.exists():
            size = CAPTURE.stat().st_size
            if size > 0 and size == last_size:
                stable += 1
                if stable >= 3:
                    break
            else:
                stable = 0
                last_size = size
        time.sleep(0.1)
    if not CAPTURE.exists() or CAPTURE.stat().st_size == 0:
        raise AssertionError(f"no printer output; log:\n{LOG.read_text(errors='replace')[-2000:]}")


def check_startup_banner() -> None:
    text = OUT.read_text(errors="replace")
    expected = f"ipp://localhost:{PORT}/ipp/print/TD4410"
    assert expected in text, text
    assert f"/TD4410/ipp" not in text, text


def check_web() -> None:
    # Exercise the web pages that are easy to crash when driver data is
    # incomplete, especially the footer localization.
    for path in ("/", "/TD4410/", "/TD4410/media", "/TD4410/printing",
                 "/addprinter", "/config"):
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}{path}", timeout=5) as response:
            assert response.status == 200, (path, response.status)
            assert response.read(1), path

    with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/style.css", timeout=5) as response:
        css = response.read().decode("utf-8", errors="replace")
    assert "--td-primary" in css, "modern theme stylesheet is not being served"
    assert "fonts.googleapis.com" not in css, "stylesheet must not use external fonts"

    assert PROCESS.poll() is None, "printer application exited during web test"


def check_strings(tmp: pathlib.Path) -> None:
    # CUPS/libppd localizes standard inch sizes with a UTF-8 double-prime and
    # then writes a PPD declared as ISO-8859-1, which makes clients show
    # mojibake.  The app's ASCII catalog must override those labels.
    with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/strings-en",
                                timeout=5) as response:
        strings = response.read().decode("utf-8", errors="replace")
    assert '"media.na_index-4x6_4x6in" = "4 x 6 in";' in strings, strings
    assert '"media.na_index-3x5_3x5in" = "3 x 5 in";' in strings, strings
    assert "″" not in strings, "catalog must be ASCII"

    if shutil.which("driverless"):
        result = subprocess.run(
            ["driverless", f"ipp://localhost:{PORT}/ipp/print/TD4410"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            assert "*PageSize 4x6/4 x 6 in:" in result.stdout, result.stdout[-2000:]
            assert "″" not in result.stdout, "generated PPD contains a double-prime"


def check_media() -> None:
    # Every custom size must keep its own name.  PAPPL/libcups returns a
    # shared static buffer from pwgMediaForSize() for custom sizes, which
    # previously made most of the list collapse into custom_39x100mm.
    result = subprocess.run(
        ["ipptool", "-tv", f"ipp://localhost:{PORT}/ipp/print/TD4410",
         "/usr/share/cups/ipptool/get-printer-attributes.test"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout[-2000:]
    assert "printer-strings-uri (uri)" in result.stdout, result.stdout[-2000:]
    line = next((line for line in result.stdout.splitlines()
                 if "media-supported (1setOf keyword)" in line), "")
    assert line, result.stdout[-2000:]
    names = line.split("=", 1)[1].strip().split(",")
    assert len(names) == len(set(names)), f"duplicate media names: {names}"
    for expected in ("na_index-4x6_4x6in", "na_index-3x5_3x5in",
                     "oe_square-photo_4x4in", "custom_102x127mm_102x127mm",
                     "oe_photo-l_3.5x5in", "jpn_hagaki_100x148mm",
                     "om_card_54x86mm", "iso_a8_52x74mm"):
        assert expected in names, (expected, names)
    assert "custom_39x100mm_39x100mm" in names, names

    ready_line = next((line for line in result.stdout.splitlines()
                       if "media-ready (1setOf keyword)" in line), "")
    assert ready_line, result.stdout[-2000:]
    ready = ready_line.split("=", 1)[1].strip().split(",")
    assert len(ready) == len(set(ready)) == 16, ready
    assert ready[0] == "na_index-4x6_4x6in", ready
    for expected in ("custom_102x127mm_102x127mm", "na_index-3x5_3x5in"):
        assert expected in ready, (expected, ready)

    col_line = next((line for line in result.stdout.splitlines()
                     if "media-col-ready (1setOf collection)" in line), "")
    assert col_line, result.stdout[-2000:]
    sources = re.findall(r"media-source=([^ }]+)", col_line)
    assert len(sources) == len(set(sources)) == 16, sources
    print(f"pappl-integration media: {len(names)} distinct sizes, "
          f"{len(ready)} ready sizes")


def capture_pages(head_bytes: int) -> list[dict]:
    return integration.parse_stream(CAPTURE.read_bytes(), head_bytes)


def make_pwg(tmp: pathlib.Path, width: int = 608, height: int = 1016,
             page_width_mm: int = 76, page_length_mm: int = 127) -> pathlib.Path:
    output = tmp / "input.pwg"
    subprocess.run(
        [
            RASTER, "--pwg", "--output", str(output),
            "--width", str(width), "--height", str(height), "--dpi", "203",
            "--page-width-mm", str(page_width_mm),
            "--page-length-mm", str(page_length_mm),
            "--pattern", "square", "--colorspace", "K",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return output


def make_pdf(tmp: pathlib.Path) -> pathlib.Path | None:
    if shutil.which("ps2pdf") is None:
        return None
    ps = tmp / "input.ps"
    pdf = tmp / "input.pdf"
    ps.write_text(
        "%!PS-Adobe-3.0\n"
        "%%Pages: 1\n"
        "%%Page: 1 1\n"
        "0 setgray\n"
        "36 36 120 120 rectfill\n"
        "showpage\n"
        "%%EOF\n",
        encoding="ascii",
    )
    result = subprocess.run(["ps2pdf", str(ps), str(pdf)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            check=False)
    return pdf if result.returncode == 0 else None


def check_pwg(tmp: pathlib.Path) -> None:
    ipptool_job(tmp, "PWG raster", make_pwg(tmp), "image/pwg-raster")
    pages = capture_pages(104)
    assert len(pages) == 1, len(pages)
    assert 960 <= len(pages[0]["lines"]) <= 970, len(pages[0]["lines"])
    assert pages[0]["info"][2] == 76
    assert pages[0]["info"][3] == 127
    assert any(any(line) for line in pages[0]["lines"])


def check_ready_media_print(tmp: pathlib.Path) -> None:
    # A virtual roll must select the corresponding page size without
    # affecting the physical print pipeline.
    ipptool_job(tmp, "PWG raster 4x5",
                make_pwg(tmp, 816, 1016, 102, 127), "image/pwg-raster",
                "  ATTR keyword media-source roll-102x127mm\n",
                media="custom_102x127mm_102x127mm")
    pages = capture_pages(104)
    assert len(pages) == 1, len(pages)
    assert pages[0]["info"][2] == 102, pages[0]["info"]
    assert pages[0]["info"][3] == 127, pages[0]["info"]
    assert any(any(line) for line in pages[0]["lines"])


def check_pdf(tmp: pathlib.Path) -> None:
    pdf = make_pdf(tmp)
    if pdf is None:
        print("pappl-integration: ps2pdf not available; skipping PDF case")
        return
    ipptool_job(tmp, "PDF", pdf, "application/pdf")
    pages = capture_pages(104)
    assert len(pages) == 1, len(pages)
    assert 960 <= len(pages[0]["lines"]) <= 970, len(pages[0]["lines"])
    assert any(any(line) for line in pages[0]["lines"])


def check_pdf_media(tmp: pathlib.Path) -> None:
    pdf = make_pdf(tmp)
    if pdf is None:
        return
    ipptool_job(tmp, "PDF 4x5", pdf, "application/pdf",
                "  ATTR keyword media-source roll-102x127mm\n",
                media="custom_102x127mm_102x127mm")
    pages = capture_pages(104)
    assert len(pages) == 1, len(pages)
    assert pages[0]["info"][2] == 102, pages[0]["info"]
    assert pages[0]["info"][3] == 127, pages[0]["info"]
    assert any(any(line) for line in pages[0]["lines"])


def check_options(tmp: pathlib.Path) -> None:
    pdf = make_pdf(tmp)
    if pdf is None:
        return
    extra = (
        "  ATTR integer copies 2\n"
        "  ATTR keyword td-media-type continuous\n"
        "  ATTR keyword td-dither bayer8\n"
        "  ATTR keyword td-auto-cut true\n"
        "  ATTR integer td-margin 10\n"
    )
    ipptool_job(tmp, "PDF with TD options", pdf, "application/pdf", extra)
    pages = capture_pages(104)
    assert len(pages) == 2, len(pages)
    for page in pages:
        assert page["info"][1] == 0x0A  # continuous
        assert page["info"][3] == 0     # continuous length is not fixed
        assert page["margin"] == 80     # 10 mm at 203 dpi
        assert page["various"] & 0x40   # auto-cut
    assert pages[0]["info"][8] == 0
    assert pages[1]["info"][8] == 1


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="td-pappl-") as tmpname:
        tmp = pathlib.Path(tmpname)
        try:
            start_app(tmp)
            check_startup_banner()
            print("pappl-integration banner: OK")
            check_web()
            print("pappl-integration web: OK")
            check_strings(tmp)
            print("pappl-integration strings: OK")
            check_media()
            print("pappl-integration media: OK")
            check_pwg(tmp)
            print("pappl-integration PWG: OK")
            check_ready_media_print(tmp)
            print("pappl-integration virtual roll: OK")
            check_pdf(tmp)
            print("pappl-integration PDF: OK")
            check_pdf_media(tmp)
            print("pappl-integration PDF virtual roll: OK")
            check_options(tmp)
            print("pappl-integration options: OK")
        finally:
            stop_app()
    print("pappl-integration: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
