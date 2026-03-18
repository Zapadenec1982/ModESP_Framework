# Design Token Migration Guide

## Як використовувати tokens

Tokens визначені в `tokens.css` та доступні глобально через CSS custom properties.

### Spacing
```css
/* Замість hardcoded: */
padding: 16px;        → padding: var(--sp-4);
margin-bottom: 12px;  → margin-bottom: var(--sp-3);
gap: 8px;             → gap: var(--sp-2);
```

### Typography
```css
font-size: 14px;  → font-size: var(--text-md);
font-size: 11px;  → font-size: var(--text-sm);
font-size: 64px;  → font-size: var(--text-hero-lg);
```

### Border Radius
```css
border-radius: 12px; → border-radius: var(--radius-2xl);
border-radius: 8px;  → border-radius: var(--radius-lg);
border-radius: 6px;  → border-radius: var(--radius-md);
```

### Status Colors
```css
color: #22c55e;   → color: var(--status-ok);
color: #2563eb;   → color: var(--status-compressor);
background: #991b1b; → background: var(--alarm-dark);
```

## Migration Priority

### Sprint 2 (Dashboard Redesign)
- Dashboard.svelte: hero temp, equipment pills, alarm banner
- SliderWidget.svelte: track/thumb sizes, spacing
- MiniChart.svelte: chart colors

### Sprint 2 (Settings UX)
- Card.svelte: padding, radius, title font
- WidgetRenderer.svelte: widget gap
- NumberInput.svelte: button sizes
- SelectWidget.svelte: disabled styles
- ToggleWidget.svelte: toggle dimensions

### Later
- ChartWidget.svelte: palette → --chart-* vars
- Layout.svelte: sidebar widths → --sidebar-width
- StatusText.svelte: defrost colors → --defrost-* vars
- Toast.svelte: position, sizing
- All buttons: gradient → consistent pattern
