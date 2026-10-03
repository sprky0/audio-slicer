// Bounce every parity session through the browser version's own renderMix in
// headless Chrome, writing <case>.js.wav (32-bit float stereo) next to it.
//
//   cd Frog/tools/parity && npm i playwright-core
//   python3 -m http.server 8931 --bind 127.0.0.1 --directory ../../..   # repo root, in another shell
//   node js-bounce.mjs [case ...]
//
// The app is loaded from /public/, the session is seeded into localStorage and
// the source WAV into IndexedDB exactly as the app saves them, the page is
// reloaded so it restores the slicer, and the slicer registry's export hooks
// (the same ones the Export panel uses) drive renderMix. Float data comes back
// before the 16-bit encode, so the comparison is not limited by quantisation.
import { chromium } from 'playwright-core';
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const fixtures = process.env.FROG_PARITY_FIXTURES || join(here, '..', '..', 'engine', 'tests', 'fixtures', 'parity');
const base = process.env.FROG_PARITY_URL || 'http://127.0.0.1:8931';
const lengthBeats = 4;

const wanted = process.argv.slice(2);
const cases = readdirSync(fixtures).filter((f) => f.endsWith('.json') && !f.endsWith('.js.json'))
	.map((f) => f.replace(/\.json$/, '')).filter((c) => wanted.length === 0 || wanted.includes(c));

function floatWav(channels, rate) {
	const n = channels[0].length, nCh = channels.length;
	const data = n * nCh * 4;
	const buf = Buffer.alloc(44 + data);
	let p = 0;
	const str = (s) => { buf.write(s, p, 'ascii'); p += s.length; };
	str('RIFF'); buf.writeUInt32LE(36 + data, p); p += 4; str('WAVE');
	str('fmt '); buf.writeUInt32LE(16, p); p += 4; buf.writeUInt16LE(3, p); p += 2; buf.writeUInt16LE(nCh, p); p += 2;
	buf.writeUInt32LE(rate, p); p += 4; buf.writeUInt32LE(rate * nCh * 4, p); p += 4; buf.writeUInt16LE(nCh * 4, p); p += 2; buf.writeUInt16LE(32, p); p += 2;
	str('data'); buf.writeUInt32LE(data, p); p += 4;
	for (let i = 0; i < n; i++) {
		for (let c = 0; c < nCh; c++) { buf.writeFloatLE(channels[c][i], p); p += 4; }
	}
	return buf;
}

const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--autoplay-policy=no-user-gesture-required'] });
for (const name of cases) {
	const session = JSON.parse(readFileSync(join(fixtures, `${name}.json`), 'utf8'));
	const ctx = await browser.newContext();
	const page = await ctx.newPage();
	page.on('pageerror', (e) => console.error(`[${name}] page error:`, e.message));
	// seed storage from a blank same-origin page: the app flushes its own state
	// on beforeunload, so seeding on the app page and reloading would lose it
	await page.goto(`${base}/public/blank-for-seeding.html`);
	await page.evaluate(async ({ state, wavUrl }) => {
		localStorage.setItem('jsloop.state.v1', JSON.stringify(state));
		const blob = await (await fetch(wavUrl)).blob();
		await new Promise((resolve, reject) => {
			const req = indexedDB.open('jsloop', 1);
			req.onupgradeneeded = () => { if (!req.result.objectStoreNames.contains('audio')) req.result.createObjectStore('audio'); };
			req.onerror = () => reject(req.error);
			req.onsuccess = () => {
				const tx = req.result.transaction('audio', 'readwrite');
				tx.objectStore('audio').put(blob, `audio-${state.slicers[0].id}`);
				tx.oncomplete = resolve;
				tx.onerror = () => reject(tx.error);
			};
		});
	}, { state: session, wavUrl: `${base}/Frog/engine/tests/fixtures/parity/parity-source.wav` });
	await page.goto(`${base}/public/index.html`);
	await page.waitForFunction(() => {
		const s = window.__jsloop && window.__jsloop.slicers && window.__jsloop.slicers[0];
		return !!(s && s.exportMeta && s.exportMeta().exportable);
	}, null, { timeout: 20000 });

	const result = await page.evaluate(async ({ bpm, beats }) => {
		const { renderMix } = await import('/public/js/export-wav.js');
		const s = window.__jsloop.slicers[0];
		s.exportPrescan();
		const t0 = performance.now();
		while (s.exportPending() > 0 && performance.now() - t0 < 60000) {
			await new Promise((r) => setTimeout(r, 30));
		}
		const rate = new AudioContext().sampleRate;
		const buf = await renderMix({ tracks: [s.exportTrack()], masterBpm: bpm, lengthBeats: beats, sampleRate: rate });
		return { rate, length: buf.length, L: Array.from(buf.getChannelData(0)), R: Array.from(buf.getChannelData(1)), state: s.getState() };
	}, { bpm: session.master.bpm, beats: lengthBeats });

	writeFileSync(join(fixtures, `${name}.js.wav`), floatWav([Float32Array.from(result.L), Float32Array.from(result.R)], result.rate));
	// what the browser actually restored and played, for the record
	writeFileSync(join(fixtures, `${name}.js.json`), JSON.stringify(result.state, null, '\t') + '\n');
	let peak = 0;
	for (const v of result.L) peak = Math.max(peak, Math.abs(v));
	console.log(`${name}: ${result.length} frames at ${result.rate} Hz, ${result.state.seq.tiles.length} tiles, beats ${result.state.beats} bpm ${result.state.bpm}, peak ${peak.toFixed(3)}`);
	await ctx.close();
}
await browser.close();
