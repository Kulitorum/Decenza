#pragma once

#include <QString>

// Vital Stats: self-contained script that injects machine status into the page header
// Shows connection status, machine state, temperature, and water level
// Polls /api/telemetry every 3 seconds

inline QString generateVitalStatsScript()
{
    return QStringLiteral(R"HTML(
<script>
(function() {
    var style = document.createElement('style');
    style.textContent =
        '.vital-stats { display: flex; align-items: center; gap: 0.6rem; font-size: 0.8rem; font-weight: 500; }' +
        '.vital-stat { display: flex; align-items: center; gap: 0.25rem; white-space: nowrap; }' +
        '.vital-dot { width: 8px; height: 8px; border-radius: 50%; background: #8b949e; display: inline-block; flex-shrink: 0; }' +
        '.vital-dot.connected { background: #18c37e; }' +
        '.vital-dot.disconnected { background: #f85149; }' +
        '.vital-sep { color: #30363d; font-size: 0.7rem; }' +
        '@media (max-width: 600px) { .vital-stats { gap: 0.4rem; font-size: 0.7rem; } .vital-state-text { display: none; } }';
    document.head.appendChild(style);

    var stats = document.createElement('div');
    stats.className = 'vital-stats';
    stats.id = 'vitalStats';
    stats.innerHTML =
        '<span class="vital-stat" title="Connection status">' +
            '<span class="vital-dot" id="vitalDot"></span>' +
            '<span class="vital-state-text" id="vitalState" style="color:#8b949e">--</span>' +
        '</span>' +
        '<span class="vital-sep">|</span>' +
        '<span class="vital-stat" title="Group head temperature">' +
            '<span id="vitalTempValue" style="color:#e73249">--</span>' +
        '</span>' +
        '<span class="vital-sep">|</span>' +
        '<span class="vital-stat" title="Water level">' +
            '<span id="vitalWaterValue" style="color:#8b949e">--</span>' +
        '</span>';

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

    function update() {
        fetch('/api/telemetry')
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var dot = document.getElementById('vitalDot');
                var st = document.getElementById('vitalState');
                var tmp = document.getElementById('vitalTempValue');
                var wtr = document.getElementById('vitalWaterValue');
                if (!dot) return;

                if (d.connected) {
                    dot.className = 'vital-dot connected';
                    st.textContent = d.state || 'Connected';
                    st.style.color = '#8b949e';
                } else {
                    dot.className = 'vital-dot disconnected';
                    st.textContent = 'Offline';
                    st.style.color = '#f85149';
                    tmp.textContent = '--';
                    tmp.style.color = '#8b949e';
                    wtr.textContent = '--';
                    wtr.style.color = '#8b949e';
                    return;
                }

                if (d.temperature !== undefined && d.temperature > 0) {
                    tmp.textContent = d.temperature.toFixed(1) + '\u00b0C';
                    tmp.style.color = '#e73249';
                } else {
                    tmp.textContent = '--';
                    tmp.style.color = '#8b949e';
                }

                if (d.waterLevelMl !== undefined) {
                    wtr.textContent = d.waterLevelMl + ' ml';
                    if (d.waterLevelMl < 200) {
                        wtr.style.color = '#f85149';
                    } else if (d.waterLevelMl < 400) {
                        wtr.style.color = '#d29922';
                    } else {
                        wtr.style.color = '#18c37e';
                    }
                } else {
                    wtr.textContent = '--';
                    wtr.style.color = '#8b949e';
                }
            })
            .catch(function() {
                var dot = document.getElementById('vitalDot');
                if (dot) dot.className = 'vital-dot disconnected';
                var st = document.getElementById('vitalState');
                if (st) { st.textContent = 'Offline'; st.style.color = '#f85149'; }
            });
    }

    update();
    var vitalTimer = setInterval(update, 3000);
    document.addEventListener('visibilitychange', function() {
        if (document.hidden) {
            clearInterval(vitalTimer);
        } else {
            update();
            vitalTimer = setInterval(update, 3000);
        }
    });
})();
</script>
)HTML");
}
