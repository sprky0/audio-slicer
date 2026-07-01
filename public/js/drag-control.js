/**
 * DragControl: a button-shaped, grid-friendly control that replaces knobs,
 * sliders and dropdowns with one consistent component.
 *
 * Shape/size match a button (one --control-h cell), so it drops into the same
 * grid columns. Inside: a translucent fill from the origin to the value with a
 * bright vertical "leading edge" line marking the exact position, plus a centered
 * "Label value" readout. Drag horizontally to change — smoothly for continuous
 * params (with an optional center detent), or stepping evenly between discrete
 * options for dropdown replacements.
 *
 * Three things it can wrap (or run standalone):
 *   - a number/range <input>  → continuous (min/max/step read from the element)
 *   - a <select>              → stepped (options read from the element)
 *   - nothing (standalone)    → pass {min,max,step,value} or {options,value} + onChange
 *
 * When wrapping an element it mirrors the value to it and dispatches input/change,
 * so existing listeners/readers of that element keep working unchanged.
 *
 * No external deps. Pointer-based drag; also supports wheel + arrow keys.
 */

const SENS_PX = 150;   // drag distance (px) for the full continuous range
const STEP_PX = 20;    // drag distance (px) per discrete step
const DRAG_PX = 4;     // movement under this on release = a click (jump), not a drag

export default class DragControl {
	constructor(opts = {}) {
		this.el       = opts.el || null;
		this.label    = opts.label || '';
		this.detent   = opts.detent;                  // optional center value
		this.format   = opts.format || ((v) => String(v));
		this.onChange = opts.onChange || (() => {});
		this.disabled = false;

		if (this.el && this.el.tagName === 'SELECT') {
			this.stepped = true;
			this.options = Array.from(this.el.options).map((o) => ({ value: o.value, label: o.textContent }));
			this._idx    = Math.max(0, this.options.findIndex((o) => o.value === this.el.value));
		} else if (this.el) {                          // number / range input
			this.stepped = false;
			this.min   = parseFloat(this.el.min);
			this.max   = parseFloat(this.el.max);
			this.step  = parseFloat(this.el.step) || 0;
			this.value = parseFloat(this.el.value);
		} else if (opts.options) {                     // standalone stepped
			this.stepped = true;
			this.options = opts.options.map((o) => (typeof o === 'object' ? o : { value: o, label: String(o) }));
			this._idx    = Math.max(0, this.options.findIndex((o) => String(o.value) === String(opts.value)));
		} else {                                        // standalone continuous
			this.stepped = false;
			this.min   = opts.min != null ? opts.min : 0;
			this.max   = opts.max != null ? opts.max : 1;
			this.step  = opts.step != null ? opts.step : 0;
			this.value = opts.value != null ? opts.value : this.min;
		}

		this._build();
		this._render();
	}

	getElement() { return this.root; }

	getValue() { return this.stepped ? this.options[this._idx].value : this.value; }

	// Programmatic set (no onChange); mirrors to a wrapped element without dispatching.
	setValue(v) {
		if (this._dragging) return;
		if (this.stepped) {
			const i = this.options.findIndex((o) => String(o.value) === String(v));
			if (i >= 0) this._idx = i;
		} else {
			this.value = this._clampSnap(parseFloat(v));
		}
		if (this.el) this.el.value = String(this.getValue());
		this._render();
	}

	setDisabled(on) {
		this.disabled = !!on;
		this.root.classList.toggle('disabled', this.disabled);
	}

	// Re-read from a wrapped element that changed externally (e.g. a tempo lock).
	syncFromEl() {
		if (!this.el) return;
		if (this.stepped) {
			const i = this.options.findIndex((o) => o.value === this.el.value);
			if (i >= 0) this._idx = i;
		} else {
			this.value = parseFloat(this.el.value);
		}
		this._render();
	}

	_build() {
		const root = document.createElement('div');
		root.className = 'drag-control';
		root.tabIndex  = 0;
		this.fill = document.createElement('div'); this.fill.className = 'drag-fill';
		this.edge = document.createElement('div'); this.edge.className = 'drag-edge';
		this.text = document.createElement('span'); this.text.className = 'drag-label';
		root.appendChild(this.fill);
		root.appendChild(this.edge);
		root.appendChild(this.text);
		if (this.el) { this.el.classList.add('drag-native-hidden'); root.appendChild(this.el); }
		this.root = root;
		this._bind();
	}

	_clampSnap(v) {
		if (!isFinite(v)) return this.min;
		const range = this.max - this.min;
		if (this.detent != null && range && Math.abs(v - this.detent) < range * 0.04) v = this.detent;
		if (this.step > 0) v = Math.round(v / this.step) * this.step;
		return Math.max(this.min, Math.min(this.max, v));
	}

	// Commit a continuous value: store, mirror+dispatch to wrapped el, fire onChange.
	_commit(v) {
		this.value = v;
		if (this.el) {
			this.el.value = String(v);
			this.el.dispatchEvent(new Event('input',  { bubbles: true }));
			this.el.dispatchEvent(new Event('change', { bubbles: true }));
		}
		this.onChange(v);
		this._render();
	}

	_stepBy(d) {
		const i = Math.max(0, Math.min(this.options.length - 1, this._idx + d));
		if (i === this._idx) return;
		this._idx = i;
		const opt = this.options[i];
		if (this.el) { this.el.value = String(opt.value); this.el.dispatchEvent(new Event('change', { bubbles: true })); }
		this.onChange(opt.value);
		this._render();
	}

	_nudge(dir) {
		if (this.stepped) { this._stepBy(dir); return; }
		const inc = this.step > 0 ? this.step : (this.max - this.min) / 100;
		this._commit(this._clampSnap(this.value + dir * inc));
	}

	// Jump to the absolute position of a click (x within the control's width).
	_jumpTo(clientX) {
		const rect = this.root.getBoundingClientRect();
		if (!rect.width) return;
		const frac = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
		if (this.stepped) {
			const n = this.options.length;
			const i = Math.max(0, Math.min(n - 1, Math.round(frac * (n - 1))));
			if (i !== this._idx) {
				this._idx = i;
				const opt = this.options[i];
				if (this.el) { this.el.value = String(opt.value); this.el.dispatchEvent(new Event('change', { bubbles: true })); }
				this.onChange(opt.value);
			}
			this._render();
		} else {
			this._commit(this._clampSnap(this.min + frac * (this.max - this.min)));
		}
	}

	_bind() {
		const root = this.root;
		let lastX = 0, lastY = 0, accum = 0, downX = 0, downY = 0, moved = false;

		const onMove = (e) => {
			// Stay a (potential) click until the pointer moves past the threshold;
			// then it's a drag and we start adjusting from that point.
			if (!moved) {
				if (Math.abs(e.clientX - downX) < DRAG_PX && Math.abs(e.clientY - downY) < DRAG_PX) return;
				moved = true;
				lastX = e.clientX;
				lastY = e.clientY;
			}
			// Up or right increases; down or left decreases. Combining the axes means
			// a vertical drag behaves like a horizontal one, so either gesture works.
			const move = (e.clientX - lastX) - (e.clientY - lastY);
			lastX = e.clientX;
			lastY = e.clientY;
			if (this.stepped) {
				accum += move;
				while (accum >=  STEP_PX) { this._stepBy(1);  accum -= STEP_PX; }
				while (accum <= -STEP_PX) { this._stepBy(-1); accum += STEP_PX; }
			} else {
				const range = this.max - this.min;
				this._commit(this._clampSnap(this.value + (move / SENS_PX) * range));
			}
		};
		const onUp = (e) => {
			window.removeEventListener('pointermove', onMove);
			window.removeEventListener('pointerup',   onUp);
			try { root.releasePointerCapture(e.pointerId); } catch (_) {}
			// No real drag → treat as a click: jump to the clicked position.
			if (!moved) this._jumpTo(downX);
			this._dragging = false;
			root.classList.remove('dragging');
		};
		root.addEventListener('pointerdown', (e) => {
			if (this.disabled) return;
			this._dragging = true;
			downX = e.clientX; downY = e.clientY;
			lastX = e.clientX; lastY = e.clientY;
			accum = 0;
			moved = false;
			root.classList.add('dragging');
			try { root.setPointerCapture(e.pointerId); } catch (_) {}
			window.addEventListener('pointermove', onMove);
			window.addEventListener('pointerup',   onUp);
			e.preventDefault();
		});
		root.addEventListener('dblclick', () => {
			if (this.disabled) return;
			if (this.detent != null) this._commit(this.detent);
		});
		root.addEventListener('wheel', (e) => {
			if (this.disabled) return;
			e.preventDefault();
			this._nudge(e.deltaY < 0 ? 1 : -1);
		}, { passive: false });
		root.addEventListener('keydown', (e) => {
			if (this.disabled) return;
			let dir = 0;
			if (e.key === 'ArrowRight' || e.key === 'ArrowUp')   dir =  1;
			else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') dir = -1;
			else return;
			e.preventDefault();
			this._nudge(dir);
		});
		// A wrapped element changed under us (e.g. programmatic tempo lock) → reflect it.
		if (this.el) this.el.addEventListener('change', () => { if (!this._dragging) this.syncFromEl(); });
	}

	_render() {
		let frac, originFrac, valueText;
		if (this.stepped) {
			const n = this.options.length;
			frac       = n > 1 ? this._idx / (n - 1) : 0;
			originFrac = 0;
			valueText  = this.options[this._idx] ? this.options[this._idx].label : '';
		} else {
			const range = (this.max - this.min) || 1;
			frac       = (this.value - this.min) / range;
			originFrac = this.detent != null ? (this.detent - this.min) / range : 0;
			valueText  = this.format(this.value);
		}
		frac = Math.max(0, Math.min(1, frac));
		const a = Math.min(frac, originFrac) * 100;
		const b = Math.max(frac, originFrac) * 100;
		this.fill.style.left  = a + '%';
		this.fill.style.width = (b - a) + '%';
		this.edge.style.left  = `calc(${frac * 100}% - 1px)`;
		this.text.textContent = `${this.label} ${valueText}`.trim();
	}
}
