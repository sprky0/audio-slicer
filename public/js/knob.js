/**
 * Knob UI Component (modular, OOP, clean)
 * Usage: const knob = new Knob({ label, min, max, step, value, onChange });
 * knob.getElement() returns the DOM element to insert into the UI.
 */

class Knob extends EventTarget {
	constructor({ label = '', min = 0, max = 1, step = 0.01, value = 0.5, onChange = null } = {}) {
		super();
		this.label = label;
		this.min = min;
		this.max = max;
		this.step = step;
		this.value = value;
		this.onChange = onChange;

		this._createDOM();
		this._bindEvents();
		this._updateVisual();
	}

	_createDOM() {
		this.container = document.createElement('div');
		this.container.className = 'knob-container';

		this.knob = document.createElement('div');
		this.knob.className = 'knob';

		this.input = document.createElement('input');
		this.input.type = 'range';
		this.input.min = this.min;
		this.input.max = this.max;
		this.input.step = this.step;
		this.input.value = this.value;
		this.input.className = 'knob-input';

		this.labelElem = document.createElement('label');
		this.labelElem.className = 'knob-label';
		this.labelElem.textContent = this.label;

		this.valueElem = document.createElement('span');
		this.valueElem.className = 'knob-value';
		this.valueElem.textContent = this.value;

		this.knob.appendChild(this.input);
		this.knob.appendChild(this.valueElem);
		this.container.appendChild(this.knob);
		this.container.appendChild(this.labelElem);
	}

	_bindEvents() {
		this.input.addEventListener('input', (e) => {
			this.value = parseFloat(this.input.value);
			this._updateVisual();
			this.dispatchEvent(new CustomEvent('change', { detail: this.value }));
			if (typeof this.onChange === 'function') {
				this.onChange(this.value);
			}
		});
	}

	_updateVisual() {
		this.valueElem.textContent = this.value;
		const percent = (this.value - this.min) / (this.max - this.min);
		this.knob.style.setProperty('--knob-rotation', `${percent * 270 - 135}deg`);
	}

	getElement() {
		return this.container;
	}

	setValue(val) {
		this.value = val;
		this.input.value = val;
		this._updateVisual();
	}
}

export default Knob;
