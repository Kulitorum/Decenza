#pragma once

// Menu JavaScript: toggle menu, power control, click-outside-to-close
// Used by all web pages with the header menu

inline constexpr const char* WEB_JS_MENU = R"JS(
        function toggleMenu() {
            document.getElementById("menuDropdown").classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            if (menu && !e.target.closest(".menu-btn") && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        function togglePower() {
            var el = document.getElementById("powerToggle");
            var isAwake = el.dataset.awake === "true";
            fetch(isAwake ? "/api/power/sleep" : "/api/power/wake", { method: "POST" })
                .then(function() { updatePowerStatus(); })
                .catch(function(e) { console.warn('Power toggle failed:', e); });
        }

        function updatePowerStatus() {
            fetch("/api/power/status")
                .then(function(r) {
                    if (!r.ok) throw new Error('Server error (' + r.status + ')');
                    return r.json();
                })
                .then(function(data) {
                    var el = document.getElementById("powerToggle");
                    if (data.awake) {
                        el.textContent = "💤 Sleep";
                        el.dataset.awake = "true";
                    } else {
                        el.textContent = "⚡ Wake";
                        el.dataset.awake = "false";
                    }
                })
                .catch(function(e) { console.warn('Power status update failed:', e); });
        }

        updatePowerStatus();
        var powerTimer = setInterval(updatePowerStatus, 10000);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) {
                clearInterval(powerTimer);
            } else {
                updatePowerStatus();
                powerTimer = setInterval(updatePowerStatus, 10000);
            }
        });
)JS";

// Power control JavaScript for shot pages (list, detail, comparison)
// Uses powerState object pattern with "powerToggle" button element.
// innerHTML values are hardcoded HTML entity strings (not user input).
inline constexpr const char* WEB_JS_POWER_CONTROL = R"JS(
        var powerState = {awake: false, state: "Unknown"};
        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }
        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error (" + r.status + ")");
                    return r.json();
                })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function(e) { console.warn("fetchPowerState:", e.message); });
        }
        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) {
                    if (!r.ok) throw new Error("Server error (" + r.status + ")");
                    return r.json();
                })
                .then(function() { setTimeout(fetchPowerState, 1000); })
                .catch(function(e) { alert("Power toggle failed: " + e.message); });
        }
        fetchPowerState();
        var _pwrTimer = setInterval(fetchPowerState, 5000);
        document.addEventListener("visibilitychange", function() {
            if (document.hidden) { clearInterval(_pwrTimer); }
            else { fetchPowerState(); _pwrTimer = setInterval(fetchPowerState, 5000); }
        });
)JS";
