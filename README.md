# brother-td-linux

A native Linux CUPS driver and IPP Everywhere printer application for Brother
TD-4D series direct-thermal label printers. This is the Linux/CUPS counterpart
to the original [`brother-td.pl`](https://github.com/scne59/brother-td)
utility. It turns the printer's raster protocol into a proper CUPS filter plus
generated PPDs, so labels can be printed from any CUPS client: `lp`,
LibreOffice, GIMP, Firefox, `system-config-printer`, the CUPS web interface,
Samba, Android, iPhone/iPad, and so on.

The Debian source package and CUPS filter keep the internal names
`brother-td-cups` and `brother-td-filter`; the repository itself is
`brother-td-linux`.

The driver emits the printer's native raster command stream. It does not depend
on a custom backend: CUPS' standard `usb`, `socket`, `ipp`, or any other
backend can transport the data.

## Features

- **CUPS-native architecture** — a `application/vnd.cups-raster` filter and
  static PPDs. PDF, PostScript, text, and image jobs are converted by CUPS'
  normal filter chain.
- **CUPS 3 / IPP Everywhere printer application** — an optional native PAPPL
  application exposes the printer without a PPD. It accepts PWG/Apple raster
  directly and uses Ghostscript for PDF/PostScript.
- **Automatic IPP port** — the app chooses a free TCP port on first start,
  saves it in `SPOOL/port`, and reuses it; mDNS advertises the current port.
  `--port N` still pins a fixed port.
- **Modern web interface** — a responsive card-based theme with system fonts,
  clearer forms/tables, navigation links, and automatic dark mode.
- **16 printer models** — the six original TD-4D models plus the 2024 TD-4D
  and linerless variants.
- **Die-cut labels and continuous rolls** — including automatic trimming of
  unused rows on continuous media.
- **High-quality monochrome conversion** — Floyd-Steinberg, Jarvis,
  Stucki, Atkinson, Sierra, Sierra Lite, Burkes, ordered 4×4/8×8, and plain
  threshold.
- **Correct geometry** — uses Brother's documented print-area pin counts and
  the 203/300 dpi head widths instead of stretching to the full label width.
- **Landscape and custom page sizes** — print dialogs that describe the
  document page (Inkscape, for example) are accepted; landscape pages are
  transposed to the printer's physical media orientation.
- **TIFF/PackBits raster compression** — with the documented fallback for
  incompressible lines. Can be disabled for troubleshooting.
- **Scaling, rotation, alignment, mirroring, inversion, and black side
  margins**.
- **Multiple pages and copies** — handled by the filter (`cupsManualCopies`).
- **Auto-cut, cut-at-end, and peeler control** for continuous media.
- **Debug capture** — write the exact byte stream sent to the printer to a
  file for inspection.
- **Browser-only WebUSB app** — a static page in `web/` renders PDF and image
  files in the browser and sends the native command stream directly over USB,
  with no CUPS queue, printer application or server.
- **Tested** — unit tests, PPD validation, and an end-to-end synthetic CUPS
  raster test suite.

## Supported models

| Model | Resolution | Head | Max media | USB VID:PID |
|---|---:|---:|---:|---|
| TD-4210D | 203 dpi | 832 pins | 104 mm | `04f9:20f2` |
| TD-4410D | 203 dpi | 832 pins | 104 mm | `04f9:20b6` |
| TD-4420DN | 203 dpi | 832 pins | 104 mm | `04f9:20b7` |
| TD-4510D | 300 dpi | 1280 pins | 108 mm | `04f9:20b8` |
| TD-4520DN | 300 dpi | 1280 pins | 108 mm | `04f9:20b9` |
| TD-4550DNWB | 300 dpi | 1280 pins | 108 mm | `04f9:20ba` |
| TD-4215D | 203 dpi | 832 pins | 104 mm | — |
| TD-4415D | 203 dpi | 832 pins | 104 mm | — |
| TD-4425DN | 203 dpi | 832 pins | 104 mm | — |
| TD-4455DNWB | 203 dpi | 832 pins | 104 mm | — |
| TD-4525DN | 300 dpi | 1280 pins | 108 mm | — |
| TD-4555DNWB | 300 dpi | 1280 pins | 108 mm | — |
| TD-4425DNF | 203 dpi | 832 pins | 106 mm | — |
| TD-4555DNWBF | 300 dpi | 1280 pins | 108 mm | — |
| TD-4420DNFC | 203 dpi | 832 pins | 106 mm | — |
| TD-4550DNWBFC | 300 dpi | 1280 pins | 108 mm | — |

The first six are the models supported by the original utility. The remaining
models use the same raster command set. The driver uses the 661-byte invalidate
sequence documented for that generation, and as a safe fallback when no explicit
`TDModel` option is supplied.

## How it works

```text
application/pdf ──► pdftopdf ──► gstoraster ──► application/vnd.cups-raster
                                                        │
                                                        ▼
                                              brother-td-filter
                                                        │
                                                        ▼
                                       Brother TD raster command stream
                                                        │
                                                        ▼
                                      CUPS usb / socket / ipp backend
                                                        │
                                                        ▼
                                                   TD-4D printer
```

The filter reads the CUPS raster page, converts it to 8-bit grayscale, applies
scaling/rotation/alignment, dithers it to one bit per pixel, and emits the
Brother raster commands (`ESC i z`, `g`, `Z`, `FF`, `Ctrl-Z`, etc.). The
printer's own USB or network backend only has to move bytes.

This is the same model used by most Linux printer drivers and avoids libusb
permission problems: CUPS' USB backend already has the right udev rules and
device access.

## Requirements

Runtime:

- CUPS 2.2 or newer (tested with CUPS 2.4)
- `cups-filters` and Ghostscript for PDF/PostScript/text input
- A CUPS backend for your transport (`usb`, `socket`, ...)

Build:

- C11 compiler, `make`, `pkg-config`
- `libcups2-dev` (or the equivalent development package)
- Python 3 only if you want to regenerate PPDs from `models.json`
- `libpappl-dev` only for the optional PAPPL printer application (1.0.3
  or newer; automatic first-run printer discovery requires PAPPL 1.4)

Tests additionally use:

- `cupstestppd` from `cups-ppdc`
- Python 3
- `libcups2-dev` for the synthetic raster generator

The PAPPL application and test suite are also tested on Raspberry Pi 4
(Raspbian 12, 32-bit `armhf`, PAPPL 1.3.1) and Raspberry Pi 5
(Ubuntu 22.04, arm64, PAPPL 1.0.3), as well as on x86-64 with PAPPL 1.4.x.

## Build and install

```sh
git clone https://github.com/scne59/brother-td-linux.git
cd brother-td-linux
make
sudo make install
```

The default install locations are taken from `cups-config`:

- filter: `$(cups-config --serverbin)/filter/brother-td-filter`
- PPDs: `$(cups-config --datadir)/model/brother-td-*.ppd`
- man pages: `$(PREFIX)/share/man/man7`
- docs: `$(PREFIX)/share/doc/brother-td`

Useful overrides:

```sh
make install PREFIX=/usr DESTDIR=/tmp/staging
make install CUPS_SERVERBIN=/usr/lib/cups CUPS_DATADIR=/usr/share/cups
```

Uninstall with `sudo make uninstall`.

Restart CUPS after installing or removing PPDs so the driver list is refreshed:

```sh
sudo systemctl restart cups
```

Existing queues keep a private copy of the PPD in `/etc/cups/ppd/`. After
upgrading the driver (for example to get a new `PageSize`), refresh the queue
so it picks up the new PPD:

```sh
sudo lpadmin -p TD4210 -P /usr/share/cups/model/brother-td-4210d.ppd
```

### Debian/Ubuntu package

The `debian/` directory builds `printer-driver-brother-td` and
`brother-td-printer-app` packages:

```sh
sudo apt install debhelper cups-ppdc cups-filters ghostscript libcups2-dev pkg-config python3
dpkg-buildpackage -b -us -uc -d
sudo dpkg -i ../printer-driver-brother-td_*.deb ../brother-td-printer-app_*.deb
```

### Prebuilt release packages

Every tagged GitHub release includes:

- Debian/Ubuntu: `printer-driver-brother-td_<version>_<arch>.deb` and
  `brother-td-printer-app_<version>_<arch>.deb` for `amd64`, `arm64`, `armhf`
- Fedora/RHEL/openSUSE: `printer-driver-brother-td-<version>.<dist>.<arch>.rpm`
  and `brother-td-printer-app-<version>.<dist>.<arch>.rpm` for `x86_64` and
  `aarch64`
- Arch Linux: `printer-driver-brother-td-<version>-<rel>-x86_64.pkg.tar.zst`
  and `brother-td-printer-app-<version>-<rel>-x86_64.pkg.tar.zst`
- `brother-td-linux-<version>.tar.gz` and `SHA256SUMS`

The `.deb` packages are built on Ubuntu 22.04 to keep them usable on older
Raspberry Pi OS and Ubuntu systems. RPMs are built in Fedora and Arch
packages in Arch Linux from the same tagged source.

Debian/Ubuntu:

```sh
sudo apt install ./printer-driver-brother-td_*_arm64.deb \
                 ./brother-td-printer-app_*_arm64.deb
```

Fedora/RHEL:

```sh
sudo dnf install ./printer-driver-brother-td-*.x86_64.rpm \
                 ./brother-td-printer-app-*.x86_64.rpm
```

Arch Linux:

```sh
sudo pacman -U ./printer-driver-brother-td-*.pkg.tar.zst \
               ./brother-td-printer-app-*.pkg.tar.zst
```

Replace `arm64`/`x86_64` with the architecture matching the machine. The
PAPPL application packages depend on PAPPL at runtime; installing PAPPL 1.4
or newer enables automatic printer discovery on first start. Arch Linux ARM
users can rebuild the included `packaging/arch/PKGBUILD` for `aarch64` or
`armv7h`.

## Add a printer

1. Find the device URI:

   ```sh
   lpinfo -v | grep -i brother
   lsusb | grep -i brother
   ```

   Typical USB URIs look like:

   ```text
   usb://Brother/TD-4410D
   usb://Brother/TD-4410D?serial=000G0G123456
   ```

   Network models can use a raw socket URI:

   ```text
   socket://192.168.1.50:9100
   ```

2. List the installed PPDs:

   ```sh
   lpinfo -m | grep -i 'Brother TD'
   ```

3. Create the queue. The PPD name is the lower-case model name:

   ```sh
   sudo lpadmin -p TD4410 -E \
       -v 'usb://Brother/TD-4410D?serial=000G0G123456' \
       -m brother-td-4410d.ppd
   sudo lpadmin -d TD4410
   ```

   For a network model:

   ```sh
   sudo lpadmin -p TD4520 -E \
       -v socket://192.168.1.50:9100 \
       -m brother-td-4520dn.ppd
   ```

4. Print a test label:

   ```sh
   lp -d TD4410 -o PageSize=102x152mm -o TDMediaType=DieCut test.pdf
   ```

You can also use the CUPS web interface at <http://localhost:631> and choose
the "Brother TD-…" PPD.

## CUPS 3 / PAPPL printer application

CUPS 3 removes PPD-based drivers in favour of IPP Everywhere. The optional
`brother-td-app` program is a native PAPPL printer application that exposes
each supported model as an IPP Everywhere/AirPrint printer, so it works with
CUPS 3, the CUPS Snap, mobile clients, and other driverless clients.

Build it when `libpappl-dev` is installed (Debian/Ubuntu package name):

```sh
sudo apt install libpappl-dev cups-ipp-utils
make pappl
sudo make install-pappl PREFIX=/usr
```

Ubuntu 24.04 ships PAPPL 1.3.1 and Ubuntu 22.04 ships PAPPL 1.0.3. The app
builds and prints with `--device` on both, but automatic first-run printer
discovery requires PAPPL 1.4 or newer. On older PAPPL versions, add a printer
through the web interface (with `avahi-daemon` running) or pass `--device` and
`--printer`. PAPPL 1.0.x also ignores `--no-tls` and always advertises IPPS.

Run one printer connected to a device URI. As root the default spool is
`/var/spool/brother-td-app`; as a normal user it defaults to
`$XDG_STATE_HOME/brother-td-app` or `~/.local/state/brother-td-app`, and the
directory is created automatically. Use `--spool` to override it.

```sh
# USB
brother-td-app --model TD-4410D \
    --device 'usb://Brother/TD-4410D?serial=...' \
    --printer TD4410

# Network
brother-td-app --model TD-4520DN \
    --device socket://192.168.1.50:9100 \
    --printer TD4520
```

On first start the app chooses a free TCP port, saves it in
`SPOOL/port`, and reuses it on later starts. The startup message prints the
exact web and IPP endpoints. Use `--port N` to pin a specific port instead.

`make install-pappl` installs `brother-td-app.service`; enable it with:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now brother-td-app
```

The unit installed by `make install-pappl PREFIX=/usr` uses
`/usr/bin/brother-td-app`. If you install under `/usr/local`, the generated
unit uses `/usr/local/bin/brother-td-app`.

The packaged service auto-discovers printers and runs the web interface with
`--no-tls`, because iOS prefers IPPS and PAPPL cannot always create server
credentials as user `lp`. To bind one specific printer, add a systemd drop-in:

```sh
sudo systemctl edit brother-td-app
```

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/brother-td-app --model TD-4410D \
    --device usb://Brother/TD-4410D?serial=... \
    --printer TD4410 --no-tls --spool /var/lib/brother-td-app
```

The `[Service]` line must not start with `#`; otherwise systemd ignores every
line in the drop-in.

Then restart it:

```sh
sudo systemctl restart brother-td-app
```

Add it to CUPS as an IPP Everywhere printer:

```sh
PORT=$(cat /var/lib/brother-td-app/port)
sudo lpadmin -p TD4410 -E \
    -v "ipp://localhost:$PORT/ipp/print/TD4410" \
    -m everywhere
```

On CUPS 3, use the same IPP URI in the CUPS web interface or with `lpadmin`.
The app also discovers USB/network printers and lets you add them from its
web interface when started without `--device`.

The app prints its exact web and IPP endpoints at startup. If a CUPS queue
does not print, check:

```sh
PORT=$(cat /var/lib/brother-td-app/port)
ps -ef | grep brother-td-app
curl -I "http://localhost:$PORT/"             # web interface
lpstat -t
lpstat -v TD4210                               # queue device URI
lpoptions -p TD4210 -l | head                  # media/option list
tail -f /var/log/cups/error_log
```

The printer name in the CUPS URI must match the app resource exactly, for
example `--printer TD4210` gives
`ipp://localhost:<port>/ipp/print/TD4210`. If an old state file contains a
different printer name, start the app with `--no-state` or remove
`SPOOL/state`. Check a device URI without starting the server with:

```sh
brother-td-app --model TD-4210D --device 'usb://Brother/TD-4210D?serial=...' --check-device
```

Formats:

- **PWG Raster and Apple Raster** are processed directly.
- **PDF and PostScript** are rasterized with Ghostscript.
- **JPEG and PNG** are handled by PAPPL's built-in image filters.

The TD-specific IPP options are exposed as vendor attributes:
`td-media-type`, `td-dither`, `td-compression`, `td-scaling`, `td-rotate`,
`td-margin`, `td-threshold`, `td-align-x`, `td-align-y`, `td-invert`,
`td-mirror`, `td-border`, `td-auto-trim`, `td-auto-cut`, `td-cut-at-end`, and
`td-peeler`.

The application test is part of `make check` when the app has been built:

```sh
make check
```

## Browser app (WebUSB)

`web/` contains a serverless web app that prints PDF and image files directly
from the browser over USB. It does not need CUPS or the printer application:
the raster pipeline is ported to JavaScript, PDFs are rendered with pdf.js,
and the resulting Brother command stream is sent through WebUSB.

```sh
cd web
python3 -m http.server 8080 --bind 127.0.0.1
```

Open <http://localhost:8080/> in Chrome or Edge, connect the printer, and
print. WebUSB requires a secure context and the USB interface must not be
claimed by CUPS or `brother-td-app`. See [`web/README.md`](web/README.md) for
the browser requirements, udev rules, the raw `.prn` fallback and the test
instructions.

`make check-web` runs the JavaScript driver tests; they are also part of
`make check` when Node.js is available.

## Printing from Android

The PAPPL application is a driverless IPP Everywhere / AirPrint printer and
advertises itself as Mopria-certified via mDNS. Android can print to it
directly; a CUPS queue on the Linux host is not involved.

1. Make sure the service is running with the printer name that matches the
   app endpoint:

   ```sh
   systemctl status brother-td-app
   journalctl -u brother-td-app -n 20 --no-pager
   ```

   It should show:

   ```text
   printer: TD4210  ipp://localhost:<port>/ipp/print/TD4210
   ```

   The actual port is also saved in `/var/lib/brother-td-app/port`.

2. Connect the phone to the **same Wi-Fi/LAN** as the Linux host. Guest Wi-Fi
   networks that isolate clients will not work. The host in this setup
   advertises on `192.168.0.202`, for example.

3. Allow the relevant ports on the host if a firewall is active:

   ```sh
   PORT=$(cat /var/lib/brother-td-app/port)
   sudo ufw allow "$PORT"/tcp
   sudo ufw allow 5353/udp
   ```

4. On Android, enable the print service:

   - **Settings → Connected devices → Connection preferences → Printing**
   - Enable **Default Print Service** (Mopria) or install **Mopria Print
     Service** from the Play Store.
   - Tap the print service and look for **TD4210**.

5. Open a PDF or photo on the phone, choose **Share → Print**, select
   **TD4210**, choose the label size (`3x5` / `4x6`), and print.

If Android discovers the printer but cannot connect, it may be preferring
IPPS and rejecting the app's self-signed certificate. Add `--no-tls` to the
service drop-in and restart it:

```sh
sudo systemctl edit brother-td-app
```

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/brother-td-app --model TD-4210D \
    --device 'usb://Brother/TD-4210D?serial=000F2G731063' \
    --printer TD4210 --no-tls \
    --spool /var/lib/brother-td-app
```

```sh
sudo systemctl daemon-reload
sudo systemctl restart brother-td-app
```

Then remove and re-add the printer in Android's print settings.

If mDNS discovery is blocked, use a manual IPP-capable print app such as
**Mopria Print Service**, **Let's Print Droid**, or **NokoPrint**, and add:

```text
ipp://192.168.0.202:<port>/ipp/print/TD4210
```

Use the port saved in `/var/lib/brother-td-app/port`, or the port printed in
the service journal. The mDNS advertisement always contains the current port.

The Android print dialog may default to A4/Letter. Select the 3×5 or 4×6
label size; the app maps it to the TD-4210D print area and scales the page.

## Printing from iPhone and iPad

iPhone and iPad print through **AirPrint**, which is built into iOS. No app or
driver is needed, and the Linux CUPS queue is not involved. The PAPPL
application advertises itself with the `_universal` DNS-SD subtype that
AirPrint uses, under both `_ipp._tcp` and `_ipps._tcp`.

1. Install the current app so the AirPrint subtype is advertised:

   ```sh
   cd brother-td-linux
   make pappl
   sudo make install-pappl PREFIX=/usr
   sudo systemctl daemon-reload
   sudo systemctl restart brother-td-app
   ```

2. Connect the iPhone/iPad to the **same Wi-Fi/LAN** as the Linux host.
   Guest Wi-Fi with client isolation will not work. Allow discovery and
   printing through the firewall if one is active:

   ```sh
   PORT=$(cat /var/lib/brother-td-app/port)
   sudo ufw allow "$PORT"/tcp
   sudo ufw allow 5353/udp
   ```

3. Verify that the printer is advertised with the AirPrint subtype:

   ```sh
   avahi-browse -rt _universal._sub._ipp._tcp
   ```

   The output should contain `TD4210`, `u104-3.local`, the current port, and
   TXT entries including `URF=`, `pdl=...application/pdf...`, and `TLS=1.2`.

4. On the iPhone/iPad, open a document, photo, email, or web page and tap
   **Share → Print**. Under **Printer**, select **TD4210**. Choose the label
   size (for example 4×6 or 3×5) if iOS offers it, then tap **Print**.

The printer name in the AirPrint list comes from the `--printer` option. The
name `TD4210` therefore matches
`ipp://192.168.0.202:<port>/ipp/print/TD4210`.

### If the iPhone does not show the printer

- Confirm the service is running and the host is reachable:

  ```sh
  systemctl status brother-td-app
  journalctl -u brother-td-app -n 20 --no-pager
  ping u104-3.local
  ```

- Toggle Wi-Fi off and on on the iPhone, then reopen the print dialog.
  iOS caches the printer list for a while.

- iOS has no reliable “add printer by IP address” dialog. If mDNS/Bonjour is
  blocked on the network, use a third-party IPP client such as **NokoPrint**
  and add the printer manually:

  ```text
  ipp://192.168.0.202:<port>/ipp/print/TD4210
  ```

  Use the port saved in `/var/lib/brother-td-app/port`.

- If iOS lists `TD4210` but the job fails immediately, it may be rejecting
  the app's self-signed IPPS certificate. Rebuild/install the latest app, then
  add `--no-tls` to the service drop-in and restart:

  ```sh
  sudo systemctl edit brother-td-app
  ```

  ```ini
  [Service]
  ExecStart=
  ExecStart=/usr/bin/brother-td-app --model TD-4210D \
      --device 'usb://Brother/TD-4210D?serial=000F2G731063' \
      --printer TD4210 --no-tls \
      --spool /var/lib/brother-td-app
  ```

  ```sh
  sudo systemctl daemon-reload
  sudo systemctl restart brother-td-app
  ```

  With `--no-tls` the app advertises only unencrypted IPP on its selected
  port. Toggle Wi-Fi on the iPhone again so it re-discovers the printer.

- Some iOS print dialogs default to A4 or US Letter. If a 3×5 or 4×6 label
  size is offered, choose it. Otherwise the app scales the page to the
  printer's printable width.

### Label sizes on iPhone and iPad

AirPrint expects each selectable size to belong to a separate media source,
so the app exposes the common label sizes as virtual rolls. The IP and print
path are unchanged; a virtual roll only names a size.

The AirPrint paper-size menu offers these sizes:

| Millimetres | Inches |
| --- | --- |
| 102 × 152 | 4 × 6 |
| 102 × 127 | 4 × 5 |
| 102 × 114 | 4 × 4.5 |
| 102 × 102 | 4 × 4 |
| 102 × 76 | 4 × 3 |
| 102 × 50 | 4 × 2 |
| 102 × 38 | 4 × 1.5 |
| 102 × 25 | 4 × 1 |
| 76 × 127 | 3 × 5 |
| 76 × 100 | 3 × 3.94 |
| 76 × 76 | 3 × 3 |
| 76 × 51 | 3 × 2 |
| 76 × 38 | 3 × 1.5 |
| 58 × 100 | 2.28 × 3.94 |
| 51 × 76 | 2 × 3 |
| 39 × 100 | 1.54 × 3.94 |

### Label sizes on Android

Android's built-in print service and Mopria only display a fixed whitelist of
standard PWG media names. Unlike AirPrint, they ignore custom IPP sizes, so
the app also advertises standard sizes that fit inside the 4×6 roll. Depending
on the Android print service, the menu can include:

| PWG name | Size |
| --- | --- |
| `na_index-4x6_4x6in` | 4 × 6 in |
| `na_index-3x5_3x5in` | 3 × 5 in |
| `oe_square-photo_4x4in` | 4 × 4 in |
| `oe_photo-l_3.5x5in` | 3.5 × 5 in |
| `oe_business-card_2x3.5in` | 2 × 3.5 in |
| `om_card_54x86mm` | 54 × 86 mm |
| `om_dsc-photo_89x119mm` | 89 × 119 mm |
| `jpn_hagaki_100x148mm` | 100 × 148 mm |
| `om_small-photo_100x150mm` | 100 × 150 mm |
| `om_square-photo_89x89mm` | 3.5 × 3.5 in |
| `om_business-card_55x85mm` | 55 × 85 mm |
| `iso_a8_52x74mm` | A8 |
| `iso_a7_74x105mm` | A7 |
| `iso_a9_37x52mm` | A9 |
| `iso_a10_26x37mm` | A10 |

Which entries actually appear depends on the Android print service. Sizes
such as 4×5, 4×3, 3×3, and 39 mm labels are custom PWG sizes; Android's
built-in print dialog cannot select them. Use the iPhone/iPad, the CUPS
queue, or a third-party IPP client for those sizes.

The app supports additional sizes through its IPP interface and the CUPS
queue (for example 106 mm-wide media and A6 on 300 dpi models), but PAPPL can
advertise only 16 media sources for AirPrint.

If Android or iOS shows only one size, confirm the current binary is
installed and that the IPP media list is not stale:

```sh
PORT=$(cat /var/lib/brother-td-app/port)
avahi-browse -rt _universal._sub._ipp._tcp
ipptool -tv "ipp://localhost:$PORT/ipp/print/TD4210" \
    /usr/share/cups/ipptool/get-printer-attributes.test \
    | grep -E 'media-supported|media-ready'
```

The `media-ready` line lists the AirPrint virtual rolls. The
`media-supported` line includes the standard Android sizes. Toggle Wi-Fi on
the phone after restarting the app so it refreshes its cached printer
description.

### Garbled inch sizes in a CUPS dialog

CUPS and libppd localize standard inch media names with a UTF-8 double-prime,
for example `4 x 6″`, but write the PPD with `*LanguageEncoding: ISOLatin1`.
Programs that honor the PPD encoding then display the text as mojibake.

The current app serves an ASCII printer-strings catalog, so a newly generated
PPD uses `4 x 6 in`, `3 x 5 in`, and similar labels. To refresh an existing
queue, reinstall the app and recreate or modify the CUPS queue:

```sh
cd brother-td-linux
make pappl
sudo make install-pappl PREFIX=/usr
sudo systemctl daemon-reload
sudo systemctl restart brother-td-app
```

Then re-add the queue, or force CUPS to regenerate its PPD:

```sh
PORT=$(cat /var/lib/brother-td-app/port)
sudo lpadmin -p TD-4210D-2 -E \
    -v "ipp://localhost:$PORT/ipp/print/TD4210" -m everywhere
```

Alternatively, patch the old PPD in place:

```sh
sudo sed -i 's/″/ in/g' /etc/cups/ppd/TD-4210D-2.ppd
sudo systemctl reload cups
```

Verify the generated PPD:

```sh
PORT=$(cat /var/lib/brother-td-app/port)
driverless "ipp://localhost:$PORT/ipp/print/TD4210" \
    | grep '^\*PageSize 4x6'
```

It should print:

```text
*PageSize 4x6/4 x 6 in: ...
```

On Raspbian 12 with cups-filters 1.28, `driverless`/libppd does not support
`printer-strings-uri`; the generated PPD may still contain the built-in
`4 x 6″` text. Building and printing are unaffected. A PPD generated by the
CUPS queue itself, or a newer cups-filters/libppd, uses the ASCII labels.

## Printing options

Options can be passed per job with `lp -o NAME=VALUE`, set as queue defaults
with `lpadmin -o NAME=VALUE`, or selected in the CUPS web interface.

### Media

| Option | Values | Default | Description |
|---|---|---|---|
| `PageSize` | `102x152mm`, `102x127mm`, `76x127mm`, `76x26mm`, `51x26mm`, … | `102x152mm` | Physical label or page size. Custom sizes are supported. |
| `TDMediaType` | `DieCut`, `Continuous` | `DieCut` | Die-cut labels or continuous roll. |
| `TDModel` | model name | auto | Optional explicit model selection. When omitted, the filter infers the 203/300 dpi head from the raster and uses the 661-byte invalidate sequence (NUL is skipped, so it is safe for both generations). |
| `Resolution` | `203dpi`, `300dpi` | model-specific | Raster resolution. |

The page width is the physical media width. The filter maps it to Brother's
documented print-area width (for example 102 mm media has 788 printable pins
at 203 dpi) and centers it on the 832/1280-pin head.

**Important:** CUPS silently ignores a `PageSize` that is not one of the PPD
choices and falls back to the queue default (`102x152mm`). That can make a
small label print a 4x6 page and overprint into the label gap or the next
label. Use a listed size, or specify a custom size explicitly:

```sh
# 76 x 127 mm custom size, using the media option:
lp -d TD4210 -o media=Custom.76x127mm -o TDMediaType=DieCut label.pdf

# The same custom size can be given through PageSize:
lp -d TD4210 -o PageSize=Custom.76x127mm -o TDMediaType=DieCut label.pdf

# Custom values accept mm or in suffixes. Without a suffix CUPS treats them
# as PostScript points (1/72 inch), so this is 3 x 5 inches:
lp -d TD4210 -o PageSize=Custom.216x360 -o TDMediaType=DieCut label.pdf
```

### Image quality

| Option | Values | Default | Description |
|---|---|---|---|
| `ColorModel` | `Gray` | `Gray` | Requests 8-bit grayscale CUPS raster so the driver, not Ghostscript, controls halftoning. |
| `TDDither` | `Floyd`, `Jarvis`, `Stucki`, `Atkinson`, `Sierra`, `SierraLite`, `Burkes`, `Bayer4`, `Bayer8`, `Threshold` | `Floyd` | Monochrome conversion algorithm. |
| `TDThreshold` | `64`, `96`, `128`, `160`, `192` | `128` | Threshold (0–255) used by `Threshold` and as the midpoint for ordered dithering. |
| `TDScaling` | `Fit`, `FitEnlarge`, `Crop`, `None` | `Fit` | `Fit` shrinks to the printable area; `FitEnlarge` also enlarges; `Crop` fills and crops; `None` uses actual size. |
| `TDRotate` | `0`, `90`, `180`, `270` | `0` | Rotate the image before scaling. |
| `TDInvert` | `False`, `True` | `False` | Invert black and white. |
| `TDMirror` | `False`, `True` | `False` | Mirror the image horizontally. |
| `TDBorder` | `False`, `True` | `False` | Print the non-imageable side margins black. |

### Layout and finishing

| Option | Values | Default | Description |
|---|---|---|---|
| `TDAlignX` | `Left`, `Center`, `Right` | `Center` | Horizontal placement inside the printable area. |
| `TDAlignY` | `Top`, `Center`, `Bottom` | `Center` | Vertical placement inside the printable area. |
| `TDMargin` | `3`, `5`, `10`, `15`, `20`, `30` mm | `3` | Feed margin for continuous media. Ignored for die-cut labels. |
| `TDAutoTrim` | `False`, `True` | `True` | Trim blank leading/trailing rows on continuous media. |
| `TDAutoCut` | `False`, `True` | `False` | Enable the printer's auto-cutter (continuous media). |
| `TDCutAtEnd` | `False`, `True` | `True` | Cut at the end of the job when the cutter is enabled. |
| `TDPeeler` | `False`, `True` | `False` | Enable the peeler (continuous media). |
| `TDCompression` | `None`, `TIFF` | `TIFF` | `TIFF` uses PackBits compression per raster line. |

Die-cut labels always use a zero feed margin; the label pitch is controlled by
the printer and the selected page length. Auto-cut and peeler options are
disabled for die-cut media.

`TDAutoTrim` removes blank leading and trailing rows on continuous media, so it
takes precedence over `TDAlignY` there. Disable it if you need to preserve the
full selected page length. When `TDBorder` is enabled, auto-trim is skipped
because every row contains black margin pixels.

### Examples

4 × 6 inch die-cut label, 300 dpi model:

```sh
lp -d TD4520 \
   -o PageSize=102x152mm \
   -o TDMediaType=DieCut \
   -o TDDither=Floyd \
   -o TDScaling=Fit \
   invoice.pdf
```

Continuous 102 mm roll, 100 mm page, auto-trim, 10 mm feed:

```sh
lp -d TD4410 \
   -o PageSize=102x100mm \
   -o TDMediaType=Continuous \
   -o TDAutoTrim=True \
   -o TDMargin=10 \
   -o TDDither=Jarvis \
   shipping-label.pdf
```

Rotate a landscape image onto a portrait label:

```sh
lp -d TD4410 -o TDRotate=90 -o TDScaling=Fit photo.png
```

Print three copies of each page:

```sh
lp -d TD4410 -n 3 label.pdf
```

## Debugging

Enable filter diagnostics in the CUPS log:

```sh
sudo cupsctl --debug-logging
tail -f /var/log/cups/error_log
```

Set `BROTHER_TD_DEBUG=1` in the filter environment (for example in
`/etc/cups/cups-files.conf`, then restart CUPS) to get detailed geometry and
option logs. To capture the exact printer byte stream, also set a writable
directory:

```sh
# /etc/cups/cups-files.conf
SetEnv BROTHER_TD_DEBUG 1
SetEnv BROTHER_TD_DEBUG_DIR /var/spool/cups/td-debug
```

The filter writes `job-<id>.prn` for each job. The directory must be writable
by the CUPS filter user (usually `lp`).

Common checks:

```sh
lpstat -t
lpinfo -v
lpstat -l -p TD4410
```

- **Nothing appears**: verify the queue is enabled (`lpstat -p`), the backend
  is correct (`lpstat -v`), and the printer is online.
- **Blank labels**: check `TDMediaType`, the page size, and whether the image
  is white. Try `TDDither=Threshold`.
- **Overprinting into the gap or next label**: CUPS silently falls back to
  the queue default when `PageSize` is not one of the PPD choices. Check the
  available sizes with `lpoptions -p QUEUE -l | grep PageSize`, or use a
  custom size such as `-o media=Custom.76x127mm`. After a driver upgrade,
  refresh the queue's PPD with `lpadmin -P`.
- **Output is cut off**: choose the correct `PageSize`; the driver uses
  Brother's print-area width, not the full media width. Try
  `TDScaling=Fit` or `Crop`.
- **Garbled output on a 300 dpi model**: make sure the queue uses a 300 dpi
  PPD (`TD-4510D`, `TD-4520DN`, `TD-4550DNWB`, or a 4x25/4x55 model). The
  original utility had a hard-coded 203 dpi raster length bug; this driver
  computes 104 or 160 bytes per line correctly.
- **USB printer not found**: check `lpinfo -v`, the USB cable, and the CUPS
  `usb` backend. Do not run a separate program that claims the USB interface
  at the same time.
- **Network model not found**: verify TCP port 9100 is reachable and use
  `socket://address:9100`.

### CUPS 3

CUPS 3 deprecates PPD-based drivers in favour of IPP Everywhere. This driver
targets the widely deployed CUPS 2.2/2.4 PPD driver model. On distributions
that ship CUPS 3, enable the legacy PPD/driver support or use CUPS 2.4.

## Limitations

- The filter is unidirectional. It does not query ink/media status or report
  supply levels; the CUPS backend handles device availability and its own
  status query. The TD-4D raster command set also has no density or speed
  command, so those front-panel settings are not exposed as job options.
  The PAPPL application likewise reports a healthy status but no supply
  levels, printer icons, or hardware self-test page.
- The driver targets the TD-4D raster command language. It is not a ZPL,
  ESC/P, or P-touch Template driver.
- The test suite is synthetic. It verifies the command stream byte-for-byte
  and runs a real Ghostscript raster chain, but it cannot replace a smoke test
  on each physical model and media combination.
- CUPS 3 deprecates PPD-based drivers. Use the PAPPL application above for a
  driverless queue; the PPD/filter driver remains for CUPS 2.x.

## Development

```sh
make            # build filter and PPDs
make check      # unit tests + PPD validation + end-to-end synthetic raster tests
make regen      # regenerate ppd/*.ppd and src/td_models.c from models.json
make dist       # source tarball
```

The source layout:

```text
include/td_*.h        public/internal interfaces
src/td_media.c        media geometry and Brother print-area tables
src/td_raster.c       CUPS raster input and colorspace conversion
src/td_image.c        grayscale image rotation, scaling, placement, bitmaps
src/td_dither.c       error-diffusion and ordered dithering
src/td_packbits.c     TIFF/PackBits codec
src/td_protocol.c     Brother TD command stream builder
src/brother-td-filter.c  CUPS filter entry point
src/td_job.c          shared page/copy/job pipeline
pappl/brother-td-app.c  PAPPL/IPP Everywhere printer application
tools/generate.py     models.json -> PPDs + C model table
tests/                unit and integration tests
docs/protocol.md      protocol notes and differences from the original
```

To add a printer, add an entry to `models.json`, run `make regen`, and rebuild.
If the model uses a different command generation, update `src/td_protocol.c`.

## Protocol notes

The driver follows Brother's *TD-4D Raster Command Reference* (document
`cv_td4000d_eng_raster_103.pdf`). See [`docs/protocol.md`](docs/protocol.md)
for the command sequence and the fixes relative to the original Perl script.

## Related projects

- [`scne59/brother-td`](https://github.com/scne59/brother-td) — the original
  Perl command-line utility and protocol research.
- [`scne59/brother-td-linux`](https://github.com/scne59/brother-td-linux) —
  this CUPS/PAPPL implementation.

## License

MIT. The original `brother-td.pl` is Copyright © 2025 Jochen Schneider.
This driver is a derivative work; see [`LICENSE`](LICENSE) and
[`NOTICE`](NOTICE).
