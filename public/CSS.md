# CSS System & Variables Documentation

## Overview

This project uses a modern CSS system based on CSS custom properties (variables) to enable flexible, maintainable, and themeable styles. The system is designed to make it easy to update colors, spacing, typography, and other design tokens globally, while keeping the CSS organized and readable.

## Where to Find the CSS

- Main stylesheet: `css/styles.css`
- Example demonstration: `example.html` and `css/example.css`

## CSS Variables

CSS variables are defined in the `:root` selector for global scope. Example:

```css
:root {
  --color-primary: #007bff;
  --color-secondary: #6c757d;
  --color-success: #28a745;
  --color-danger: #dc3545;
  --color-warning: #ffc107;
  --color-info: #17a2b8;
  --color-light: #f8f9fa;
  --color-dark: #343a40;

  --font-family-base: 'Segoe UI', Arial, sans-serif;
  --font-size-base: 16px;
  --font-size-lg: 1.25rem;
  --font-size-sm: 0.875rem;

  --spacing-xs: 0.25rem;
  --spacing-sm: 0.5rem;
  --spacing-md: 1rem;
  --spacing-lg: 2rem;
  --border-radius: 0.25rem;
}
```

## Using CSS Variables

You can use variables anywhere in your CSS:

```css
body {
  font-family: var(--font-family-base);
  font-size: var(--font-size-base);
  color: var(--color-dark);
  background: var(--color-light);
}
.button {
  background: var(--color-primary);
  color: #fff;
  padding: var(--spacing-sm) var(--spacing-md);
  border-radius: var(--border-radius);
}
```

## Extending and Customizing

To add or override variables, add them to the `:root` selector or a specific selector for scoping. For theming, you can define alternate sets of variables under a class or data attribute.

Example (dark mode):

```css
body.dark-mode {
  --color-background: #222;
  --color-text: #eee;
}
```

## Example Page

See `example.html` for a comprehensive demonstration of all variables and styles in use, including:

- Headings, paragraphs, and text styles
- Buttons in all color variants
- Alerts and cards
- Spacing and layout utilities
- Customizing variables live

## Best Practices

- Use variables for all design tokens (colors, spacing, fonts, etc.)
- Reference variables with `var(--variable-name)` in your CSS
- Group related variables for clarity
- Use semantic variable names (e.g., `--color-primary`, not `--blue`)
- Document new variables in this file
