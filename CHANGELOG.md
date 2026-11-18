# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/).

## [1.0.0] - 2026-11-17

### Added

- Native CUPS raster filter (`brother-td-filter`) for Brother TD-4D label
  printers.
- Generated PPDs for 16 models: TD-4210D, TD-4410D, TD-4420DN, TD-4510D,
  TD-4520DN, TD-4550DNWB, TD-4215D, TD-4415D, TD-4425DN, TD-4455DNWB,
  TD-4525DN, TD-4555DNWB, TD-4425DNF, TD-4555DNWBF, TD-4420DNFC, and
  TD-4550DNWBFC.
- Die-cut and continuous media support.
- Floyd-Steinberg, Jarvis, Stucki, Atkinson, Sierra, Sierra Lite, Burkes,
  ordered 4x4/8x8, and threshold halftoning. The PPD requests 8-bit
  grayscale raster so the driver controls halftoning for PDF/text jobs too.
- Fit/fill/crop/actual-size scaling, rotation, alignment, mirroring,
  inversion, and black side margins.
- TIFF/PackBits raster compression with incompressible-line fallback.
- Continuous-media auto-trim, feed margin, auto-cut, cut-at-end, and peeler
  options.
- Multi-page jobs and manual copy handling.
- Common 3x5 (76x127mm), 4x5, 3x3, 3x2.5, 3x1.5, 2x3, and 2x1.5 label
  sizes, plus custom page-size support.
- Debug logging and raw stream capture (`BROTHER_TD_DEBUG`,
  `BROTHER_TD_DEBUG_DIR`).
- Unit tests for geometry, image operations, dithering, PackBits, and the
  command stream.
- Synthetic end-to-end CUPS raster tests and PPD validation.
- Optional native PAPPL/IPP Everywhere printer application
  (`brother-td-app`) for CUPS 3 and driverless clients. It accepts
  PWG/Apple raster, PDF/PostScript through Ghostscript, and JPEG/PNG.
- Makefile install/uninstall, man pages, and GitHub Actions CI.

### Fixed relative to `brother-td.pl`

- Correct 160-byte raster rows for 300 dpi models (the original always used
  104 bytes).
- Correct die-cut printable height at 300 dpi.
- Use Brother's documented print-area width instead of the full media width.
- Use zero feed margin for die-cut labels.
- Send `FF` only between pages and `Ctrl-Z` only on the last page.
- Reset the printer command mode at the end of the job.
- Correct per-model 350/661-byte invalidate sequence.
- Accept 1-bit, 8-bit, and 16-bit grayscale, RGB, RGBA, CMY, and CMYK CUPS
  raster input.
- Handle PDF/SVG and other formats through CUPS instead of shelling out to
  ImageMagick/rsvg.
- Document that CUPS silently falls back to the default page size when an
  unknown `PageSize` is requested, which can overprint smaller labels.
- Use a per-user state/spool directory when the PAPPL app is not run as root,
  and create it automatically.  An unwritable explicit `--spool` now produces
  a clear error instead of a generic PAPPL failure.
- Set a PAPPL web-interface footer, fixing a crash on the first web request.
- Add PAPPL web-interface coverage to the integration tests.
- Print the exact web/IPP endpoint at app startup, log the job format and
  device URI, and add `--debug`, `--no-state`, and `--check-device` for
  diagnosis.
- Make `--device` replace a stale saved printer with a different device URI
  instead of silently ignoring the new configuration.
- Install a real `brother-td-app.service` unit from `make install-pappl` and
  the Debian package, with a configurable executable path.
- Log the actual IPP endpoint for every configured/discovered printer, plus
  job format, device, Ghostscript progress, and a 120-second Ghostscript
  timeout to make stuck jobs diagnosable.
- Correct the printed IPP endpoint to `/ipp/print/<printer>`; the previous
  banner used PAPPL's web path `/ <printer>/ipp`, which made CUPS queues
  point at a non-IPP URL and hang in "processing".
- Copy the document to `/tmp` before invoking Ghostscript.  Ubuntu's
  AppArmor profile for `/usr/bin/gs` denies reading PAPPL spool files under
  `/var/lib/brother-td-app`, which made PDF jobs abort when the app ran as
  the `lp` service user.
- Default PAPPL logging to stderr so `systemctl status`/`journalctl` shows
  job diagnostics; previously an omitted `--log` sent them to a temporary
  file.
- Add a modern responsive web theme with system fonts, card layout, clearer
  tables/forms, navigation links, and automatic dark mode.  The theme is
  installed as `/usr/share/brother-td-app/style.css`.
- Add `--no-tls` for Android/driverless clients that discover the app but
  reject its self-signed IPPS certificate, plus Android printing instructions.
- Advertise the `_universal` DNS-SD subtype under `_ipp._tcp` and
  `_ipps._tcp` so iPhone and iPad discover the app through AirPrint; add
  iPhone/iPad printing instructions.
- Fix duplicate custom media names: libcups returns a shared static buffer
  from `pwgMediaForSize()`, which made most sizes collapse into
  `custom_39x100mm`. Each size now has its own IPP media name.
- Expose 16 common label sizes as separate virtual media sources so Android
  and AirPrint paper-size menus can select them instead of only 4×6.
- Advertise standard PWG media names (3.5×5, 2×3.5, 54×86, 89×119, 100×148,
  A7/A8, and others) because Android's print service ignores custom IPP
  media names.
- The packaged systemd service now runs with `--no-tls` by default, because
  iOS prefers IPPS while PAPPL may not be able to create server credentials
  as user `lp`.
- The default IPP port is now selected automatically on first start and
  saved in `SPOOL/port`, with `--port N` still available to pin a fixed port.
  This avoids collisions with other services on port 8000.
- Serve an ASCII `printer-strings-uri` catalog so CUPS/libppd writes
  `4 x 6 in` instead of the UTF-8 `4 x 6″` in a PPD declared as ISO-8859-1,
  which made inch sizes appear garbled in some CUPS dialogs.
- Build with PAPPL 1.3.x as shipped on Ubuntu 24.04. Automatic first-run
  discovery still requires PAPPL 1.4 or newer; older versions use `--device`
  or the web interface.
- Keep the Add Printer page available when `avahi-daemon` is not running
  instead of aborting in PAPPL 1.3's DNS-SD browser. The page shows a
  diagnostic and instructions to start Avahi or use `--device`.
- Serve the web stylesheet, printer strings, favicon, and navigation icon
  through callback resources, and pin standard PWG media names explicitly.
  This avoids crashes and missing standard names with PAPPL 1.3.1/libcups
  2.4.2 on 32-bit ARM.
- Make `make check` fail if the PAPPL integration test fails, and wait for
  the startup banner instead of racing it. The full test suite now passes on
  Raspberry Pi 4 / Raspbian 12 armhf with PAPPL 1.3.1.
- Build against PAPPL 1.0.3 as shipped on Ubuntu 22.04 by using compatibility
  wrappers for the older API names. PAPPL 1.0.x ignores `--no-tls` and still
  advertises IPPS, so newer PAPPL is recommended for iOS/AirPrint.

[1.0.0]: https://github.com/scne59/brother-td-linux/releases/tag/v1.0.0
