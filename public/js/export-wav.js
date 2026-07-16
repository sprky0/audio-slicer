/**
 * export-wav.js — offline render of the master mix to a downloadable stereo WAV.
 *
 * No DOM/engine dependencies beyond a Blob download helper: the caller passes
 * plain "track" descriptors (volume/pan + a tile list + the tile→buffer policy)
 * and this renders them through an OfflineAudioContext, peak-normalizes, and
 * encodes 16-bit PCM into a RIFF/WAV Blob. No new deps.
 *
 * A track mirrors what one slicer plays: its Vol/Pan build a gain(→panner)→
 * destination graph, and its tiles are walked (looping) accumulating the master
 * `stepSec`, each resolved via the same `getTilePlayback` the live transport uses
 * — so the export is bit-faithful to playback (time-stretch + pitch included).
 */

import { applyEnvelope } from './envelope.js';

// Replicates AudioEngine.scheduleBuffer's per-voice envelope + declick: tile
// fades come from the same envelope module the live engine uses; the declick
// fades to 0 ending at `stopAt` and hard-stops there when the voice is a
// stretched fill OR would ring past its slot — otherwise let a short slice ring
// out naturally.
function scheduleVoice(ctx, dest, buffer, when, stopAt, playbackRate, declick, env) {
	if (!buffer || !(when >= 0)) return;
	const source = ctx.createBufferSource();
	source.buffer = buffer;
	source.playbackRate.value = playbackRate > 0 ? playbackRate : 1;

	const voiceGain = ctx.createGain();
	voiceGain.gain.value = 1;
	source.connect(voiceGain);
	voiceGain.connect(dest);

	const DECLICK = 0.005;
	const effDur  = source.buffer.duration / source.playbackRate.value;
	const audibleEnd = (typeof stopAt === 'number' && stopAt > when)
		? Math.min(stopAt, when + effDur)
		: when + effDur;
	const fadesOut = applyEnvelope(voiceGain.gain, when, audibleEnd, env);
	source.start(when);
	if (typeof stopAt === 'number' && stopAt > when && (declick || stopAt < when + effDur)) {
		if (!fadesOut) {
			voiceGain.gain.setValueAtTime(1, Math.max(when, stopAt - DECLICK));
			voiceGain.gain.linearRampToValueAtTime(0, stopAt);
		}
		source.stop(stopAt + DECLICK);
	}
}

// Scale every sample so the max abs peak lands on `target` (~0.99): never clips,
// and lifts a quiet mix. Per-track Vol still sets the relative balance.
function normalize(buf, target = 0.99) {
	let peak = 0;
	for (let c = 0; c < buf.numberOfChannels; c++) {
		const d = buf.getChannelData(c);
		for (let i = 0; i < d.length; i++) {
			const a = Math.abs(d[i]);
			if (a > peak) peak = a;
		}
	}
	if (peak > 0) {
		const g = target / peak;
		for (let c = 0; c < buf.numberOfChannels; c++) {
			const d = buf.getChannelData(c);
			for (let i = 0; i < d.length; i++) d[i] *= g;
		}
	}
}

/**
 * Render selected tracks to a normalized stereo AudioBuffer of exactly
 * `lengthBeats` beats at `masterBpm`.
 *
 * track = { volume, pan, tiles, stepSec, getTilePlayback, modHooks? }
 *   stepSec         seconds per unit/step at the master tempo (a number)
 *   getTilePlayback (tile, stepSec, ov) => { buffer, playbackRate, dur, fill, env } | null
 *                   ov = optional one-shot { mute, rev } overrides for the voice
 *   modHooks        optional step-modifier callbacks (main.js closes over the
 *                   slicer's modifier lane; the export evolves like a live take):
 *     beforeTile(posSteps, tiles) => tiles   pattern actions (rand/reset) fire
 *                   against the render's WORKING COPY — may return a replacement
 *                   list (same length); the live pattern is untouched
 *     voiceOverrides(tile, posSteps) => { mute, rev } | null
 *   (posSteps = steps of render time elapsed, monotonic across loop passes.)
 */
export async function renderMix({ tracks, masterBpm, lengthBeats, sampleRate }) {
	if (!(masterBpm > 0) || !(lengthBeats > 0) || !(sampleRate > 0)) {
		throw new Error('renderMix: invalid tempo/length/rate');
	}
	const beatSec = 60 / masterBpm;
	const endTime = lengthBeats * beatSec;
	const frames  = Math.max(1, Math.round(endTime * sampleRate));
	const ctx     = new OfflineAudioContext(2, frames, sampleRate);

	for (const track of tracks || []) {
		if (!track) continue;
		const gain = ctx.createGain();
		gain.gain.value = Number.isFinite(track.volume) ? track.volume : 1;
		let node = gain;
		if (ctx.createStereoPanner) {
			const panner = ctx.createStereoPanner();
			panner.pan.value = Number.isFinite(track.pan) ? track.pan : 0;
			gain.connect(panner);
			node = panner;
		}
		node.connect(ctx.destination);

		let tiles     = (track.tiles || []).slice();   // working copy — modHooks may reshape it
		const stepSec = track.stepSec;
		const hooks   = track.modHooks || null;
		if (tiles.length === 0 || !(stepSec > 0)) continue;

		// Walk the tile list, looping, until we cover N beats. A tile occupies
		// w·stepSec of time; one full pass of the list is exactly the slicer's
		// `beats`, so if N is a multiple of that the loop lands on the boundary.
		// posSteps mirrors the live transport's step position, so step modifiers
		// fire identically (pattern actions land on the slot that triggered them).
		let t = 0, i = 0, posSteps = 0;
		while (t < endTime) {
			if (hooks && hooks.beforeTile) tiles = hooks.beforeTile(posSteps, tiles) || tiles;
			const tile = tiles[i % tiles.length];
			const w    = (tile && tile.w > 0) ? tile.w : 1;
			const dur  = w * stepSec;
			const ov   = (hooks && hooks.voiceOverrides) ? hooks.voiceOverrides(tile, posSteps) : null;
			const r    = track.getTilePlayback(tile, stepSec, ov);
			if (r && r.buffer) {
				// Hard-cut at endTime so the render tail matches a seamless loop.
				scheduleVoice(ctx, node, r.buffer, t, Math.min(t + dur, endTime), r.playbackRate || 1, !!r.fill, r.env || null);
			}
			t += dur;
			posSteps += w;
			i++;
		}
	}

	const rendered = await ctx.startRendering();
	normalize(rendered);
	return rendered;
}

/** Encode a (stereo) AudioBuffer as a 16-bit PCM RIFF/WAV Blob. */
export function encodeWav(buf) {
	const numCh      = 2;
	const rate       = buf.sampleRate;
	const len        = buf.length;
	const ch0        = buf.getChannelData(0);
	const ch1        = buf.numberOfChannels > 1 ? buf.getChannelData(1) : ch0;
	const bytesPer   = 2;
	const blockAlign = numCh * bytesPer;
	const dataSize   = len * blockAlign;

	const ab   = new ArrayBuffer(44 + dataSize);
	const view = new DataView(ab);
	let p = 0;
	const str = (s) => { for (let i = 0; i < s.length; i++) view.setUint8(p++, s.charCodeAt(i)); };

	str('RIFF');
	view.setUint32(p, 36 + dataSize, true); p += 4;
	str('WAVE');
	str('fmt ');
	view.setUint32(p, 16, true);            p += 4;   // PCM fmt chunk size
	view.setUint16(p, 1, true);             p += 2;   // format = PCM
	view.setUint16(p, numCh, true);         p += 2;
	view.setUint32(p, rate, true);          p += 4;
	view.setUint32(p, rate * blockAlign, true); p += 4;   // byte rate
	view.setUint16(p, blockAlign, true);    p += 2;
	view.setUint16(p, 16, true);            p += 2;   // bits/sample
	str('data');
	view.setUint32(p, dataSize, true);      p += 4;

	for (let i = 0; i < len; i++) {
		let l = ch0[i]; let r = ch1[i];
		l = l < -1 ? -1 : l > 1 ? 1 : l;
		r = r < -1 ? -1 : r > 1 ? 1 : r;
		view.setInt16(p, l < 0 ? l * 0x8000 : l * 0x7fff, true); p += 2;
		view.setInt16(p, r < 0 ? r * 0x8000 : r * 0x7fff, true); p += 2;
	}
	return new Blob([ab], { type: 'audio/wav' });
}

/** Kick off a browser download of `blob` as `filename`. */
export function triggerDownload(blob, filename) {
	const url = URL.createObjectURL(blob);
	const a   = document.createElement('a');
	a.href = url;
	a.download = filename || 'jsloop-export.wav';
	document.body.appendChild(a);
	a.click();
	a.remove();
	setTimeout(() => URL.revokeObjectURL(url), 1000);
}
