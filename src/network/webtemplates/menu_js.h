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
                .then(function() { updatePowerStatus(); });
        }

        function updatePowerStatus() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    var el = document.getElementById("powerToggle");
                    if (data.awake) {
                        el.textContent = "💤 Sleep";
                        el.dataset.awake = "true";
                    } else {
                        el.textContent = "⚡ Wake";
                        el.dataset.awake = "false";
                    }
                });
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
