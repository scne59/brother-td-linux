/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 *
 * Node tests for the browser driver port.  Run with:
 *   node --test tests/web-driver.test.mjs
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    Bitmap,
    GrayImage,
    buildJobStream,
    ditherImage,
    emitLine,
    geometryFor,
    modelById,
    normalizeOrientation,
    packBitsEncode,
    preparePage,
    printableWidthPx,
    rotateGray,
} from '../web/driver.js';

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));

function asBytes(buffer) {
    return Array.from(buffer);
}

test('geometry matches the C driver', () => {
    const model = modelById('TD-4210D');
    assert.ok(model);

    const geometry = geometryFor(model, 'diecut', 101.6, 152.4);
    assert.equal(geometry.dpi, 203);
    assert.equal(geometry.headPins, 832);
    assert.equal(geometry.headBytes, 104);
    assert.equal(geometry.printWidthPx, 788);
    assert.equal(geometry.printHeightPx, 1170);
    assert.equal(geometry.leftOffsetPx, 22);
    assert.equal(geometry.rightOffsetPx, 22);
    assert.equal(geometry.mediaWidthRound, 102);
    assert.equal(geometry.mediaLengthRound, 152);

    const continuous = geometryFor(model, 'continuous', 102, 100);
    assert.equal(continuous.mediaLengthRound, 0);
    assert.equal(continuous.printHeightPx, 799);

    const model300 = modelById('TD-4510D');
    const geometry300 = geometryFor(model300, 'diecut', 102, 152.4);
    assert.equal(geometry300.headPins, 1280);
    assert.equal(geometry300.headBytes, 160);
    assert.equal(geometry300.printWidthPx, 1164);
    assert.equal(geometry300.leftOffsetPx, 58);
});

test('printable width table and fallback', () => {
    assert.equal(printableWidthPx(203, 39), 288);
    assert.equal(printableWidthPx(203, 76), 583);
    assert.equal(printableWidthPx(203, 102), 788);
    assert.equal(printableWidthPx(203, 106), 823);
    assert.equal(printableWidthPx(300, 102), 1164);
    assert.equal(printableWidthPx(203, 100), 775);
});

test('landscape media is transposed, portrait media is not', () => {
    const model = modelById('TD-4210D');

    let result = normalizeOrientation(model, 152, 102);
    assert.deepEqual(result, { widthMm: 102, lengthMm: 152, rotated: true });

    result = normalizeOrientation(model, 102, 152);
    assert.deepEqual(result, { widthMm: 102, lengthMm: 152, rotated: false });

    result = normalizeOrientation(model, 152, 152);
    assert.deepEqual(result, { widthMm: 152, lengthMm: 152, rotated: false });
});

test('rotation matches the C implementation', () => {
    const image = new GrayImage(2, 3, new Uint8Array([
        1, 2,
        3, 4,
        5, 6,
    ]));

    assert.deepEqual(asBytes(rotateGray(image, 90).pixels), [5, 3, 1, 6, 4, 2]);
    assert.deepEqual(asBytes(rotateGray(image, 180).pixels), [6, 5, 4, 3, 2, 1]);
    assert.deepEqual(asBytes(rotateGray(image, 270).pixels), [2, 4, 6, 1, 3, 5]);
});

test('PackBits encoder matches the C implementation', () => {
    assert.deepEqual(asBytes(packBitsEncode(Uint8Array.of(0, 0, 0))), [0xfe, 0x00]);
    assert.deepEqual(asBytes(packBitsEncode(Uint8Array.of(1, 2, 3))), [0x02, 1, 2, 3]);
    assert.deepEqual(asBytes(packBitsEncode(Uint8Array.of(5, 5, 6, 7))),
                     [0xff, 5, 0x01, 6, 7]);

    const run128 = new Uint8Array(128).fill(0xab);
    assert.deepEqual(asBytes(packBitsEncode(run128)), [0x81, 0xab]);

    const run200 = new Uint8Array(200).fill(0xab);
    assert.deepEqual(asBytes(packBitsEncode(run200)), [0x81, 0xab, 0xb9, 0xab]);
});

test('dithering fixtures match the C implementation', () => {
    const gradient = new GrayImage(8, 8,
        new Uint8Array(Array.from({ length: 64 }, (_, index) => index * 4)));
    const floyd = ditherImage(gradient, 'floyd', 128);

    assert.deepEqual(asBytes(floyd.bits), [0xff, 0xfe, 0xab, 0xd4, 0x55, 0x20, 0x44, 0x00]);

    const flat = new GrayImage(8, 8, new Uint8Array(64).fill(120));
    const bayer = ditherImage(flat, 'bayer8', 128);

    assert.deepEqual(asBytes(bayer.bits), [0x55, 0xaa, 0x55, 0xae, 0x55, 0xaa, 0x55, 0xea]);
});

test('line emission handles blank lines, raw lines and compression', () => {
    assert.deepEqual(asBytes(emitLine(new Uint8Array(104), 'tiff')), [0x5a]);

    const black = new Uint8Array(104).fill(0xff);
    assert.deepEqual(asBytes(emitLine(black, 'tiff')), [0x67, 0x00, 0x02, 0x99, 0xff]);
    assert.deepEqual(asBytes(emitLine(black, 'none')), [0x67, 0x00, 104, ...black]);
});

function decodePackBits(payload) {
    const out = [];
    let input = 0;

    while (input < payload.length) {
        const control = payload[input++];

        if (control & 0x80) {
            const count = 257 - control;
            const value = payload[input++];
            for (let i = 0; i < count; i++)
                out.push(value);
        } else {
            const count = control + 1;
            for (let i = 0; i < count; i++)
                out.push(payload[input++]);
        }
    }

    return out;
}

function parseStream(data, headBytes) {
    let offset = 0;
    let zeros = 0;

    while (offset < data.length && data[offset] === 0)
        offset++, zeros++;

    assert.ok(zeros === 350 || zeros === 661, `unexpected invalidate length ${zeros}`);
    assert.deepEqual(Array.from(data.subarray(offset, offset + 2)), [0x1b, 0x40]);
    offset += 2;

    const pages = [];

    while (offset < data.length) {
        if (data[offset] === 0x1b && data[offset + 1] === 0x69 &&
            data[offset + 2] === 0x61 && data[offset + 3] === 0xff) {
            offset += 4;
            assert.equal(offset, data.length, 'trailing data after mode reset');
            break;
        }

        assert.deepEqual(Array.from(data.subarray(offset, offset + 4)), [0x1b, 0x69, 0x61, 0x01]);
        offset += 4;
        assert.deepEqual(Array.from(data.subarray(offset, offset + 4)), [0x1b, 0x69, 0x21, 0x00]);
        offset += 4;
        assert.deepEqual(Array.from(data.subarray(offset, offset + 3)), [0x1b, 0x69, 0x7a]);
        offset += 3;

        const info = data.subarray(offset, offset + 10);
        offset += 10;

        assert.deepEqual(Array.from(data.subarray(offset, offset + 3)), [0x1b, 0x69, 0x4d]);
        offset += 3;
        const flags = data[offset++];

        assert.deepEqual(Array.from(data.subarray(offset, offset + 3)), [0x1b, 0x69, 0x64]);
        offset += 3;
        const margin = data[offset] | (data[offset + 1] << 8);
        offset += 2;

        assert.deepEqual(Array.from(data.subarray(offset, offset + 3)), [0x1b, 0x69, 0x4b]);
        offset += 3;
        const expanded = data[offset++];

        assert.equal(data[offset++], 0x4d);
        const compression = data[offset++];

        const lines = [];
        let zeroLines = 0;
        let terminator = null;

        while (offset < data.length) {
            const command = data[offset++];

            if (command === 0x0c || command === 0x1a) {
                terminator = command;
                break;
            }

            if (command === 0x5a) {
                lines.push(new Uint8Array(headBytes));
                zeroLines++;
                continue;
            }

            assert.equal(command, 0x67);
            assert.equal(data[offset++], 0x00);
            const length = data[offset++];
            const payload = data.subarray(offset, offset + length);
            offset += length;

            let line;

            if (compression === 0) {
                assert.equal(length, headBytes);
                line = payload;
            } else if (length === headBytes + 1 && payload[0] === headBytes) {
                line = payload.subarray(1);
            } else {
                line = Uint8Array.from(decodePackBits(payload));
            }

            assert.equal(line.length, headBytes);
            lines.push(line);
        }

        assert.ok(terminator === 0x0c || terminator === 0x1a);
        pages.push({ info, flags, margin, expanded, compression, lines, zeroLines, terminator });
    }

    return pages;
}

test('a prepared page produces a valid printer stream', () => {
    const model = modelById('TD-4210D');
    const geometry = geometryFor(model, 'diecut', 101.6, 152.4);
    const source = new GrayImage(600, 300, new Uint8Array(600 * 300).fill(255));

    for (let y = 100; y < 200; y++) {
        for (let x = 100; x < 500; x++)
            source.pixels[y * 600 + x] = 0;
    }

    const bitmap = preparePage(source, geometry, { scaling: 'fit' });
    assert.equal(bitmap.width, 788);
    assert.equal(bitmap.height, 1170);

    const stream = buildJobStream({ model, geometry, bitmaps: [bitmap] });
    const pages = parseStream(stream, geometry.headBytes);

    assert.equal(pages.length, 1);
    assert.equal(pages[0].lines.length, 1170);
    assert.equal(pages[0].info[0], 0x8e);
    assert.equal(pages[0].info[1], 0x0b);
    assert.equal(pages[0].info[2], 102);
    assert.equal(pages[0].info[3], 152);
    assert.equal(pages[0].info[8], 0);
    assert.equal(pages[0].terminator, 0x1a);
    assert.equal(pages[0].expanded, 0x08);
    assert.ok(pages[0].lines.some((line) => line.some((byte) => byte !== 0)));
});

test('copies and raw compression produce valid pages', () => {
    const model = modelById('TD-4210D');
    const geometry = geometryFor(model, 'diecut', 102, 127);
    const source = new GrayImage(100, 100, new Uint8Array(100 * 100).fill(255));
    const bitmap = preparePage(source, geometry, { scaling: 'fit' });
    const stream = buildJobStream({
        model,
        geometry,
        bitmaps: [bitmap],
        copies: 2,
        options: { compression: 'none' },
    });
    const pages = parseStream(stream, geometry.headBytes);

    assert.equal(pages.length, 2);
    assert.equal(pages[0].info[8], 0);
    assert.equal(pages[1].info[8], 1);
    assert.equal(pages[0].terminator, 0x0c);
    assert.equal(pages[1].terminator, 0x1a);
    assert.equal(pages[0].compression, 0);
    assert.equal(pages[0].lines.length, 967);
});

test('stream is accepted by the CUPS raster test parser', (t) => {
    const python = spawnSync('python3', ['--version'], { encoding: 'utf8' });

    if (python.error || python.status !== 0) {
        t.skip('python3 is not available');
        return;
    }

    const model = modelById('TD-4210D');
    const geometry = geometryFor(model, 'diecut', 101.6, 152.4);
    const source = new GrayImage(400, 200, new Uint8Array(400 * 200).fill(255));

    for (let y = 50; y < 150; y++) {
        for (let x = 50; x < 350; x++)
            source.pixels[y * 400 + x] = 0;
    }

    const bitmap = preparePage(source, geometry, { scaling: 'fit' });
    const stream = buildJobStream({ model, geometry, bitmaps: [bitmap] });
    const directory = mkdtempSync(join(tmpdir(), 'td-web-'));
    const filename = join(directory, 'job.prn');

    writeFileSync(filename, stream);

    const code = [
        'import sys',
        "sys.path.insert(0, 'tests')",
        'import integration',
        "data = open(sys.argv[1], 'rb').read()",
        'pages = integration.parse_stream(data, 104)',
        'assert len(pages) == 1, len(pages)',
        'assert pages[0]["info"][2] == 102, pages[0]["info"]',
        'assert pages[0]["info"][3] == 152, pages[0]["info"]',
        'assert len(pages[0]["lines"]) == 1170, len(pages[0]["lines"])',
        'assert pages[0]["terminator"] == 0x1a',
        "print('ok')",
    ].join('\n');

    const result = spawnSync('python3', ['-c', code, filename], {
        cwd: ROOT,
        encoding: 'utf8',
        env: {
            ...process.env,
            BROTHER_TD_FILTER: join(ROOT, 'build', 'brother-td-filter'),
            BROTHER_TD_RASTER: join(ROOT, 'build', 'make_raster'),
        },
    });

    rmSync(directory, { recursive: true, force: true });

    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
    assert.match(result.stdout, /ok/);
});
