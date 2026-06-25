# Liqueia HTML/CSS Renderer

Liqueia now has a native, in-kernel HTML/CSS rendering path instead of only converting pages to plain text.

## Pipeline

1. `scripts/webbridge.py` fetches HTTPS pages for the guest and preserves supported content types such as `text/html`.
2. `kernel/core/network.c` stores larger HTTP bodies so real page markup can reach the browser.
3. `kernel/apps/liqueia.c` parses a compact HTML/CSS subset and paints it using LiquidOS graphics primitives.

## Supported First-Pass Features

- Block-style HTML flow for headings, paragraphs, divs, sections, articles, buttons, and links.
- Basic embedded CSS selectors for tags and single classes.
- Inline `style=""` attributes.
- CSS colors from hex, `rgb(...)`, `rgba(...)`, and common named colors.
- `background`, `background-color`, `color`, `border-radius`, `padding`, `margin`, `width`, `height`, and `font-size`.
- `backdrop-filter` and `box-shadow` map to the native Liquid Glass material.
- SVG-style `feDisplacementMap` glass is represented by `gfx_liquid_filter_glass_rect()`, a native procedural displacement material that bends the current framebuffer behind the component, brightens/saturates it, and traces specular highlights around the full rounded perimeter.
- Clipped browser content area so pages do not bleed into chrome.

## Current Limits

This is not yet a standards-complete browser engine. It does not implement JavaScript, DOM mutation, flexbox/grid, full CSS inheritance, external stylesheet loading, images, forms, media, or full TLS inside the kernel. SVG filter graphs are not interpreted instruction-by-instruction yet; the Liquid Glass displacement filter is supported as a native material so it can adapt to live wallpaper/window contents without embedding large PNG maps in the kernel.
