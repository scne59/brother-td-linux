/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 brother-td-cups contributors
 */

import * as pdfjsLib from './vendor/pdfjs/pdf.min.mjs';
import {
    MODELS,
    buildJobStream,
    geometryFor,
    grayFromRGBA,
    modelById,
    modelForUsbPid,
    normalizeOrientation,
    preparePage,
} from './driver.js';

pdfjsLib.GlobalWorkerOptions.workerSrc =
    new URL('./vendor/pdfjs/pdf.worker.min.mjs', import.meta.url).href;

const $ = (selector) => document.querySelector(selector);

const MEDIA_PRESETS = [
    { id: '4x6', label: '4 × 6 in (102 × 152 mm)', width: 101.6, length: 152.4 },
    { id: '4x5', label: '4 × 5 in (102 × 127 mm)', width: 101.6, length: 127 },
    { id: '4x4', label: '4 × 4 in (102 × 102 mm)', width: 101.6, length: 101.6 },
    { id: '3.5x5', label: '3.5 × 5 in (89 × 127 mm)', width: 88.9, length: 127 },
    { id: '3x5', label: '3 × 5 in (76 × 127 mm)', width: 76.2, length: 127 },
    { id: '2x3.5', label: '2 × 3.5 in (51 × 89 mm)', width: 50.8, length: 88.9 },
    { id: '102x152', label: '102 × 152 mm', width: 102, length: 152 },
    { id: '102x127', label: '102 × 127 mm', width: 102, length: 127 },
    { id: '102x114', label: '102 × 114 mm', width: 102, length: 114 },
    { id: '102x100', label: '102 × 100 mm', width: 102, length: 100 },
    { id: '102x76', label: '102 × 76 mm', width: 102, length: 76 },
    { id: '102x50', label: '102 × 50 mm', width: 102, length: 50 },
    { id: '102x38', label: '102 × 38 mm', width: 102, length: 38 },
    { id: '102x25', label: '102 × 25 mm', width: 102, length: 25 },
    { id: '76x127', label: '76 × 127 mm', width: 76, length: 127 },
    { id: '76x100', label: '76 × 100 mm', width: 76, length: 100 },
    { id: '76x76', label: '76 × 76 mm', width: 76, length: 76 },
    { id: '58x100', label: '58 × 100 mm', width: 58, length: 100 },
    { id: '51x76', label: '51 × 76 mm', width: 51, length: 76 },
    { id: '39x100', label: '39 × 100 mm', width: 39, length: 100 },
    { id: 'custom', label: 'Custom…', width: null, length: null },
];

const MAX_RENDER_DIMENSION = 8192;
const MAX_RENDER_PIXELS = 40 * 1000 * 1000;
const USB_CHUNK_SIZE = 16 * 1024;

const state = {
    file: null,
    pages: null,
    pagesKey: '',
    bitmaps: [],
    geometry: null,
    stream: null,
    pageIndex: 0,
    busy: false,
    usb: null,
    generating: false,
    pending: false,
    generation: 0,
    regenTimer: null,
};

class StaleGeneration extends Error {}

function log(message, kind = '') {
    const element = $('#log');
    const line = document.createElement('div');

    if (kind)
        line.className = kind;
    line.textContent = message;
    element.append(line);
    element.scrollTop = element.scrollHeight;
}

function clearLog() {
    $('#log').replaceChildren();
}

function setBusy(busy) {
    state.busy = busy;
    updateButtons();
}

function updateButtons() {
    const hasDocument = state.file !== null;
    const hasOutput = state.stream !== null;
    const busy = state.busy || state.generating;

    $('#preview-button').disabled = !hasDocument || busy;
    $('#download-button').disabled = !hasOutput || busy;
    $('#print-button').disabled = !hasOutput || busy;
    $('#connect').disabled = busy;
}

function selectedMedia() {
    const preset = MEDIA_PRESETS.find((item) => item.id === $('#media').value);

    if (!preset || preset.id === 'custom') {
        return {
            width: Number($('#custom-width').value),
            length: Number($('#custom-length').value),
        };
    }

    return { width: preset.width, length: preset.length };
}

function readOptions() {
    const media = selectedMedia();

    return {
        modelId: $('#model').value,
        widthMm: media.width,
        lengthMm: media.length,
        mediaType: $('#media-type').value,
        copies: Number($('#copies').value) || 1,
        scaling: $('#scaling').value,
        rotation: $('#rotation').value,
        dither: $('#dither').value,
        threshold: Number($('#threshold').value),
        compression: $('#compression').value,
        border: $('#border').checked,
        invert: $('#invert').checked,
        cutAtEnd: $('#cut-at-end').checked,
    };
}

function populateSelects() {
    const modelSelect = $('#model');

    for (const model of MODELS) {
        const option = document.createElement('option');
        option.value = model.id;
        option.textContent = `${model.id} · ${model.dpi} dpi`;
        modelSelect.append(option);
    }
    modelSelect.value = 'TD-4210D';

    const mediaSelect = $('#media');

    for (const preset of MEDIA_PRESETS) {
        const option = document.createElement('option');
        option.value = preset.id;
        option.textContent = preset.label;
        mediaSelect.append(option);
    }
    mediaSelect.value = '4x6';
}

function updateCustomMediaRows() {
    const custom = $('#media').value === 'custom';
    $('#custom-width-row').hidden = !custom;
    $('#custom-length-row').hidden = !custom;
}

function clearOutput() {
    state.bitmaps = [];
    state.geometry = null;
    state.stream = null;
    state.pageIndex = 0;
    $('#preview').hidden = true;
    $('#page-nav').hidden = true;
    $('#job-info').textContent = '';
    $('#progress').hidden = true;
    $('#progress').value = 0;
    updateButtons();
}

function scheduleGenerate(delay = 350) {
    if (!state.file)
        return;

    clearTimeout(state.regenTimer);
    state.regenTimer = setTimeout(() => {
        state.regenTimer = null;

        if (state.busy || state.generating) {
            state.pending = true;
            return;
        }

        generate();
    }, delay);
}

function formatMm(value) {
    return `${value.toFixed(1)} mm`;
}

function setFile(file) {
    state.file = file;
    state.pages = null;
    state.pagesKey = '';
    state.generation++;
    state.pending = false;
    clearTimeout(state.regenTimer);
    state.regenTimer = null;
    clearOutput();
    clearLog();

    if (!file) {
        $('#file-info').textContent = 'No document selected.';
        updateButtons();
        return;
    }

    $('#file-info').textContent = `${file.name} (${(file.size / 1024).toFixed(1)} KiB)`;
    updateButtons();
    log(`Selected ${file.name}; generating preview…`);
    scheduleGenerate(0);
}

function renderScaleFor(page, model) {
    const base = page.getViewport({ scale: 1 });
    let scale = model.dpi / 72;

    if (Math.max(base.width, base.height) * scale > MAX_RENDER_DIMENSION)
        scale = MAX_RENDER_DIMENSION / Math.max(base.width, base.height);
    if (base.width * scale * base.height * scale > MAX_RENDER_PIXELS)
        scale = Math.sqrt(MAX_RENDER_PIXELS / (base.width * base.height));

    return scale;
}

async function loadPdfPages(file, model, onProgress, isStale) {
    const data = new Uint8Array(await file.arrayBuffer());
    const pdf = await pdfjsLib.getDocument({ data }).promise;
    const pages = [];

    try {
        if (pdf.numPages > 500) {
            throw new Error(`Document has ${pdf.numPages} pages; the browser app is limited to 500 pages.`);
        }

        for (let number = 1; number <= pdf.numPages; number++) {
            if (isStale?.())
                throw new StaleGeneration();

            const page = await pdf.getPage(number);
            const scale = renderScaleFor(page, model);
            const viewport = page.getViewport({ scale });
            const canvas = document.createElement('canvas');
            const context = canvas.getContext('2d', { willReadFrequently: true });

            canvas.width = Math.max(1, Math.ceil(viewport.width));
            canvas.height = Math.max(1, Math.ceil(viewport.height));
            context.fillStyle = '#ffffff';
            context.fillRect(0, 0, canvas.width, canvas.height);

            await page.render({
                canvasContext: context,
                viewport,
                background: 'white',
            }).promise;

            const image = context.getImageData(0, 0, canvas.width, canvas.height);

            page.cleanup();

            if (isStale?.())
                throw new StaleGeneration();

            pages.push(grayFromRGBA(image.data, canvas.width, canvas.height));
            onProgress?.(number, pdf.numPages);
            await new Promise((resolve) => setTimeout(resolve, 0));
        }
    } finally {
        await pdf.destroy();
    }

    return pages;
}

async function loadImagePage(file, onProgress, isStale) {
    const bitmap = await createImageBitmap(file);

    if (isStale?.())
        throw new StaleGeneration();

    let scale = 1;

    if (Math.max(bitmap.width, bitmap.height) * scale > MAX_RENDER_DIMENSION)
        scale = MAX_RENDER_DIMENSION / Math.max(bitmap.width, bitmap.height);
    if (bitmap.width * scale * bitmap.height * scale > MAX_RENDER_PIXELS)
        scale = Math.sqrt(MAX_RENDER_PIXELS / (bitmap.width * bitmap.height));

    const canvas = document.createElement('canvas');
    const context = canvas.getContext('2d', { willReadFrequently: true });

    canvas.width = Math.max(1, Math.round(bitmap.width * scale));
    canvas.height = Math.max(1, Math.round(bitmap.height * scale));
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = 'high';
    context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);

    const image = context.getImageData(0, 0, canvas.width, canvas.height);
    onProgress?.(1, 1);

    if (isStale?.())
        throw new StaleGeneration();

    return [grayFromRGBA(image.data, canvas.width, canvas.height)];
}

async function ensurePages(model, token) {
    const isPdf = state.file.type === 'application/pdf' ||
                  /\.pdf$/i.test(state.file.name);
    const key = `${isPdf ? 'pdf' : 'image'}|${state.file.name}|${state.file.size}|` +
                `${state.file.lastModified}|${isPdf ? model.id : ''}`;
    const isStale = () => token !== state.generation;

    if (state.pages && state.pagesKey === key) {
        log('Using cached document render.');
        return state.pages;
    }

    log(isPdf ? `Rendering ${state.file.name} at ${model.dpi} dpi…`
              : `Rendering ${state.file.name}…`);

    const pages = isPdf
        ? await loadPdfPages(state.file, model, (page, count) => {
              log(`Rendered page ${page}/${count}.`);
          }, isStale)
        : await loadImagePage(state.file, (page, count) => {
              log(`Rendered image ${page}/${count}.`);
          }, isStale);

    if (isStale())
        throw new StaleGeneration();

    state.pages = pages;
    state.pagesKey = key;
    return pages;
}

async function generate() {
    if (!state.file)
        return;

    if (state.busy || state.generating) {
        state.pending = true;
        return;
    }

    const token = ++state.generation;
    const isStale = () => token !== state.generation;

    state.generating = true;
    state.stream = null;
    state.pending = false;
    clearTimeout(state.regenTimer);
    state.regenTimer = null;
    updateButtons();
    clearLog();
    $('#job-info').textContent = 'Updating preview…';

    try {
        const options = readOptions();
        const model = modelById(options.modelId);

        if (!model)
            throw new Error('Unknown printer model.');

        const normalized = normalizeOrientation(model, options.widthMm, options.lengthMm);
        const geometry = geometryFor(model, options.mediaType,
                                     normalized.widthMm, normalized.lengthMm);

        if (!geometry)
            throw new Error('Invalid media size.');

        if (normalized.rotated)
            log(`Landscape media description transposed to ${formatMm(normalized.widthMm)} × ${formatMm(normalized.lengthMm)}.`);

        if (geometry.mediaWidthMm !== options.widthMm)
            log(`Media width clamped to the ${model.id} maximum of ${model.maxWidthMm} mm.`);

        const pages = await ensurePages(model, token);

        if (isStale())
            return;

        log(`Preparing ${pages.length} page(s) for ${formatMm(geometry.mediaWidthMm)} × ${formatMm(geometry.mediaLengthMm)} media…`);

        const bitmaps = [];

        for (let index = 0; index < pages.length; index++) {
            if (isStale())
                return;

            const page = pages[index];
            let rotation;

            if (options.rotation === 'auto') {
                const pageIsLandscape = page.width > page.height;
                const mediaIsPortrait = geometry.mediaWidthMm <= geometry.mediaLengthMm;
                rotation = pageIsLandscape && mediaIsPortrait ? 90 : 0;
            } else {
                rotation = Number(options.rotation);
            }

            bitmaps.push(preparePage(page, geometry, {
                rotate: rotation,
                scaling: options.scaling,
                alignX: 'center',
                alignY: 'center',
                dither: options.dither,
                threshold: options.threshold,
                border: options.border,
                invert: options.invert,
                autoTrim: true,
            }));

            log(`Prepared page ${index + 1}: ${bitmaps[index].width} × ${bitmaps[index].height} dots.`);
            await new Promise((resolve) => setTimeout(resolve, 0));
        }

        if (isStale())
            return;

        log('Building printer command stream…');
        const stream = buildJobStream({
            model,
            geometry,
            bitmaps,
            copies: options.copies,
            options: {
                compression: options.compression,
                border: options.border,
                invert: options.invert,
                cutAtEnd: options.cutAtEnd,
            },
        });

        if (isStale())
            return;

        state.bitmaps = bitmaps;
        state.geometry = geometry;
        state.stream = stream;
        state.pageIndex = Math.min(state.pageIndex, bitmaps.length - 1);
        if (state.pageIndex < 0)
            state.pageIndex = 0;

        $('#job-info').textContent =
            `${pages.length} page(s) · ${geometry.printWidthPx} × ${geometry.printHeightPx} dots · ` +
            `${geometry.mediaWidthRound} × ${geometry.mediaLengthRound || '∞'} mm · ` +
            `${(stream.length / 1024).toFixed(1)} KiB`;
        $('#page-nav').hidden = bitmaps.length < 2;
        updatePageLabel();
        drawPreview();
        log(`Ready to print: ${stream.length} bytes.`, 'ok');
    } catch (error) {
        if (error instanceof StaleGeneration || isStale())
            return;
        log(`Error: ${error.message || error}`, 'error');
        $('#job-info').textContent = '';
    } finally {
        state.generating = false;
        updateButtons();
        if (state.pending) {
            state.pending = false;
            scheduleGenerate(0);
        }
    }
}

function drawPreview() {
    const bitmap = state.bitmaps[state.pageIndex];

    if (!bitmap) {
        $('#preview').hidden = true;
        return;
    }

    const canvas = $('#preview');
    const context = canvas.getContext('2d');
    const image = context.createImageData(bitmap.width, bitmap.height);
    const data = image.data;
    const stride = bitmap.stride;

    for (let y = 0; y < bitmap.height; y++) {
        const row = y * stride;
        const target = y * bitmap.width;

        for (let x = 0; x < bitmap.width; x++) {
            const black = (bitmap.bits[row + (x >> 3)] & (0x80 >> (x & 7))) !== 0;
            const value = black ? 0 : 255;
            const offset = (target + x) * 4;

            data[offset] = value;
            data[offset + 1] = value;
            data[offset + 2] = value;
            data[offset + 3] = 255;
        }
    }

    canvas.width = bitmap.width;
    canvas.height = bitmap.height;
    context.putImageData(image, 0, 0);
    canvas.hidden = false;
}

function updatePageLabel() {
    $('#page-label').textContent =
        `${state.pageIndex + 1} / ${state.bitmaps.length}`;
}

function downloadStream() {
    if (!state.stream)
        return;

    const blob = new Blob([state.stream], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');

    link.href = url;
    link.download = 'label.prn';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    log(`Saved ${state.stream.length} bytes as label.prn.`);
}

async function connectUsb() {
    if (state.usb) {
        await disconnectUsb();
        return;
    }

    if (!('usb' in navigator)) {
        log('WebUSB is not available. Use Chrome or Edge over https:// or http://localhost.', 'error');
        return;
    }

    try {
        const device = await navigator.usb.requestDevice({
            filters: [{ vendorId: 0x04f9 }],
        });
        await openUsb(device);
    } catch (error) {
        if (error && error.name === 'NotFoundError')
            log('No printer selected.');
        else
            log(`USB connection failed: ${error.message || error}`, 'error');
    }
}

async function openUsb(device) {
    try {
        await device.open();

        if (!device.configuration)
            await device.selectConfiguration(1);

        let selected = null;
        const candidates = [];

        for (const iface of device.configuration.interfaces) {
            for (const alternate of iface.alternates) {
                const endpoint = alternate.endpoints.find(
                    (candidate) => candidate.direction === 'out' && candidate.type === 'bulk');

                if (endpoint) {
                    candidates.push({
                        interfaceNumber: iface.interfaceNumber,
                        interfaceClass: iface.interfaceClass,
                        alternateSetting: alternate.alternateSetting,
                        endpointNumber: endpoint.endpointNumber,
                    });
                }
            }
        }

        /* Prefer the USB printer class (7); some devices also expose a
         * mass-storage or vendor interface with bulk endpoints. */
        selected = candidates.find((candidate) => candidate.interfaceClass === 7) ||
                   candidates[0] || null;

        if (!selected)
            throw new Error('No bulk OUT endpoint found on this USB device.');

        await device.claimInterface(selected.interfaceNumber);

        if (selected.alternateSetting !== 0)
            await device.selectAlternateInterface(selected.interfaceNumber,
                                                  selected.alternateSetting);

        state.usb = { device, ...selected };

        const model = modelForUsbPid(device.productId);

        if (model) {
            $('#model').value = model.id;
            log(`Connected ${device.productName || 'printer'} → ${model.id}.`, 'ok');
        } else {
            log(`Connected ${device.productName || 'printer'}. Select the matching model manually.`, 'ok');
        }

        $('#usb-status').textContent =
            `${device.productName || 'USB printer'} connected.`;
        $('#connect').textContent = 'Disconnect USB printer';
    } catch (error) {
        log(`Cannot claim the printer: ${error.message || error}`, 'error');
        log('Close CUPS and the printer application, unload the usblp kernel module ' +
            '(sudo modprobe -r usblp) and make sure the USB device is writable for your user.', '');
        try {
            await device.close();
        } catch {
            /* ignore */
        }
        state.usb = null;
    }
}

async function disconnectUsb() {
    if (!state.usb)
        return;

    const { device, interfaceNumber } = state.usb;

    try {
        await device.releaseInterface(interfaceNumber);
    } catch {
        /* ignore */
    }
    try {
        await device.close();
    } catch {
        /* ignore */
    }

    state.usb = null;
    $('#usb-status').textContent = 'Not connected.';
    $('#connect').textContent = 'Connect USB printer';
}

async function writeUsb(bytes) {
    if (!state.usb)
        throw new Error('No printer connected.');

    const { device, endpointNumber } = state.usb;
    const progress = $('#progress');

    progress.hidden = false;
    progress.value = 0;

    for (let offset = 0; offset < bytes.length; offset += USB_CHUNK_SIZE) {
        const chunk = bytes.subarray(offset, Math.min(offset + USB_CHUNK_SIZE, bytes.length));
        const result = await device.transferOut(endpointNumber, chunk);

        if (result.status !== 'ok')
            throw new Error(`USB transfer failed with status ${result.status}.`);

        progress.value = Math.round((offset + chunk.length) / bytes.length * 100);
        await new Promise((resolve) => setTimeout(resolve, 0));
    }

    progress.value = 100;
    setTimeout(() => { progress.hidden = true; }, 600);
}

async function print() {
    if (state.busy || state.generating)
        return;

    if (!state.stream) {
        log('The preview is not ready yet.', 'error');
        return;
    }

    if (!state.usb) {
        log('Connect the USB printer first.', 'error');
        return;
    }

    setBusy(true);

    try {
        log(`Sending ${state.stream.length} bytes to the printer…`);
        await writeUsb(state.stream);
        log('Print job sent.', 'ok');
    } catch (error) {
        log(`Printing failed: ${error.message || error}`, 'error');
    } finally {
        setBusy(false);
        if (state.pending) {
            state.pending = false;
            scheduleGenerate(0);
        }
    }
}

function wireEvents() {
    $('#file').addEventListener('change', (event) => {
        setFile(event.target.files[0] || null);
    });

    const dropzone = $('#dropzone');

    dropzone.addEventListener('dragover', (event) => {
        event.preventDefault();
        dropzone.classList.add('dragover');
    });
    dropzone.addEventListener('dragleave', () => dropzone.classList.remove('dragover'));
    dropzone.addEventListener('drop', (event) => {
        event.preventDefault();
        dropzone.classList.remove('dragover');
        setFile(event.dataTransfer.files[0] || null);
    });

    $('#media').addEventListener('change', updateCustomMediaRows);
    $('#preview-button').addEventListener('click', () => {
        state.pending = false;
        clearTimeout(state.regenTimer);
        state.regenTimer = null;
        generate();
    });
    $('#print-button').addEventListener('click', print);
    $('#download-button').addEventListener('click', downloadStream);
    $('#connect').addEventListener('click', connectUsb);

    for (const id of ['model', 'media', 'media-type', 'copies', 'scaling',
                      'rotation', 'dither', 'compression', 'border', 'invert',
                      'cut-at-end']) {
        document.getElementById(id).addEventListener('change', () => scheduleGenerate());
    }

    for (const id of ['custom-width', 'custom-length', 'threshold']) {
        document.getElementById(id).addEventListener('input', () => scheduleGenerate(500));
        document.getElementById(id).addEventListener('change', () => scheduleGenerate(0));
    }

    $('#prev-page').addEventListener('click', () => {
        if (state.pageIndex > 0) {
            state.pageIndex--;
            updatePageLabel();
            drawPreview();
        }
    });
    $('#next-page').addEventListener('click', () => {
        if (state.pageIndex + 1 < state.bitmaps.length) {
            state.pageIndex++;
            updatePageLabel();
            drawPreview();
        }
    });

    navigator.usb?.addEventListener('disconnect', (event) => {
        if (state.usb && state.usb.device === event.device) {
            state.usb = null;
            $('#usb-status').textContent = 'Printer disconnected.';
            $('#connect').textContent = 'Connect USB printer';
            log('Printer disconnected.', 'error');
        }
    });
}

async function reconnectKnownDevice() {
    if (!('usb' in navigator))
        return;

    try {
        const devices = await navigator.usb.getDevices();

        if (devices.length > 0)
            await openUsb(devices[0]);
    } catch {
        /* ignore */
    }
}

async function main() {
    populateSelects();
    updateCustomMediaRows();
    wireEvents();
    updateButtons();

    if (!('usb' in navigator)) {
        $('#usb-hint').textContent =
            'WebUSB is not available in this browser. Use Chrome or Edge on https:// or http://localhost.';
    } else if (!window.isSecureContext) {
        $('#usb-hint').textContent =
            'WebUSB requires a secure context. Open this page via https:// or http://localhost.';
    }

    await reconnectKnownDevice();
}

main();
