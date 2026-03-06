#pragma once

#include <QString>

// Vital Stats: self-contained script that injects machine status into the page header
// Fetches the user's status bar layout from /api/layout and theme colors from /api/theme,
// then renders matching widgets. Polls /api/telemetry every 3 seconds.

inline QString generateVitalStatsScript()
{
    return QStringLiteral(R"HTML(
<script>
(function() {
    var MUTED = '#8b949e';

    // Theme colors — populated from /api/theme, fallbacks match Theme.qml defaults
    var TC = {
        primary: '#4e85f4',
        success: '#00cc6d',
        warning: '#ffaa00',
        error: '#ff4444',
        temp: '#e73249',
        textSecondary: MUTED
    };

    function injectStyles() {
        var style = document.createElement('style');
        style.textContent =
            '.vital-stats { display: flex; align-items: center; gap: 0.6rem; font-size: 0.8rem; font-weight: 500; }' +
            '.vital-stat { display: flex; align-items: center; gap: 0.25rem; white-space: nowrap; }' +
            '.vital-dot { width: 8px; height: 8px; border-radius: 50%; background: ' + MUTED + '; display: inline-block; flex-shrink: 0; }' +
            '.vital-dot.connected { background: ' + TC.success + '; }' +
            '.vital-dot.disconnected { background: ' + TC.error + '; }' +
            '.vital-sep { color: #30363d; font-size: 0.7rem; }' +
            '.vital-spacer { flex: 1; }' +
            '.vital-battery-icon { display: inline-block; width: 1.1em; height: 0.7em; border: 1.5px solid currentColor; border-radius: 2px; position: relative; vertical-align: middle; margin-right: 0.15em; }' +
            '.vital-battery-icon::after { content: ""; position: absolute; right: -3.5px; top: 25%; width: 2px; height: 50%; background: currentColor; border-radius: 0 1px 1px 0; }' +
            '.vital-battery-fill { position: absolute; left: 1px; top: 1px; bottom: 1px; border-radius: 1px; }' +
            '@media (max-width: 600px) { .vital-stats { gap: 0.4rem; font-size: 0.7rem; } .vital-state-text { display: none; } }';
        document.head.appendChild(style);
    }

    // Default items if no layout configured
    var DEFAULT_ITEMS = [
        {type:'connectionStatus'}, {type:'separator'}, {type:'temperature'},
        {type:'separator'}, {type:'waterLevel'}
    ];

    // Renderable status bar types
    var RENDERABLE = {
        separator:1, spacer:1, temperature:1, waterLevel:1,
        connectionStatus:1, machineStatus:1, scaleWeight:1,
        steamTemperature:1, batteryLevel:1
    };

    // Build DOM for a single status bar item
    function renderItem(item) {
        var t = item.type;
        if (!RENDERABLE[t]) return null;

        if (t === 'separator') {
            var sep = document.createElement('span');
            sep.className = 'vital-sep';
            sep.textContent = '|';
            return sep;
        }
        if (t === 'spacer') {
            var sp = document.createElement('span');
            sp.className = 'vital-spacer';
            return sp;
        }

        var wrap = document.createElement('span');
        wrap.className = 'vital-stat';

        if (t === 'temperature') {
            wrap.title = 'Group head temperature';
            var s = document.createElement('span');
            s.id = 'vitalTemp'; s.style.color = TC.temp; s.textContent = '--';
            wrap.appendChild(s);
        } else if (t === 'waterLevel') {
            wrap.title = 'Water level';
            var s = document.createElement('span');
            s.id = 'vitalWater'; s.style.color = MUTED; s.textContent = '--';
            wrap.appendChild(s);
        } else if (t === 'connectionStatus' || t === 'machineStatus') {
            wrap.title = 'Machine status';
            var dot = document.createElement('span');
            dot.className = 'vital-dot'; dot.id = 'vitalDot';
            wrap.appendChild(dot);
            var label = document.createElement('span');
            label.className = 'vital-state-text'; label.id = 'vitalState';
            label.style.color = MUTED; label.textContent = '--';
            wrap.appendChild(label);
        } else if (t === 'scaleWeight') {
            wrap.title = 'Scale weight';
            var s = document.createElement('span');
            s.id = 'vitalScale'; s.style.color = MUTED; s.textContent = '--';
            wrap.appendChild(s);
        } else if (t === 'steamTemperature') {
            wrap.title = 'Steam temperature';
            var s = document.createElement('span');
            s.id = 'vitalSteam'; s.style.color = TC.warning; s.textContent = '--';
            wrap.appendChild(s);
        } else if (t === 'batteryLevel') {
            wrap.title = 'Battery level';
            var outer = document.createElement('span');
            outer.id = 'vitalBattery'; outer.style.color = MUTED;
            var icon = document.createElement('span');
            icon.className = 'vital-battery-icon';
            var fill = document.createElement('span');
            fill.className = 'vital-battery-fill'; fill.id = 'vitalBatteryFill';
            icon.appendChild(fill);
            outer.appendChild(icon);
            var txt = document.createElement('span');
            txt.id = 'vitalBatteryText'; txt.textContent = '--';
            outer.appendChild(txt);
            wrap.appendChild(outer);
        }
        return wrap;
    }

    function buildStats(items) {
        var stats = document.createElement('div');
        stats.className = 'vital-stats';
        stats.id = 'vitalStats';
        for (var i = 0; i < items.length; i++) {
            var el = renderItem(items[i]);
            if (el) stats.appendChild(el);
        }
        return stats;
    }

    function insertStats(stats) {
        var hc = document.querySelector('.header-content');
        if (!hc) return;
        var hr = hc.querySelector('.header-right');
        var mw = hc.querySelector(':scope > .menu-wrapper');
        if (hr) {
            var m = hr.querySelector('.menu-wrapper');
            if (m) hr.insertBefore(stats, m);
            else hr.insertBefore(stats, hr.firstChild);
        } else if (mw) {
            hc.insertBefore(stats, mw);
        } else {
            stats.style.marginLeft = 'auto';
            hc.appendChild(stats);
        }
    }

    function phaseColor(phase) {
        if (!phase) return MUTED;
        var p = phase.toLowerCase();
        if (p === 'disconnected') return TC.error;
        if (p === 'heating' || p === 'refill') return TC.warning;
        if (p === 'ready') return TC.success;
        if (p === 'sleep' || p === 'idle' || p === 'ending') return TC.textSecondary;
        return TC.primary;
    }

    function waterColor(ml) {
        if (ml < 200) return TC.error;
        if (ml < 400) return TC.warning;
        return TC.primary;
    }

    function batteryColor(pct) {
        if (pct > 50) return TC.success;
        if (pct > 20) return TC.warning;
        return TC.error;
    }

    function update(d) {
        var el;
        var offline = !d.connected;

        el = document.getElementById('vitalDot');
        if (el) el.className = 'vital-dot ' + (offline ? 'disconnected' : 'connected');
        el = document.getElementById('vitalState');
        if (el) {
            if (offline) { el.textContent = 'Offline'; el.style.color = TC.error; }
            else {
                var label = d.phase || d.state || 'Connected';
                el.textContent = label;
                el.style.color = phaseColor(d.phase);
            }
        }

        el = document.getElementById('vitalTemp');
        if (el) {
            if (offline || d.temperature === undefined || d.temperature <= 0) {
                el.textContent = '--'; el.style.color = MUTED;
            } else {
                el.textContent = d.temperature.toFixed(1) + '\u00b0C'; el.style.color = TC.temp;
            }
        }

        el = document.getElementById('vitalWater');
        if (el) {
            if (offline || d.waterLevelMl === undefined) {
                el.textContent = '--'; el.style.color = MUTED;
            } else if (d.waterLevelDisplayUnit === 'ml') {
                el.textContent = d.waterLevelMl + ' ml'; el.style.color = waterColor(d.waterLevelMl);
            } else {
                var pct = d.waterLevel !== undefined ? Math.round(d.waterLevel) : '--';
                el.textContent = pct + '%'; el.style.color = waterColor(d.waterLevelMl);
            }
        }

        el = document.getElementById('vitalScale');
        if (el) {
            if (offline || d.scaleWeight === undefined) {
                el.textContent = '--'; el.style.color = MUTED;
            } else {
                el.textContent = d.scaleWeight.toFixed(1) + 'g'; el.style.color = MUTED;
            }
        }

        el = document.getElementById('vitalSteam');
        if (el) {
            if (offline || d.steamTemperature === undefined || d.steamTemperature <= 0) {
                el.textContent = '--'; el.style.color = MUTED;
            } else {
                el.textContent = d.steamTemperature.toFixed(0) + '\u00b0C'; el.style.color = TC.warning;
            }
        }

        el = document.getElementById('vitalBatteryText');
        if (el) {
            var fill = document.getElementById('vitalBatteryFill');
            if (d.batteryPercent === undefined) {
                el.textContent = '--'; el.parentElement.style.color = MUTED;
                if (fill) { fill.style.width = '0%'; fill.style.background = MUTED; }
            } else {
                var pct = d.batteryPercent;
                var bc = batteryColor(pct);
                el.textContent = pct + '%'; el.parentElement.style.color = bc;
                if (fill) { fill.style.width = Math.max(0, Math.min(100, pct)) + '%'; fill.style.background = bc; }
            }
        }
    }

    function poll() {
        fetch('/api/telemetry')
            .then(function(r) { if (!r.ok) throw new Error(r.status); return r.json(); })
            .then(update)
            .catch(function() {
                var dot = document.getElementById('vitalDot');
                if (dot) dot.className = 'vital-dot disconnected';
                var st = document.getElementById('vitalState');
                if (st) { st.textContent = 'Offline'; st.style.color = TC.error; }
            });
    }

    // Fetch theme and layout in parallel, then build UI
    Promise.all([
        fetch('/api/theme').then(function(r) { return r.ok ? r.json() : {}; }).catch(function() { return {}; }),
        fetch('/api/layout').then(function(r) { return r.ok ? r.json() : {}; }).catch(function() { return {}; })
    ]).then(function(results) {
        var theme = results[0];
        var layout = results[1];

        // Apply theme colors
        var c = (theme && theme.colors) || {};
        if (c.primaryColor) TC.primary = c.primaryColor;
        if (c.successColor) TC.success = c.successColor;
        if (c.warningColor) TC.warning = c.warningColor;
        if (c.errorColor) TC.error = c.errorColor;
        if (c.temperatureColor) TC.temp = c.temperatureColor;
        if (c.textSecondaryColor) TC.textSecondary = c.textSecondaryColor;

        injectStyles();

        var items = DEFAULT_ITEMS;
        if (layout && layout.zones && layout.zones.statusBar && layout.zones.statusBar.length > 0) {
            items = layout.zones.statusBar;
        }
        insertStats(buildStats(items));
        poll();
    });

    var vitalTimer = setInterval(poll, 3000);
    document.addEventListener('visibilitychange', function() {
        if (document.hidden) {
            clearInterval(vitalTimer);
        } else {
            poll();
            vitalTimer = setInterval(poll, 3000);
        }
    });
})();
</script>
)HTML");
}
