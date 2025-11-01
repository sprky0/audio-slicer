# Code Style Guide

This project follows strict code style. All contributors and future changes **must** adhere to the following rules:

## Brace Style: OTBS (One True Brace Style)
- Opening braces are placed on the same line as the control statement or function declaration.
- Example:
	```js
	if (condition) {
		// code
	} else {
		// code
	}

	function example() {
		// code
	}
	```

## Indentation: Tabs Only
- Use **tabs** for all indentation.
- Do **not** use spaces for indentation.
- Example:
	```js
	function foo() {
		if (bar) {
			baz();
		}
	}
	```

## Alignment: Spaces for Character Alignment
- Use **spaces** (not tabs) to align characters within a line, such as aligning variable assignments or comments.
- Example:
	```js
	const foo   = 1;
	const bar   = 2;
	const quux  = 3; // aligned with spaces
	```

## General Recommendations
- Be consistent with style throughout the codebase.
- Review this file before submitting changes.
- Use your editor's settings to enforce tabs for indentation and OTBS brace style.

