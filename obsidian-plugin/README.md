# Decenza DE1 Espresso — Obsidian Plugin

Display espresso shot data from your Decenza DE1 controller directly in your Obsidian notes. Browse shots, embed interactive charts, and build your coffee journal without leaving Obsidian.

## Requirements

- [Obsidian](https://obsidian.md) v1.0.0 or later (desktop or Android)
- [Decenza DE1](https://github.com/Kulitorum/de1-qt) app running on the same WiFi network
- **Remote Access** enabled in the Decenza app

> **Note:** This plugin uses HTTP to communicate with the Decenza app over your local network. It works on desktop (Windows, macOS, Linux) and Android. iOS is not supported because Obsidian on iOS requires HTTPS connections.

## Setup

### 1. Enable Remote Access in Decenza

On your device running Decenza:

1. Open **Settings**
2. Go to the **Shot History** tab
3. Enable **Remote Access**
4. Note the URL shown (e.g., `http://192.168.1.100:8888`)

### 2. Install the Plugin

**Manual installation:**

1. Download `main.js`, `manifest.json`, and `styles.css` from the latest release
2. Create a folder called `decenza-de1` inside your vault's `.obsidian/plugins/` directory
3. Copy the three files into that folder
4. Restart Obsidian

**From source:**

```bash
cd /path/to/your/vault/.obsidian/plugins
git clone https://github.com/Kulitorum/de1-qt
cd de1-qt/obsidian-plugin
npm install
npm run build
```

### 3. Enable and Configure

1. Open Obsidian **Settings** (gear icon or `Ctrl+,` / `Cmd+,`)
2. Go to **Community plugins**
3. Enable **Decenza DE1 Espresso**
4. Click the gear icon next to the plugin name to open its settings
5. Enter your Decenza server URL (e.g., `http://192.168.1.100:8888`)
6. Click **Test** to verify the connection

## Features

### Embed a Shot Chart

To display a single shot with its extraction chart, add a `decenza-shot` code block to any note. Put the shot ID inside the block:

````markdown
```decenza-shot
42
```
````

This renders:

- **Metadata header** — date, profile name, bean, grinder, dose/yield, ratio, duration, and star rating
- **Extraction chart** — pressure, flow, and weight curves over time, with phase markers and goal lines
- **Notes** — your tasting notes for that shot (if any)

#### Choosing Which Curves to Show

By default, the chart shows pressure, flow, and weight. You can customize this with JSON:

````markdown
```decenza-shot
{"id": 42, "charts": ["pressure", "flow", "weight", "temperature"]}
```
````

Available chart types: `pressure`, `flow`, `weight`, `temperature`

#### Chart Colors

| Curve | Color | Style |
|-------|-------|-------|
| Pressure | Green (#18c37e) | Solid |
| Pressure Goal | Light green (#69fdb3) | Dashed |
| Flow | Blue (#4e85f4) | Solid |
| Flow Goal | Light blue (#7aaaff) | Dashed |
| Yield (weight) | Brown (#a2693d) | Solid |
| Temperature | Red (#e73249) | Solid |

Phase transitions are shown as vertical dotted lines with labels indicating the transition reason: [W]eight, [P]ressure, [F]low, or [T]ime.

### Embed a Shot Table

To display a table of recent shots, add a `decenza-shots` code block:

````markdown
```decenza-shots
```
````

This shows the 10 most recent shots in a table with columns for date, profile, bean, dose/yield, ratio, rating, and grinder setting.

#### Filtering and Limiting

Use JSON to filter by text or change the number of results:

````markdown
```decenza-shots
{"limit": 20, "filter": "Ethiopian"}
```
````

- **limit** — number of shots to show (default: 10)
- **filter** — text to match against profile name, bean brand, bean type, or grinder setting

You can also just type a filter string directly:

````markdown
```decenza-shots
Ethiopian
```
````

### Sidebar Browser

A sidebar panel lets you browse and search all your shots:

1. Click the **coffee cup icon** in the left ribbon, or
2. Open the command palette (`Ctrl+P` / `Cmd+P`) and run **Decenza: Browse shots**

The sidebar shows a scrollable list of all shots with date, profile name, bean info, and ratio. Use the search box at the top to filter.

**Click any shot** in the sidebar to insert a `decenza-shot` code block at your cursor position in the current note.

Click **Refresh** to reload the shot list from the Decenza app.

### Insert Shot Command

For quick insertion without opening the sidebar:

1. Place your cursor where you want the chart in your note
2. Open the command palette (`Ctrl+P` / `Cmd+P`)
3. Run **Decenza: Insert shot**
4. Search for a shot by profile name, bean, date, or ID
5. Select a shot from the list

The plugin inserts a `decenza-shot` code block at your cursor.

## Example: Coffee Journal

Here's how you might structure a daily coffee note:

````markdown
# Coffee Notes — 2026-01-31

## Morning Shot

Dialed in the Ethiopian Yirgacheffe today. Went a bit finer on the grinder.

```decenza-shot
247
```

Tasted bright and fruity — much better than yesterday's attempt.

## This Week's Shots

```decenza-shots
{"limit": 7}
```
````

## Finding Shot IDs

Shot IDs are shown:

- In the **sidebar browser** (the `#number` at the end of each entry)
- In the **Insert shot** modal (shown after the bean info)
- In the Decenza app's **web interface** (visit the server URL in a browser)
- In the **shot table** — hover over a row to see the ID

## Troubleshooting

### "Decenza server URL not configured"

Open Obsidian Settings → Decenza DE1 and enter the server URL.

### "Connection failed"

- Verify the Decenza app is running and **Remote Access** is enabled
- Verify both devices are on the same WiFi network
- Try opening the server URL directly in your browser (e.g., `http://192.168.1.100:8888`)
- Check that no firewall is blocking port 8888

### Chart not rendering

- Switch to **Reading view** or **Live Preview** mode — charts don't render in Source mode
- Verify the shot ID exists (try it in the browser: `http://your-server:8888/shot/42`)

### "Shot #X not found"

The shot ID doesn't exist on the server. Use the sidebar or Insert Shot command to browse available shots.

### Plugin not appearing in settings

- Make sure the plugin files (`main.js`, `manifest.json`, `styles.css`) are in `.obsidian/plugins/decenza-de1/`
- Restart Obsidian
- Check that Community Plugins are enabled in Settings

## Building from Source

```bash
cd obsidian-plugin
npm install
npm run build    # production build (minified)
npm run dev      # development build (with source maps)
```

The build produces `main.js` in the plugin root directory.
