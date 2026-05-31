/**
 * Knob — a custom pointer-driven rotary control.
 *
 * Driven directly by pointer events (not a hidden range input), which is what
 * makes it feel solid: vertical drag with pointer-capture (the grab never gets
 * lost), a 1:1 indicator (no CSS transition lag), step snapping, Shift for fine
 * adjust, mouse-wheel, arrow-key + ARIA accessibility, and double-click to reset.
 *
 * Usage: const knob = new Knob({ label, min, max, step, value, onChange, ... });
 *   getElement() → DOM element to insert
 *   .value       → current value
 *   setValue(v)  → set programmatically (no event)
 *   'change'     → CustomEvent with detail = value (on user interaction)
 *
 * Options: detent (snap target while dragging, e.g. 0), resetValue (double-click
 * target; defaults to detent ?? initial value), signed (show +/-), format (fn
 * value→string for the readout), dragRange (px for a full sweep, default 150).
 */

class Knob extends EventTarget {
	constructor({
		label = '', min = 0, max = 1, step = 0.01, value = 0.5, onChange = null,
		detent = null, detentZone = null, resetValue = null, signed = false, format = null, dragRange = 150,
	} = {}) {
		super();
		this.label     = label;
		this.min       = min;
		this.max       = max;
		this.step      = step;
		this.value     = this._clamp(value);
		this.onChange  = onChange;
		this.detent    = detent;
		// Soft-detent catch zone (on the continuous drag value). Default ~3% of
		// range — wide enough to "stick" to the detent, small enough not to swallow
		// neighbouring stops (e.g. ±1 semitone on the pitch knob stays reachable).
		this.detentZone= detentZone !== null ? detentZone : (max - min) * 0.03;
		this.resetValue= resetValue !== null ? resetValue : (detent !== null ? detent : value);
		this.signed    = signed;
		this.format    = format;
		this.dragRange = dragRange;
		this._decimals = (String(step).split('.')[1] || '').length;
		this._dragging = false;

		this._createDOM();
		this._bindEvents();
		this._updateVisual();
	}

	_clamp(v) {
		return Math.max(this.min, Math.min(this.max, v));
	}

	// Soft-detent on the continuous value, else snap to step + clamp.
	_quantize(v) {
		if (this.detent !== null && Math.abs(v - this.detent) <= this.detentZone) return this.detent;
		let q = Math.round((v - this.min) / this.step) * this.step + this.min;
		return parseFloat(this._clamp(q).toFixed(6));
	}

	_format(v) {
		if (typeof this.format === 'function') return this.format(v);
		let s = v.toFixed(this._decimals);
		if (this.signed && v > 0) s = '+' + s;
		return s;
	}

	_createDOM() {
		this.container = document.createElement('div');
		this.container.className = 'knob-container';

		this.knob = document.createElement('div');
		this.knob.className = 'knob';
		this.knob.tabIndex  = 0;
		this.knob.setAttribute('role', 'slider');
		this.knob.setAttribute('aria-label', this.label);
		this.knob.setAttribute('aria-valuemin', String(this.min));
		this.knob.setAttribute('aria-valuemax', String(this.max));

		this.valueElem = document.createElement('span');
		this.valueElem.className = 'knob-value';

		this.labelElem = document.createElement('label');
		this.labelElem.className = 'knob-label';
		this.labelElem.textContent = this.label;

		this.knob.appendChild(this.valueElem);
		this.container.appendChild(this.knob);
		this.container.appendChild(this.labelElem);
	}

	_bindEvents() {
		this.knob.addEventListener('pointerdown', (e) => this._onPointerDown(e));
		this.knob.addEventListener('pointermove', (e) => this._onPointerMove(e));
		this.knob.addEventListener('pointerup',   (e) => this._onPointerUp(e));
		this.knob.addEventListener('pointercancel', (e) => this._onPointerUp(e));
		this.knob.addEventListener('lostpointercapture', () => { this._dragging = false; this.knob.classList.remove('dragging'); });
		this.knob.addEventListener('wheel', (e) => this._onWheel(e), { passive: false });
		this.knob.addEventListener('keydown', (e) => this._onKeyDown(e));
		this.knob.addEventListener('dblclick', () => this.setValue(this.resetValue, true));
	}

	_onPointerDown(e) {
		if (e.button && e.button !== 0) return;
		this._dragging = true;
		this._startY   = e.clientY;
		this._startVal = this.value;
		this.knob.classList.add('dragging');
		try { this.knob.setPointerCapture(e.pointerId); } catch (err) { /* unsupported */ }
		this.knob.focus();
		e.preventDefault();
	}

	_onPointerMove(e) {
		if (!this._dragging) return;
		const dy   = this._startY - e.clientY;            // drag up → increase
		const fine = e.shiftKey ? 0.25 : 1;
		const delta = (dy / this.dragRange) * (this.max - this.min) * fine;
		this._setFromInteraction(this._startVal + delta);
		e.preventDefault();
	}

	_onPointerUp(e) {
		if (!this._dragging) return;
		this._dragging = false;
		this.knob.classList.remove('dragging');
		try { this.knob.releasePointerCapture(e.pointerId); } catch (err) { /* already released */ }
	}

	_onWheel(e) {
		e.preventDefault();
		const dir = e.deltaY < 0 ? 1 : -1;
		this._setFromInteraction(this.value + dir * this.step);
	}

	_onKeyDown(e) {
		let handled = true;
		switch (e.key) {
			case 'ArrowUp':   case 'ArrowRight': this._setFromInteraction(this.value + this.step); break;
			case 'ArrowDown': case 'ArrowLeft':  this._setFromInteraction(this.value - this.step); break;
			case 'PageUp':    this._setFromInteraction(this.value + this.step * 10); break;
			case 'PageDown':  this._setFromInteraction(this.value - this.step * 10); break;
			case 'Home':      this._setFromInteraction(this.min); break;
			case 'End':       this._setFromInteraction(this.max); break;
			default:          handled = false;
		}
		if (handled) e.preventDefault();
	}

	// Apply an interactive change: quantize, store, redraw, emit if it actually moved.
	_setFromInteraction(raw) {
		const v = this._quantize(raw);
		if (v === this.value) { this._updateVisual(); return; }
		this.value = v;
		this._updateVisual();
		this.dispatchEvent(new CustomEvent('change', { detail: this.value }));
		if (typeof this.onChange === 'function') this.onChange(this.value);
	}

	_updateVisual() {
		const frac = (this.max - this.min) === 0 ? 0 : (this.value - this.min) / (this.max - this.min);
		this.knob.style.setProperty('--knob-fraction', frac.toFixed(4));
		this.knob.style.setProperty('--knob-rotation', `${(-135 + frac * 270).toFixed(2)}deg`);
		const text = this._format(this.value);
		this.valueElem.textContent = text;
		this.knob.setAttribute('aria-valuenow', String(this.value));
		this.knob.setAttribute('aria-valuetext', text);
	}

	getElement() {
		return this.container;
	}

	// Programmatic set (e.g. restore). Clamps but does NOT step/detent-snap, so a
	// stored value is preserved exactly. Emits a change only when emit === true.
	setValue(val, emit = false) {
		this.value = this._clamp(val);
		this._updateVisual();
		if (emit) {
			this.dispatchEvent(new CustomEvent('change', { detail: this.value }));
			if (typeof this.onChange === 'function') this.onChange(this.value);
		}
	}
}

export default Knob;
