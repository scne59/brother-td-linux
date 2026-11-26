/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 *
 * Browser port of the brother-td-linux raster and command pipeline.
 *
 * This module has no browser dependencies so it can be unit tested with Node.
 * It mirrors src/td_media.c, src/td_image.c, src/td_dither.c,
 * src/td_packbits.c and src/td_protocol.c.
 */

export const MM_PER_INCH = 25.4;
export const DIECUT_END_OFFSET_MM = 6.0;
export const MIN_PRINT_WIDTH_PX = 8;
export const MAX_MEDIA_LENGTH_MM = 3000;
export const MIN_MEDIA_LENGTH_MM = 12;

export const MODELS = [
    { id: 'TD-4210D',     dpi: 203, headPins: 832,  initNuls: 350, maxWidthMm: 104, usbVid: 0x04f9, usbPid: 0x20f2 },
    { id: 'TD-4410D',     dpi: 203, headPins: 832,  initNuls: 350, maxWidthMm: 104, usbVid: 0x04f9, usbPid: 0x20b6 },
    { id: 'TD-4420DN',    dpi: 203, headPins: 832,  initNuls: 350, maxWidthMm: 104, usbVid: 0x04f9, usbPid: 0x20b7 },
    { id: 'TD-4510D',     dpi: 300, headPins: 1280, initNuls: 350, maxWidthMm: 108, usbVid: 0x04f9, usbPid: 0x20b8 },
    { id: 'TD-4520DN',    dpi: 300, headPins: 1280, initNuls: 350, maxWidthMm: 108, usbVid: 0x04f9, usbPid: 0x20b9 },
    { id: 'TD-4550DNWB',  dpi: 300, headPins: 1280, initNuls: 350, maxWidthMm: 108, usbVid: 0x04f9, usbPid: 0x20ba },
    { id: 'TD-4215D',     dpi: 203, headPins: 832,  initNuls: 661, maxWidthMm: 104, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4415D',     dpi: 203, headPins: 832,  initNuls: 661, maxWidthMm: 104, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4425DN',    dpi: 203, headPins: 832,  initNuls: 661, maxWidthMm: 104, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4455DNWB',  dpi: 203, headPins: 832,  initNuls: 661, maxWidthMm: 104, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4525DN',    dpi: 300, headPins: 1280, initNuls: 661, maxWidthMm: 108, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4555DNWB',  dpi: 300, headPins: 1280, initNuls: 661, maxWidthMm: 108, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4425DNF',   dpi: 203, headPins: 832,  initNuls: 661, maxWidthMm: 106, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4555DNWBF', dpi: 300, headPins: 1280, initNuls: 661, maxWidthMm: 108, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4420DNFC',  dpi: 203, headPins: 832,  initNuls: 350, maxWidthMm: 106, usbVid: 0,      usbPid: 0 },
    { id: 'TD-4550DNWBFC',dpi: 300, headPins: 1280, initNuls: 350, maxWidthMm: 108, usbVid: 0,      usbPid: 0 },
];

export function modelById(id) {
    return MODELS.find((model) => model.id === id) || null;
}

export function modelForUsbPid(pid) {
    return MODELS.find((model) => model.usbPid === pid) || null;
}

const WIDTHS_203 = new Map([
    [39, 288], [51, 382], [58, 440], [76, 583],
    [80, 615], [90, 695], [102, 788], [106, 823],
]);

const WIDTHS_300 = new Map([
    [39, 425], [51, 564], [58, 649], [76, 861],
    [80, 909], [90, 1027], [102, 1164], [106, 1216],
]);

export function printableWidthPx(dpi, widthMm) {
    if (!(widthMm > 0))
        return MIN_PRINT_WIDTH_PX;

    const table = dpi === 203 ? WIDTHS_203 : dpi === 300 ? WIDTHS_300 : null;
    const rounded = Math.round(widthMm);

    if (table && table.has(rounded))
        return table.get(rounded);

    const fallback = Math.round((widthMm - 3) * dpi / MM_PER_INCH);
    return Math.max(MIN_PRINT_WIDTH_PX, fallback);
}

export function clamp(value, minimum, maximum) {
    return Math.min(maximum, Math.max(minimum, value));
}

export function geometryFor(model, mediaType, widthMm, lengthMm) {
    let width = Number(widthMm);
    let length = Number(lengthMm);

    if (!Number.isFinite(width) || width <= 0)
        return null;
    if (!Number.isFinite(length) || length <= 0)
        return null;

    width = clamp(width, 19, model.maxWidthMm);
    length = clamp(length, MIN_MEDIA_LENGTH_MM, MAX_MEDIA_LENGTH_MM);

    const headPins = model.headPins;
    const headBytes = (headPins + 7) >> 3;
    const dpi = model.dpi;

    let mediaLengthRound;
    let printHeightPx;

    if (mediaType === 'continuous') {
        mediaLengthRound = 0;
        printHeightPx = Math.round(length * dpi / MM_PER_INCH);
    } else {
        mediaLengthRound = clamp(Math.round(length), 1, 255);
        const printableLength = Math.max(length - DIECUT_END_OFFSET_MM, 1);
        printHeightPx = Math.round(printableLength * dpi / MM_PER_INCH);
    }

    if (printHeightPx < 1)
        printHeightPx = 1;

    let printWidthPx = printableWidthPx(dpi, width);
    if (printWidthPx > headPins - 2)
        printWidthPx = headPins - 2;
    if (printWidthPx < MIN_PRINT_WIDTH_PX)
        printWidthPx = MIN_PRINT_WIDTH_PX;

    const leftOffsetPx = Math.floor((headPins - printWidthPx) / 2);

    return {
        modelId: model.id,
        dpi,
        headPins,
        headBytes,
        type: mediaType,
        mediaWidthMm: width,
        mediaLengthMm: length,
        mediaWidthRound: clamp(Math.round(width), 1, 255),
        mediaLengthRound,
        printWidthPx,
        printHeightPx,
        leftOffsetPx,
        rightOffsetPx: headPins - printWidthPx - leftOffsetPx,
    };
}

/* Mirrors td_media_normalize_orientation(): a landscape page description of a
 * portrait label is transposed.  The caller must rotate the raster by 90
 * degrees when `rotated` is true. */
export function normalizeOrientation(model, widthMm, lengthMm) {
    if (widthMm <= model.maxWidthMm || lengthMm > model.maxWidthMm)
        return { widthMm, lengthMm, rotated: false };
    return { widthMm: lengthMm, lengthMm: widthMm, rotated: true };
}

export class GrayImage {
    constructor(width, height, pixels) {
        this.width = width;
        this.height = height;
        this.pixels = pixels || new Uint8Array(width * height).fill(255);
    }

    clone() {
        return new GrayImage(this.width, this.height, new Uint8Array(this.pixels));
    }
}

/* Convert RGBA (as returned by canvas getImageData) to 8-bit grayscale,
 * compositing transparent pixels over white first. */
export function grayFromRGBA(rgba, width, height) {
    const pixels = new Uint8Array(width * height);

    for (let i = 0, p = 0; i < pixels.length; i++, p += 4) {
        const alpha = rgba[p + 3];
        let red = rgba[p];
        let green = rgba[p + 1];
        let blue = rgba[p + 2];

        if (alpha !== 255) {
            red = Math.round((red * alpha + 255 * (255 - alpha)) / 255);
            green = Math.round((green * alpha + 255 * (255 - alpha)) / 255);
            blue = Math.round((blue * alpha + 255 * (255 - alpha)) / 255);
        }

        pixels[i] = (299 * red + 587 * green + 114 * blue + 500) / 1000 | 0;
    }

    return new GrayImage(width, height, pixels);
}

/* Positive angles rotate clockwise, matching td_gray_rotate(). */
export function rotateGray(source, degrees) {
    let angle = ((degrees % 360) + 360) % 360;

    if (angle === 0)
        return source.clone();
    if (angle !== 90 && angle !== 180 && angle !== 270)
        throw new Error(`unsupported rotation ${degrees}`);

    const { width, height, pixels } = source;
    const swap = angle === 90 || angle === 270;
    const outWidth = swap ? height : width;
    const outHeight = swap ? width : height;
    const out = new Uint8Array(outWidth * outHeight);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            let dx;
            let dy;

            if (angle === 90) {
                dx = height - 1 - y;
                dy = x;
            } else if (angle === 180) {
                dx = width - 1 - x;
                dy = height - 1 - y;
            } else {
                dx = y;
                dy = width - 1 - x;
            }

            out[dy * outWidth + dx] = pixels[y * width + x];
        }
    }

    return new GrayImage(outWidth, outHeight, out);
}

export function mirrorGray(source) {
    const { width, height, pixels } = source;
    const out = new Uint8Array(pixels.length);

    for (let y = 0; y < height; y++) {
        const row = y * width;

        for (let x = 0; x < width; x++)
            out[row + x] = pixels[row + width - 1 - x];
    }

    return new GrayImage(width, height, out);
}

function resizeArea(source, dstWidth, dstHeight) {
    const out = new Uint8Array(dstWidth * dstHeight).fill(255);
    const { width, height, pixels } = source;

    for (let y = 0; y < dstHeight; y++) {
        const sy0 = y * height / dstHeight;
        const sy1 = (y + 1) * height / dstHeight;
        let iy0 = Math.floor(sy0);
        let iy1 = Math.ceil(sy1);

        iy0 = Math.max(0, iy0);
        iy1 = Math.min(height, iy1);
        if (iy1 <= iy0)
            iy1 = iy0 + 1;
        if (iy1 > height)
            iy1 = height;

        for (let x = 0; x < dstWidth; x++) {
            const sx0 = x * width / dstWidth;
            const sx1 = (x + 1) * width / dstWidth;
            let ix0 = Math.floor(sx0);
            let ix1 = Math.ceil(sx1);

            ix0 = Math.max(0, ix0);
            ix1 = Math.min(width, ix1);
            if (ix1 <= ix0)
                ix1 = ix0 + 1;
            if (ix1 > width)
                ix1 = width;

            let sum = 0;
            let count = 0;

            for (let iy = iy0; iy < iy1; iy++) {
                const row = iy * width;

                for (let ix = ix0; ix < ix1; ix++) {
                    sum += pixels[row + ix];
                    count++;
                }
            }

            out[y * dstWidth + x] = count ? Math.floor((sum + count / 2) / count) : 255;
        }
    }

    return new GrayImage(dstWidth, dstHeight, out);
}

function resizeBilinear(source, dstWidth, dstHeight) {
    const out = new Uint8Array(dstWidth * dstHeight).fill(255);
    const { width, height, pixels } = source;

    for (let y = 0; y < dstHeight; y++) {
        let sy = (y + 0.5) * height / dstHeight - 0.5;

        if (sy < 0)
            sy = 0;
        if (sy > height - 1)
            sy = height - 1;

        const y0 = Math.floor(sy);
        let y1 = y0 + 1;
        if (y1 >= height)
            y1 = height - 1;
        const fy = sy - y0;

        for (let x = 0; x < dstWidth; x++) {
            let sx = (x + 0.5) * width / dstWidth - 0.5;

            if (sx < 0)
                sx = 0;
            if (sx > width - 1)
                sx = width - 1;

            const x0 = Math.floor(sx);
            let x1 = x0 + 1;
            if (x1 >= width)
                x1 = width - 1;
            const fx = sx - x0;

            const top = (1 - fx) * pixels[y0 * width + x0] + fx * pixels[y0 * width + x1];
            const bottom = (1 - fx) * pixels[y1 * width + x0] + fx * pixels[y1 * width + x1];
            const value = (1 - fy) * top + fy * bottom;

            out[y * dstWidth + x] = Math.round(clamp(value, 0, 255));
        }
    }

    return new GrayImage(dstWidth, dstHeight, out);
}

export function resizeGray(source, dstWidth, dstHeight) {
    if (source.width === dstWidth && source.height === dstHeight)
        return source.clone();
    if (dstWidth <= 0 || dstHeight <= 0)
        return new GrayImage(0, 0, new Uint8Array(0));
    if (dstWidth * 2 <= source.width || dstHeight * 2 <= source.height)
        return resizeArea(source, dstWidth, dstHeight);
    return resizeBilinear(source, dstWidth, dstHeight);
}

function alignOffset(target, item, alignment) {
    const slack = target - item;

    if (alignment === 'start')
        return 0;
    if (alignment === 'end')
        return slack;
    return Math.trunc(slack / 2);
}

export function placeGray(source, targetWidth, targetHeight, scaling = 'fit',
                          alignX = 'center', alignY = 'center') {
    let scale = 1;

    if (scaling === 'crop') {
        scale = Math.max(targetWidth / source.width, targetHeight / source.height);
    } else if (scaling === 'fitenlarge') {
        scale = Math.min(targetWidth / source.width, targetHeight / source.height);
    } else if (scaling === 'none') {
        scale = 1;
    } else {
        scale = Math.min(targetWidth / source.width, targetHeight / source.height);
        if (scale > 1)
            scale = 1;
    }

    if (!Number.isFinite(scale) || scale <= 0)
        throw new Error('invalid scale');

    let scaledWidth = Math.round(source.width * scale);
    let scaledHeight = Math.round(source.height * scale);
    if (scaledWidth < 1)
        scaledWidth = 1;
    if (scaledHeight < 1)
        scaledHeight = 1;

    const scaled = (scaledWidth === source.width && scaledHeight === source.height)
        ? source : resizeGray(source, scaledWidth, scaledHeight);

    const offsetX = alignOffset(targetWidth, scaled.width, alignX);
    const offsetY = alignOffset(targetHeight, scaled.height, alignY);
    const out = new GrayImage(targetWidth, targetHeight);

    const copyX0 = Math.max(0, offsetX);
    const copyY0 = Math.max(0, offsetY);
    let copyX1 = offsetX + scaled.width;
    let copyY1 = offsetY + scaled.height;

    if (copyX1 > targetWidth)
        copyX1 = targetWidth;
    if (copyY1 > targetHeight)
        copyY1 = targetHeight;

    for (let y = copyY0; y < copyY1; y++) {
        const sourceY = y - offsetY;
        const sourceX = copyX0 - offsetX;
        let count = copyX1 - copyX0;

        if (sourceY < 0 || sourceY >= scaled.height || sourceX < 0 || count <= 0)
            continue;
        if (sourceX + count > scaled.width)
            count = scaled.width - sourceX;
        if (count <= 0)
            continue;

        out.pixels.set(
            scaled.pixels.subarray(sourceY * scaled.width + sourceX,
                                   sourceY * scaled.width + sourceX + count),
            y * targetWidth + copyX0);
    }

    return out;
}

export class Bitmap {
    constructor(width, height) {
        this.width = width;
        this.height = height;
        this.stride = (width + 7) >> 3;
        this.bits = new Uint8Array(this.stride * height);
    }

    get(x, y) {
        if (x < 0 || x >= this.width || y < 0 || y >= this.height)
            return false;
        return (this.bits[y * this.stride + (x >> 3)] & (0x80 >> (x & 7))) !== 0;
    }

    set(x, y, black) {
        if (x < 0 || x >= this.width || y < 0 || y >= this.height)
            return;
        const index = y * this.stride + (x >> 3);
        const mask = 0x80 >> (x & 7);

        if (black)
            this.bits[index] |= mask;
        else
            this.bits[index] &= ~mask;
    }
}

function rowIsBlank(bitmap, y, inverted) {
    const row = y * bitmap.stride;
    const blank = inverted ? 0xff : 0x00;

    for (let i = 0; i < bitmap.stride; i++) {
        if (i === bitmap.stride - 1 && bitmap.width % 8 !== 0) {
            const mask = 0xff << (8 - (bitmap.width % 8));
            const value = bitmap.bits[row + i];
            if ((value & mask) !== (blank & mask))
                return false;
        } else if (bitmap.bits[row + i] !== blank) {
            return false;
        }
    }

    return true;
}

export function trimBitmap(bitmap, minHeight, inverted = false) {
    const blank = inverted ? 0xff : 0x00;
    let top = 0;
    let bottom;

    if (minHeight < 1)
        minHeight = 1;
    if (minHeight > bitmap.height)
        minHeight = bitmap.height;

    while (top < bitmap.height && rowIsBlank(bitmap, top, inverted))
        top++;

    if (top === bitmap.height) {
        const newHeight = Math.min(minHeight, bitmap.height);
        if (newHeight === bitmap.height)
            return false;
        bitmap.bits.fill(blank, 0, bitmap.stride * newHeight);
        bitmap.height = newHeight;
        return true;
    }

    bottom = bitmap.height - 1;
    while (bottom > top && rowIsBlank(bitmap, bottom, inverted))
        bottom--;

    const contentHeight = bottom - top + 1;
    let newHeight = Math.max(contentHeight, minHeight);
    if (newHeight > bitmap.height)
        newHeight = bitmap.height;

    if (top > 0 && contentHeight > 0)
        bitmap.bits.copyWithin(0, top * bitmap.stride, (top + contentHeight) * bitmap.stride);

    if (newHeight > contentHeight)
        bitmap.bits.fill(blank, contentHeight * bitmap.stride, newHeight * bitmap.stride);

    if (newHeight === bitmap.height && top === 0)
        return false;

    bitmap.height = newHeight;
    return true;
}

const KERNELS = {
    floyd: {
        divisor: 16,
        entries: [[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]],
    },
    jarvis: {
        divisor: 48,
        entries: [[1, 0, 7], [2, 0, 5], [-2, 1, 3], [-1, 1, 5], [0, 1, 7], [1, 1, 5], [2, 1, 3],
                  [-2, 2, 1], [-1, 2, 3], [0, 2, 5], [1, 2, 3], [2, 2, 1]],
    },
    stucki: {
        divisor: 42,
        entries: [[1, 0, 8], [2, 0, 4], [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2],
                  [-2, 2, 1], [-1, 2, 2], [0, 2, 4], [1, 2, 2], [2, 2, 1]],
    },
    atkinson: {
        divisor: 8,
        entries: [[1, 0, 1], [2, 0, 1], [-1, 1, 1], [0, 1, 1], [1, 1, 1], [0, 2, 1]],
    },
    sierra: {
        divisor: 32,
        entries: [[1, 0, 5], [2, 0, 3], [-2, 1, 2], [-1, 1, 4], [0, 1, 5], [1, 1, 4], [2, 1, 2],
                  [-1, 2, 2], [0, 2, 3], [1, 2, 2]],
    },
    sierralite: {
        divisor: 4,
        entries: [[1, 0, 2], [-1, 1, 1], [0, 1, 1]],
    },
    burkes: {
        divisor: 32,
        entries: [[1, 0, 8], [2, 0, 4], [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2],
                  [-2, 2, 1], [-1, 2, 2], [0, 2, 4], [1, 2, 2], [2, 2, 1]],
    },
};

const BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
];

const BAYER8 = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
];

function ditherDiffusion(source, kernel, threshold, result) {
    const rowSize = source.width + 4;
    const rows = [
        new Float32Array(rowSize),
        new Float32Array(rowSize),
        new Float32Array(rowSize),
    ];

    for (let y = 0; y < source.height; y++) {
        const row = y * source.width;

        for (let x = 0; x < source.width; x++) {
            let value = source.pixels[row + x] + rows[0][x + 2];

            if (value < 0)
                value = 0;
            if (value > 255)
                value = 255;

            const black = value < threshold;
            const quantized = black ? 0 : 255;
            const error = value - quantized;

            result.set(x, y, black);

            for (const [dx, dy, weight] of kernel.entries) {
                const target = x + 2 + dx;

                if (target < 0 || target >= rowSize)
                    continue;
                rows[dy][target] += error * weight / kernel.divisor;
            }
        }

        const old = rows[0];
        rows[0] = rows[1];
        rows[1] = rows[2];
        rows[2] = old;
        rows[2].fill(0);
    }
}

function ditherOrdered(source, matrix, size, threshold, result) {
    const shift = threshold - 128;

    for (let y = 0; y < source.height; y++) {
        for (let x = 0; x < source.width; x++) {
            const level = matrix[y % size][x % size];
            let value = (level + 0.5) * 255 / (size * size) + shift;

            value = clamp(value, 1, 255);
            result.set(x, y, source.pixels[y * source.width + x] < value);
        }
    }
}

export function ditherImage(source, algorithm = 'floyd', threshold = 128) {
    threshold = clamp(Math.round(threshold), 0, 255);

    const result = new Bitmap(source.width, source.height);

    if (algorithm === 'threshold') {
        for (let y = 0; y < source.height; y++) {
            for (let x = 0; x < source.width; x++)
                result.set(x, y, source.pixels[y * source.width + x] < threshold);
        }
        return result;
    }

    if (algorithm === 'bayer4')
        return ditherOrdered(source, BAYER4, 4, threshold, result), result;
    if (algorithm === 'bayer8')
        return ditherOrdered(source, BAYER8, 8, threshold, result), result;

    const kernel = KERNELS[algorithm] || KERNELS.floyd;
    ditherDiffusion(source, kernel, threshold, result);
    return result;
}

export function packBitsEncode(source) {
    const out = new Uint8Array(source.length * 2 + 2);
    let output = 0;
    let input = 0;

    const countRun = (offset) => {
        let run = 1;
        while (offset + run < source.length && run < 128 &&
               source[offset + run] === source[offset])
            run++;
        return run;
    };

    while (input < source.length) {
        const run = countRun(input);

        if (run >= 2) {
            out[output++] = (1 - run) & 0xff;
            out[output++] = source[input];
            input += run;
            continue;
        }

        const start = input;

        input += run;
        while (input < source.length && input - start < 128) {
            const nextRun = countRun(input);

            if (nextRun >= 2)
                break;
            if (input - start + nextRun > 128)
                break;
            input += nextRun;
        }

        const literal = input - start;

        out[output++] = (literal - 1) & 0xff;
        out.set(source.subarray(start, start + literal), output);
        output += literal;
    }

    return out.subarray(0, output);
}

function lineIsZero(line) {
    for (let i = 0; i < line.length; i++) {
        if (line[i] !== 0)
            return false;
    }
    return true;
}

export function buildLine(bitmap, row, geometry, border = false, invert = false) {
    const line = new Uint8Array(geometry.headBytes);
    const headPins = geometry.headPins;

    if (!bitmap || bitmap.width <= 0 || bitmap.height <= 0 || row < 0 ||
        row >= bitmap.height || geometry.printWidthPx <= 0) {
        if (border) {
            for (let k = 0; k < headPins; k++) {
                const pin = headPins - 1 - k;
                if (pin < geometry.leftOffsetPx ||
                    pin >= geometry.leftOffsetPx + geometry.printWidthPx)
                    line[k >> 3] |= 0x80 >> (k & 7);
            }
        }
        if (invert) {
            for (let i = 0; i < line.length; i++)
                line[i] ^= 0xff;
        }
        return line;
    }

    const imageLeft = geometry.leftOffsetPx +
        Math.trunc((geometry.printWidthPx - bitmap.width) / 2);
    const minimumPin = imageLeft;
    const maximumPin = imageLeft + bitmap.width;

    for (let k = 0; k < headPins; k++) {
        const pin = headPins - 1 - k;
        let black;

        if (pin >= geometry.leftOffsetPx &&
            pin < geometry.leftOffsetPx + geometry.printWidthPx &&
            pin >= minimumPin && pin < maximumPin) {
            black = bitmap.get(pin - imageLeft, row);
        } else {
            black = border;
        }

        if (invert)
            black = !black;
        if (black)
            line[k >> 3] |= 0x80 >> (k & 7);
    }

    return line;
}

export function emitLine(line, compression = 'tiff') {
    if (lineIsZero(line))
        return Uint8Array.of(0x5a); /* 'Z' */

    let payload;

    if (compression === 'tiff') {
        const encoded = packBitsEncode(line);

        if (encoded.length > 0 && encoded.length < line.length) {
            payload = encoded;
        } else {
            payload = new Uint8Array(line.length + 1);
            payload[0] = line.length;
            payload.set(line, 1);
        }
    } else {
        payload = line;
    }

    const out = new Uint8Array(3 + payload.length);
    out[0] = 0x67; /* 'g' */
    out[1] = 0x00;
    out[2] = payload.length;
    out.set(payload, 3);
    return out;
}

export function marginDots(geometry, marginMm) {
    if (geometry.type === 'diecut')
        return 0;

    const dots = marginMm * geometry.dpi / MM_PER_INCH;
    return clamp(Math.ceil(dots), 0, 65535);
}

function pageHeader(geometry, lines, firstPage, options) {
    const info = new Uint8Array(10);
    const margin = marginDots(geometry, options.marginMm);
    let flags = 0;
    let expanded = 0;

    if (options.peeler)
        flags |= 0x10;
    if (options.autoCut)
        flags |= 0x40;
    if (options.cutAtEnd)
        expanded |= 0x08;

    info[0] = 0x8e;
    info[1] = geometry.type === 'continuous' ? 0x0a : 0x0b;
    info[2] = geometry.mediaWidthRound;
    info[3] = geometry.mediaLengthRound;
    info[4] = lines & 0xff;
    info[5] = (lines >> 8) & 0xff;
    info[6] = (lines >> 16) & 0xff;
    info[7] = (lines >> 24) & 0xff;
    info[8] = firstPage ? 0x00 : 0x01;
    info[9] = 0x00;

    const chunks = [
        Uint8Array.of(0x1b, 0x69, 0x61, 0x01),
        Uint8Array.of(0x1b, 0x69, 0x21, 0x00),
        Uint8Array.of(0x1b, 0x69, 0x7a),
        info,
        Uint8Array.of(0x1b, 0x69, 0x4d, flags),
        Uint8Array.of(0x1b, 0x69, 0x64, margin & 0xff, (margin >> 8) & 0xff),
        Uint8Array.of(0x1b, 0x69, 0x4b, expanded),
        Uint8Array.of(0x4d, options.compression === 'tiff' ? 0x02 : 0x00),
    ];

    return chunks;
}

function concatChunks(chunks) {
    let total = 0;
    for (const chunk of chunks)
        total += chunk.length;

    const out = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        out.set(chunk, offset);
        offset += chunk.length;
    }
    return out;
}

/**
 * Build the complete Brother raster stream for a job.
 *
 * `bitmaps` are already rotated, placed and dithered pages.  The geometry
 * describes the selected media.
 */
export function buildJobStream({ model, geometry, bitmaps, copies = 1, options = {} }) {
    const settings = {
        compression: 'tiff',
        border: false,
        invert: false,
        peeler: false,
        autoCut: false,
        cutAtEnd: true,
        marginMm: geometry.type === 'continuous' ? 3 : 0,
        ...options,
    };

    const chunks = [];

    chunks.push(new Uint8Array(model.initNuls));
    chunks.push(Uint8Array.of(0x1b, 0x40));

    let firstPage = true;
    const copyCount = Math.max(1, Math.round(copies));

    for (let page = 0; page < bitmaps.length; page++) {
        const bitmap = bitmaps[page];

        for (let copy = 0; copy < copyCount; copy++) {
            chunks.push(...pageHeader(geometry, bitmap.height, firstPage, settings));

            for (let y = 0; y < bitmap.height; y++) {
                const line = buildLine(bitmap, y, geometry, settings.border, settings.invert);
                chunks.push(emitLine(line, settings.compression));
            }

            const last = page === bitmaps.length - 1 && copy === copyCount - 1;
            chunks.push(Uint8Array.of(last ? 0x1a : 0x0c));
            firstPage = false;
        }
    }

    chunks.push(Uint8Array.of(0x1b, 0x69, 0x61, 0xff));
    return concatChunks(chunks);
}

/**
 * Prepare a document page for printing: optional rotation/mirroring, scaling
 * into the printable area, dithering and (for continuous media) trimming.
 */
export function preparePage(image, geometry, options = {}) {
    const settings = {
        rotate: 0,
        mirror: false,
        scaling: 'fit',
        alignX: 'center',
        alignY: 'center',
        dither: 'floyd',
        threshold: 128,
        border: false,
        invert: false,
        autoTrim: true,
        ...options,
    };

    let current = image;

    if (settings.rotate !== 0)
        current = rotateGray(current, settings.rotate);

    if (settings.mirror)
        current = mirrorGray(current);

    const placed = placeGray(current, geometry.printWidthPx, geometry.printHeightPx,
                             settings.scaling, settings.alignX, settings.alignY);

    const bitmap = ditherImage(placed, settings.dither, settings.threshold);

    if (geometry.type === 'continuous' && settings.autoTrim && !settings.border) {
        const minimum = Math.ceil(12 * geometry.dpi / MM_PER_INCH);
        trimBitmap(bitmap, minimum, settings.invert);
    }

    return bitmap;
}
