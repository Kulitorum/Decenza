#pragma once

#include <QString>

// Menu HTML: generates the burger menu dropdown
// Used by all web pages with the header menu

inline QString generateMenuHtml(bool includeUploadApk = false)
{
    QString html = R"HTML(
                <div class="menu-wrapper">
                    <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                    <div class="menu-dropdown" id="menuDropdown">
                        <a href="#" class="menu-item" id="powerToggle" onclick="togglePower(); return false;">&#9889; Loading...</a>
                        <a href="/" class="menu-item">&#127866; Shot History</a>
                        <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                        <a href="/remote" class="menu-item">&#128421; Remote Control</a>
                        <a href="/settings" class="menu-item">&#128273; API Keys &amp; Settings</a>)HTML";

#ifdef Q_OS_ANDROID
    if (includeUploadApk) {
        html += R"HTML(
                        <a href="/upload" class="menu-item">&#128230; Upload APK</a>)HTML";
    }
#else
    Q_UNUSED(includeUploadApk);
#endif

    html += R"HTML(
                        <a href="/database.db" class="menu-item">&#128190; Download Database</a>
                        <a href="/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                    </div>
                </div>)HTML";

    return html;
}
