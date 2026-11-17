#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 brother-td-cups contributors

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ppd_dir="$root/ppd"
fail=0

if ! command -v cupstestppd >/dev/null 2>&1; then
    echo "check-ppd: cupstestppd not found; skipping (install cups-ppdc)"
    exit 0
fi

for ppd in "$ppd_dir"/*.ppd; do
    if ! LC_ALL=C cupstestppd -q -W filters "$ppd"; then
        echo "check-ppd: FAILED: $ppd" >&2
        fail=1
    fi

    grep -q '^\*cupsFilter: "application/vnd.cups-raster 0 brother-td-filter"' "$ppd" || {
        echo "check-ppd: missing cupsFilter in $ppd" >&2
        fail=1
    }
    grep -q '^\*DefaultTDModel: TD-' "$ppd" || {
        echo "check-ppd: missing DefaultTDModel in $ppd" >&2
        fail=1
    }
    grep -q '^\*DefaultPageSize: 102x152mm' "$ppd" || {
        echo "check-ppd: missing default page size in $ppd" >&2
        fail=1
    }
    grep -q '^\*PageSize 76x127mm' "$ppd" || {
        echo "check-ppd: missing 76x127mm page size in $ppd" >&2
        fail=1
    }
    grep -q '^\*PageSize 102x152mm' "$ppd" || {
        echo "check-ppd: missing 102x152mm page size in $ppd" >&2
        fail=1
    }
done

count=$(find "$ppd_dir" -name '*.ppd' | wc -l)
echo "check-ppd: validated $count PPD file(s)"
exit "$fail"
