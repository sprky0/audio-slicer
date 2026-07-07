/**
 * WSOLA time-stretch (Waveform Similarity Overlap-Add).
 *
 * Pure DSP — no Web Audio. Stretches audio in time by `factor` (output length /
 * input length) while preserving pitch. Pitch-shifting is built on top of this by
 * the caller: stretch by an extra factor, then resample (playbackRate) to undo the
 * duration change, leaving a net pitch shift.
 *
 * Tuned for short, transient-heavy slices (drum chops) at ~44.1k:
 *   frame 1024 (~23ms), Hann window, 75% overlap (synthesis hop 256),
 *   normalized cross-correlation lag search ±256 samples.
 *
 * Stereo: a single lag is computed from channel 0 and applied to all channels, so
 * the inter-channel phase (stereo image) is preserved.
 */

function hann(n) {
	const w = new Float32Array(n);
	for (let i = 0; i < n; i++) {
		w[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
	}
	return w;
}

// Best lag δ in [-search, search] around `idealStart` whose frame best matches the
// "natural successor" at `refPos` (normalized cross-correlation over `corrLen`).
function findBestLag(x, idealStart, refPos, corrLen, search) {
	const len = x.length;
	let bestDelta = 0;
	let bestScore = -Infinity;
	for (let d = -search; d <= search; d++) {
		const cs = idealStart + d;
		if (cs < 0 || cs + corrLen >= len || refPos + corrLen >= len) continue;
		let dot = 0, e1 = 0, e2 = 0;
		for (let n = 0; n < corrLen; n++) {
			const a = x[cs + n];
			const b = x[refPos + n];
			dot += a * b;
			e1  += a * a;
			e2  += b * b;
		}
		const score = dot / (Math.sqrt(e1 * e2) + 1e-9);
		if (score > bestScore) {
			bestScore = score;
			bestDelta = d;
		}
	}
	return bestDelta;
}

/**
 * Incremental WSOLA stretcher. The synthesis loop is driven by output position,
 * so it can be advanced in chunks — process() does a bounded slice of work and
 * reports how far it got, which makes sample-accurate progress (outPos/outputLen)
 * cheap and lets a long build yield to the event loop between chunks.
 *
 * @param {Float32Array[]} channels  input channel data
 * @param {number} factor            output length / input length (> 0)
 * @param {object} [opts]            { frame, overlap, search }
 * @returns {{ outputLen:number, inputLen:number, done:boolean,
 *             process:(maxOut:number)=>number, result:()=>Float32Array[] }}
 */
export function createTimeStretcher(channels, factor, opts = {}) {
	const numCh    = channels.length;
	const inputLen = numCh ? channels[0].length : 0;

	const frame   = opts.frame   || 1024;
	const overlap = opts.overlap != null ? opts.overlap : 0.75;
	const Hs      = Math.max(1, Math.round(frame * (1 - overlap)));  // synthesis hop
	const Ha      = Hs / factor;                                     // analysis hop (fractional)
	const search  = opts.search != null ? opts.search : Hs;
	const corrLen = Hs;
	const win     = hann(frame);

	const valid     = numCh > 0 && inputLen > 0 && factor > 0;
	const outputLen = valid ? Math.max(1, Math.round(inputLen * factor)) : inputLen;
	const padded    = outputLen + frame;

	const out  = [];
	if (valid) for (let c = 0; c < numCh; c++) out.push(new Float32Array(padded));
	const norm  = valid ? new Float32Array(padded) : null;   // overlap-add normalization (COLA)
	const lagCh = numCh ? channels[0] : null;                // shared lag driven by one channel

	let prevInputPos = 0;
	let outPos       = 0;
	let frameIdx     = 0;

	return {
		outputLen,
		inputLen,
		get done() { return !valid || outPos >= outputLen; },

		// Advance synthesis by up to ~maxOut output samples (Infinity = finish).
		// Returns the number of output samples advanced this call.
		process(maxOut) {
			if (!valid) return 0;
			const startOut = outPos;
			const lim      = Math.max(1, Math.floor(maxOut));
			const target   = Math.min(outputLen, outPos + lim);
			while (outPos < target) {
				const idealStart = Math.round(frameIdx * Ha);
				const delta = frameIdx > 0
					? findBestLag(lagCh, idealStart, prevInputPos + Hs, corrLen, search)
					: 0;
				let start = idealStart + delta;
				if (start < 0) start = 0;
				if (start > inputLen - 1) start = inputLen - 1;

				for (let c = 0; c < numCh; c++) {
					const inp = channels[c];
					const o   = out[c];
					for (let n = 0; n < frame; n++) {
						const si = start + n;
						if (si >= inputLen) break;
						o[outPos + n] += inp[si] * win[n];
					}
				}
				for (let n = 0; n < frame; n++) {
					if (start + n < inputLen) norm[outPos + n] += win[n];
				}

				prevInputPos = start;
				outPos += Hs;
				frameIdx++;
			}
			// Clamp to outputLen: the loop overshoots by up to one hop on the final
			// chunk, but progress must sum to exactly outputLen (parity with total).
			return Math.max(0, Math.min(outPos, outputLen) - startOut);
		},

		// COLA-normalize the accumulated output and return the stretched channels.
		result() {
			if (!valid) return channels.map((c) => c.slice());
			const res = [];
			for (let c = 0; c < numCh; c++) {
				const o = out[c];
				const r = new Float32Array(outputLen);
				for (let i = 0; i < outputLen; i++) {
					const g = norm[i];
					r[i] = g > 1e-6 ? o[i] / g : o[i];
				}
				res.push(r);
			}
			return res;
		},
	};
}

/**
 * Convenience: run the whole stretch synchronously in one call.
 * @param {Float32Array[]} channels  input channel data
 * @param {number} factor            output length / input length (> 0)
 * @param {object} [opts]            { frame, overlap, search }
 * @returns {Float32Array[]}         stretched channel data
 */
export function timeStretch(channels, factor, opts = {}) {
	const ts = createTimeStretcher(channels, factor, opts);
	ts.process(Infinity);
	return ts.result();
}
