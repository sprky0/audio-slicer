/**
 * Shared slice palette. One color per slice index (0..15).
 * Used by the waveform segment indicators and the sequencer step boxes
 * so a given slice has the same color wherever it appears.
 */
const PALETTE_SIZE = 16;
const PALETTE      = [];
for (let i = 0; i < PALETTE_SIZE; i++) {
	const hue = i * (360 / PALETTE_SIZE);
	PALETTE.push(`hsl(${hue.toFixed(1)}, 70%, 48%)`);
}

export default PALETTE;
export { PALETTE_SIZE };
