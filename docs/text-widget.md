# Text Widget

The Text widget lets you add custom text to any zone on the home screen. You can display static labels, live machine data using variables, or create custom buttons that trigger actions. Content supports HTML for inline formatting — mix colors, bold, italic, and font sizes within a single widget.

## Adding a Text Widget

1. Go to **Settings > Layout**
2. Tap the **+** button on any zone
3. Select **Text** from the list
4. The widget appears with the default content "Text"

## Editing a Text Widget

To open the editor, **long-press** the orange "Text" chip in the layout editor.

The editor has these sections:

### Content

Type your text in the input field. You can mix plain text, HTML tags, and `%VARIABLE%` placeholders. The input uses a monospace font so tags are easy to read.

### Formatting Toolbar

The toolbar buttons **insert HTML tags at the cursor**. If you have text selected, the tags wrap the selection. If nothing is selected, empty tags are inserted and the cursor is placed between them so you can start typing.

| Button | Inserts | Example |
|--------|---------|---------|
| **B** | `<b>...</b>` | `<b>bold text</b>` |
| *I* | `<i>...</i>` | `<i>italic text</i>` |
| S | `<span style="font-size:12px">...</span>` | Small text |
| M | `<span style="font-size:18px">...</span>` | Normal text |
| L | `<span style="font-size:28px">...</span>` | Large text |
| XL | `<span style="font-size:48px">...</span>` | Extra large text |

Font sizes are in pixels with no upper limit. You can type any value manually (e.g., `font-size:72px`).

### Alignment

The three alignment buttons (left / center / right) control block-level alignment for the entire widget. This is a property of the widget, not inserted as HTML.

### Color Palette

Tapping a color circle inserts a `<span style="color:...">` tag at the cursor. Select text first to wrap it, or tap a color to start typing in that color.

Available colors: white, gray, blue, red, green, orange, brown, light blue, dark red, teal, bright red, purple.

### Preview

The preview at the bottom renders your content as rich text in real-time, with variables replaced by sample values. If an action is assigned, the preview has a highlighted border.

## HTML Reference

The content field supports a subset of HTML 4. Here are the most useful tags:

```html
<b>bold</b>
<i>italic</i>
<u>underline</u>
<span style="color:red">colored text</span>
<span style="color:#4e85f4">hex color</span>
<span style="font-size:28px">large text</span>
<span style="color:red; font-size:28px">large red text</span>
<span style="color:#e94560; font-size:48px; font-weight:bold">bold, red, large</span>
<br> (line break)
```

You can combine multiple styles in one `style` attribute, or nest tags:
```html
<b><span style="color:#e94560">bold red</span></b>
```

Named colors that work: `red`, `green`, `blue`, `white`, `yellow`, `cyan`, `magenta`, `orange`, `gray`, `black`. You can also use hex codes like `#ff4444`.

### Formatting Examples

**Temperature in red, large:**
```
<span style="color:#e73249; font-size:48px; font-weight:bold">%TEMP%°C</span>
```

**Mixed colors on one line:**
```
<span style="color:#18c37e">%PRESSURE% bar</span> / <span style="color:#4e85f4">%FLOW% ml/s</span>
```

**Bold label with normal value:**
```
<b>Profile:</b> %PROFILE%
```

**Multi-line with line break:**
```
<span style="font-size:48px; color:#e94560; font-weight:bold">%TEMP%°C</span><br><span style="color:#ffeeaa">Brew head temp</span>
```

**Status line with colors matching the shot graph:**
```
<span style="color:#18c37e">P: %PRESSURE%</span> <span style="color:#4e85f4">F: %FLOW%</span> <span style="color:#a2693d">W: %WEIGHT%g</span>
```

**Connection status indicator (recreates the built-in one):**
```
<span style="color:%CONNECTED_COLOR%; font-size:48px; font-weight:bold">%CONNECTED%</span><br><span style="color:#a0a8b8">%DEVICES%</span>
```

## Variables

Variables are placeholders replaced with live data. Wrap them in percent signs: `%VARIABLE%`. In the editor, tap any variable chip to insert it at the cursor.

### Available Variables

| Variable | Description | Example Output |
|----------|-------------|----------------|
| `%TEMP%` | Group head temperature (°C) | 92.3 |
| `%STEAM_TEMP%` | Steam heater temperature (°C) | 155.0 |
| `%PRESSURE%` | Group pressure (bar) | 9.0 |
| `%FLOW%` | Water flow rate (ml/s) | 2.1 |
| `%WATER%` | Water tank level (%) | 78 |
| `%WATER_ML%` | Water tank level (ml) | 850 |
| `%WEIGHT%` | Current scale weight (g) | 36.2 |
| `%SHOT_TIME%` | Elapsed shot time (s) | 28.5 |
| `%TARGET_WEIGHT%` | Target weight for stop-at-weight (g) | 36.0 |
| `%VOLUME%` | Cumulative volume poured (ml) | 42 |
| `%PROFILE%` | Active profile name | Adaptive v2 |
| `%STATE%` | Current machine state | Idle |
| `%TARGET_TEMP%` | Profile target temperature (°C) | 93.0 |
| `%SCALE%` | Connected scale name | Lunar |
| `%TIME%` | Current time (HH:MM) | 14:30 |
| `%DATE%` | Current date (YYYY-MM-DD) | 2025-01-15 |
| `%RATIO%` | Brew ratio | 2.0 |
| `%DOSE%` | Dose weight (g) | 18.0 |
| `%MACHINE_READY%` | Machine ready status | Ready |
| `%MACHINE_READY_COLOR%` | Color for ready status (green/red) | #00cc6d |
| `%CONNECTED%` | Machine connection status | Online |
| `%CONNECTED_COLOR%` | Color for connection status (green/red) | #00cc6d |
| `%DEVICES%` | Connected devices description | Machine + Scale |

All variables update in real-time. Temperature, pressure, flow, and weight update whenever the machine reports new values. Time and date update every second.

### Plain Text Examples

**Clock in the top bar:**
```
%TIME%
```

**Shot status:**
```
%WEIGHT%g / %TARGET_WEIGHT%g in %SHOT_TIME%s
```

**Profile and ratio:**
```
%PROFILE% @ 1:%RATIO%
```

## Actions (Idle Page)

When an action is assigned, the text widget becomes a tappable button. These actions are available on the idle (home) page:

### Navigation

| Action | Description |
|--------|-------------|
| Go to Settings | Opens the settings page |
| Go to History | Opens shot history |
| Go to Profiles | Opens the profile selector |
| Go to Profile Editor | Opens the profile editor |
| Go to D-Flow Editor | Opens the D-Flow / A-Flow profile editor |
| Go to Recipes | Opens the drink recipe list |
| Go to Descaling | Opens the descaling page |
| Go to AI Settings | Opens AI settings |
| Go to Visualizer | Opens the Visualizer browser |

### Machine Commands

| Action | Description |
|--------|-------------|
| Sleep | Puts the machine to sleep and shows the screensaver |
| Start Espresso | Begins an espresso extraction |
| Start Steam | Begins steaming |
| Start Hot Water | Dispenses hot water |
| Start Flush | Runs a flush cycle |
| Stop (Idle) | Stops the current operation and returns to idle |
| Tare Scale | Zeros the connected scale |

### Combining Text, HTML, and Actions

You can create rich custom buttons:

- `<span style="color:#e94560; font-size:28px">Settings</span>` with action "Go to Settings" — a large red settings shortcut
- `<b>%TEMP%°C</b>` with action "Go to Settings" — tappable temperature that opens settings
- `<span style="color:#00cc6d">Tare</span>` with action "Tare Scale" — a green tare button
- `<span style="font-size:48px">ZZZ</span>` with action "Sleep" — a big sleep button

When a text widget has an action, it renders with a border and press feedback so it's visually distinct from static text.

## Tips

- You can type HTML directly in the content field — the toolbar is just a shortcut
- The preview shows exactly how it will render on the home screen
- Use `<br>` for line breaks within a single widget
- Use the top/bottom bar zones for compact single-line info
- Use center zones for larger formatted text or action buttons
- Unstyled text uses the theme's default color (white on dark themes)
- Variables work inside HTML tags: `<b>%TEMP%</b>` renders the temperature in bold
