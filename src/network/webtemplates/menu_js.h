#pragma once

// Menu JavaScript: toggle menu dropdown, click-outside-to-close
// Used by all web pages with the header menu (pair with WEB_JS_POWER_CONTROL)

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
)JS";

// Power control JavaScript for all pages with the menu "powerToggle" button.
// Uses powerState object pattern with disconnected/awake/sleep states.
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
            fetch("/api/power/" + action, { method: "POST" })
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
