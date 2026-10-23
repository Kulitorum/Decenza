#pragma once

// Grind calibration section (add-grinder-ugs-calibration) — the web counterpart of
// GrindCalibrationSection.qml, shared by /equipment (every card, full) and
// /shot/<id> (compact, with the mark actions). Data comes from
// GET /api/equipment/<id>/calibration, which runs the same computation as the app.
// Codes are shared with the app; the wording is this surface's own, as for
// packageUpdateFailed (equipmentstorage.h).
//
// Needs escapeHtml (escape_js.h) and readJson on the page.
inline constexpr const char* WEB_JS_GRIND_CALIBRATION = R"JS(
        function grindCalibReason(code) {
            return ({
                notEspresso: 'only espresso shots can be anchors',
                tooLight: 'the shot weighs under 15 g',
                badged: 'the shot has a quality warning',
                noSetting: 'the shot has no numeric grind setting',
                noUgs: 'the profile is not on the UGS chart',
                noBean: 'the shot has no bean recorded',
                notInPackage: 'the shot is no longer on this equipment',
                noPackage: 'the shot has no equipment',
                noGrinder: 'the equipment has no grinder',
                sameShot: 'this shot is already the other anchor',
                differentCoffee: 'the two anchors must be on the same bag',
                differentRpm: 'the two anchors must use the same RPM',
                tooClose: 'the two anchors must be at least 2 UGS apart'
            })[code] || 'the anchor could not be saved';
        }

        function grindCalibStatus(c) {
            if (!c.hasShots) return 'No espresso shots with this equipment yet';
            if (!c.keyPublished) return 'Not enough data yet';
            if (c.basis === 'anchors') return 'From your anchors';
            if (c.basis === 'both') return c.agreesWithHistory ? 'From your history and anchors, which agree'
                                                               : 'From your history and anchors, which disagree';
            return 'From your shot history (' + c.historyPairs + ' pairs)';
        }

        function grindCalibAnchorLine(a) {
            if (!a || !(a.shotId > 0)) return 'not set';
            var parts = [];
            if (a.timestamp > 0) parts.push(new Date(a.timestamp * 1000).toLocaleString([], { month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' }));
            if (a.profileName) parts.push(a.profileName);
            if (a.setting) parts.push(a.setting);
            return parts.join(' · ');
        }

        // opts: { compact: bool, hostId: the element this renders into }
        function grindCalibHtml(c, opts) {
            var compact = !!(opts && opts.compact);
            var hostId = opts && opts.hostId;
            var guide = c.ugsGuideUrl
                ? " <a href='" + escapeHtml(c.ugsGuideUrl) + "' target='_blank' rel='noopener'>UGS guide (" + escapeHtml(c.ugsGuideUrl) + ")</a>"
                : '';
            var h = "<div class='attr-line'>Grind calibration: " + escapeHtml(grindCalibStatus(c)) + guide + "</div>";
            if (compact || !c.hasShots) return h;
            if (c.keyPublished && c.rangeFromProfile)
                h += "<div class='attr-line'>Grind numbers from " + escapeHtml(c.rangeFromProfile) + " to " + escapeHtml(c.rangeToProfile) + "</div>";
            ['fine', 'coarse'].forEach(function(side) {
                var a = c[side] || {};
                var line = (side === 'fine' ? 'Fine' : 'Coarse') + ': ' + escapeHtml(grindCalibAnchorLine(a));
                if (a.shotId > 0) line = "<a href='/shot/" + a.shotId + "'>" + line + "</a>";
                if (a.rejection) line += " (not counted: " + escapeHtml(grindCalibReason(a.rejection)) + ")";
                if (a.shotId > 0)
                    line += " <button class='small' onclick=\"clearGrindAnchor('" + hostId + "', " + c.packageId + ", '" + side + "')\">Clear</button>";
                h += "<div class='attr-line'>" + line + "</div>";
            });
            var p = c.pairStatus || '';
            if (p === 'differentCoffee' || p === 'differentRpm' || p === 'tooClose' || p === 'sameShot')
                h += "<div class='attr-line'>Anchors not used: " + escapeHtml(grindCalibReason(p)) + "</div>";
            var anyAnchor = (c.fine && c.fine.shotId > 0) || (c.coarse && c.coarse.shotId > 0);
            if (!anyAnchor)
                h += "<div class='attr-line'>" + (c.historyKeyValid
                    ? 'Anchors are optional and not needed: your history is enough.'
                    : 'Anchors are optional: mark a fine and a coarse shot on the same bag to calibrate.') + "</div>";
            return h;
        }

        function loadGrindCalibration(hostId, packageId, opts) {
            var host = document.getElementById(hostId);
            if (!host || !(packageId > 0)) return;
            var o = Object.assign({ hostId: hostId }, opts || {});
            fetch('/api/equipment/' + packageId + '/calibration').then(readJson)
                .then(function(c) { host.innerHTML = grindCalibHtml(c, o); })
                .catch(function(e) { host.textContent = 'Grind calibration unavailable: ' + e.message; });
        }

        function postGrindAnchor(packageId, side, shotId) {
            return fetch('/api/equipment/' + packageId + '/calibration', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ side: side, shotId: shotId || 0 })
            }).then(readJson);
        }

        function clearGrindAnchor(hostId, packageId, side) {
            postGrindAnchor(packageId, side, 0)
                .then(function() { loadGrindCalibration(hostId, packageId, {}); })
                .catch(function(e) { grindCalibNotify('Could not clear the anchor: ' + e.message); });
        }

        // The shot page has showToast, the management pages have status().
        function grindCalibNotify(msg) {
            if (typeof showToast === 'function') showToast(msg, 4000); else status(msg);
        }
)JS";
