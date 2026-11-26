# Brother TD-4D Web Print (WebUSB)

A static, serverless web app that renders PDF and image documents in the
browser and sends the Brother raster command stream directly to a TD-4D
label printer over USB.

Everything runs client-side: there is no CUPS queue, no printer application,
no backend and no upload to a server.  The driver logic is a JavaScript port
of `src/td_media.c`, `src/td_image.c`, `src/td_dither.c`,
`src/td_packbits.c` and `src/td_protocol.c`.

## Requirements

- **Chrome or Edge** (WebUSB is not available in Firefox or Safari).
- A **secure context**: `https://` or `http://localhost`.
- The printer's USB interface must not be claimed by another process.
- Permission to open the USB device (usually membership in the `lp` group).

## Quick start

```sh
cd web
python3 -m http.server 8080 --bind 127.0.0.1
```

Open <http://localhost:8080/>, connect the printer, choose a document, press
**Generate preview** and then **Print**.

The server must send `.mjs` files as `text/javascript`; Python's
`http.server` and common web servers do this.  If the page stays empty,
check the browser console for a module MIME-type error.

To use the page from another machine, serve it over HTTPS or put it behind a
TLS reverse proxy.  A plain `http://` LAN address is not a secure context and
WebUSB will be unavailable.

### Stop CUPS / PAPPL while printing

The USB interface can only be claimed by one driver at a time.  Stop the
printer application and, if necessary, unload the kernel printer driver:

```sh
sudo systemctl stop brother-td-app
sudo modprobe -r usblp
```

CUPS itself only opens the USB device while a job is running, but the `usblp`
kernel module keeps the interface bound.  Chrome can often detach it, but
unloading it is the reliable option.  Reload it when you go back to CUPS:

```sh
sudo modprobe usblp
sudo systemctl start brother-td-app
```

### USB permissions

If Chrome reports "Access denied" when claiming the interface, add a udev
rule for Brother devices:

```udev
# /etc/udev/rules.d/99-brother-td.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="04f9", MODE="0664", GROUP="lp"
```

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then unplug and replug the printer.

## Supported input

- PDF (rendered with the bundled pdf.js)
- PNG, JPEG and WebP images

PDF pages are rendered at the selected model's resolution (203 or 300 dpi)
and then scaled into the printable area.  Landscape pages are rotated 90°
clockwise onto portrait media by default.

## Options

| Option | Default | Description |
| --- | --- | --- |
| Model | TD-4210D | Selects the resolution, head width and USB product ID |
| Media size | 4 × 6 in | Common label sizes or a custom width/length in mm |
| Media type | Die-cut | Die-cut labels or continuous roll |
| Copies | 1 | Repeats every page |
| Scaling | Fit | Fit, fit and enlarge, fill/crop, or actual size |
| Rotation | Auto | Auto, 0°, 90°, 180°, 270° |
| Dithering | Floyd–Steinberg | All algorithms from the CUPS driver, plus ordered/threshold |
| Compression | TIFF/PackBits | Or uncompressed raster lines |
| Threshold | 128 | Threshold for the dithering algorithms |
| Black side margins | off | Marks the non-printable pins black |
| Invert | off | Inverts the bitmap |
| Cut at end | on | Sets the cut-at-end flag in the command stream |

The **Download .prn** button writes the exact byte stream that would be sent
over USB.  This is useful for debugging and as a fallback:

```sh
# USB queue using the distro ptouch driver, raw mode
lp -d TD-4210D-2 -o raw label.prn

# or directly through the kernel printer device
cat label.prn > /dev/usb/lp0
```

## Limits

- WebUSB is Chromium-only and requires a secure context.
- The printer must be free; CUPS/PAPPL and this page cannot use it at the
  same time.
- PDF and raster images only.  PostScript is not interpreted in the browser.
- Documents are limited to 500 pages.  Very large pages are rendered at a
  reduced scale to stay within browser canvas limits.
- There is no bidirectional status channel; the app sends the same
  status-off command sequence as the CUPS driver.

## Tests

The driver port is covered by Node tests, including fixtures from the C
implementation and validation with the CUPS raster test parser:

```sh
make check-web
# or
node --test tests/web-driver.test.mjs
```

The tests are skipped automatically when Node.js is not installed.

## Third-party code

`vendor/pdfjs/` contains [pdf.js](https://mozilla.github.io/pdf.js/) 4.10.38
(Apache-2.0, see `vendor/pdfjs/LICENSE`).  It is bundled so the app works
offline and without a CDN.
