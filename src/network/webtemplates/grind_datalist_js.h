#pragma once

// Shared grind/RPM <datalist> helper (replace-grind-inputs-with-picker) —
// the web counterpart of the in-app grind picker. One native element that is
// simultaneously the quick-select and the improved text entry: the input keeps
// free text (grinder notation is opaque — "C4", "3+2", "medium-fine"), while
// the attached <datalist> offers the same stepped candidates the app's wheels
// show. Deliberately NOT a ported tumbler: the wheel is a touch-first
// affordance and these pages are used with a keyboard.
//
// Candidates come from GET /api/grind-candidates — computed server-side by the
// existing C++ stepping machinery, resolved against the grinder identity the
// PAGE passes in (the shot's grinder, the bag's linked package, the recipe's
// package — never the active grinder). Nothing is stepped in browser JS.
//
// Embedded by the shot detail, /beans and /recipes pages; do not re-inline a
// private copy per page (SHOTSERVER.md reuse rule).
inline constexpr const char* WEB_JS_GRIND_DATALIST = R"JS(
        // Fill (or create) the <datalist> for an input and point the input at it.
        function fillGrindDatalist(inputEl, values) {
            if (!inputEl) return;
            var listId = inputEl.id + '_datalist';
            var dl = document.getElementById(listId);
            if (!dl) {
                dl = document.createElement('datalist');
                dl.id = listId;
                inputEl.parentNode.appendChild(dl);
                inputEl.setAttribute('list', listId);
            }
            dl.innerHTML = (values || []).map(function(v) {
                return '<option value="' + String(v).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
                       .replace(/</g, '&lt;') + '"></option>';
            }).join('');
        }

        // Attach stepped candidates to a grind input (and optionally an RPM
        // input) for the given grinder identity. Candidates are an enhancement
        // over free text: on any failure the inputs simply stay plain text
        // fields, so the .catch is deliberately quiet.
        function attachGrindDatalist(grindEl, rpmEl, brand, model) {
            if (!grindEl) return;
            var current = (grindEl.value || '').trim();
            var rpm = rpmEl ? (parseInt(rpmEl.value, 10) || 0) : 0;
            fetch('/api/grind-candidates?brand=' + encodeURIComponent(brand || '')
                  + '&model=' + encodeURIComponent(model || '')
                  + '&current=' + encodeURIComponent(current)
                  + '&rpm=' + rpm)
                .then(function(r) {
                    if (!r.ok) throw new Error('HTTP ' + r.status);
                    return r.json();
                })
                .then(function(d) {
                    fillGrindDatalist(grindEl, d.grind || []);
                    if (rpmEl) {
                        var rpmList = d.rpm || [];
                        fillGrindDatalist(rpmEl, rpmList);
                        // Capability gate, same source of truth as the app:
                        // the server fills rpm candidates only when
                        // grinderRpmCapable(brand, model) holds for the
                        // record's own grinder — an empty list means "not
                        // RPM-capable", so hide the field's row (it stays in
                        // the DOM, loaded and saved; a stale stored RPM is
                        // never silently cleared).
                        var row = rpmEl.closest('.edit-row') || rpmEl.parentElement;
                        if (row) row.style.display = rpmList.length ? '' : 'none';
                    }
                })
                .catch(function() { /* free text still works without candidates */ });
        }
)JS";
