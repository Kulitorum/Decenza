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
                        <a href="/legacy/" class="menu-item">&#127866; Shot History</a>
                        <a href="/legacy/recipes" class="menu-item">&#128209; Recipes</a>
                        <a href="/legacy/beans" class="menu-item">&#127793; Beans</a>
                        <a href="/legacy/equipment" class="menu-item">&#9881; Equipment</a>
                        <a href="/legacy/remote" class="menu-item">&#128421; Remote Control</a>
                        <a href="/legacy/settings" class="menu-item">&#128273; AI and Web Service Connections</a>)HTML";

#ifdef Q_OS_ANDROID
    if (includeUploadApk) {
        html += R"HTML(
                        <a href="/legacy/upload" class="menu-item">&#128230; Upload APK</a>)HTML";
    }
#else
    Q_UNUSED(includeUploadApk);
#endif

    html += R"HTML(
                        <a href="/database.db" class="menu-item">&#128190; Download Database</a>
                        <a href="/legacy/upload/media" class="menu-item">&#127912; Upload Screensaver Media</a>
                        <a href="/legacy/layout" class="menu-item">&#9998; Layout Editor</a>
                        <a href="/legacy/theme" class="menu-item">&#127752; Theme Editor</a>
                        <a href="/api/backup/full" class="menu-item">&#128230; Download Backup</a>
                        <a href="/legacy/restore" class="menu-item">&#128229; Restore Backup</a>
                        <a href="/legacy/ai-conversations" class="menu-item">&#129302; AI Conversations</a>
                        <a href="/legacy/debug" class="menu-item">&#128296; Debugging Tools</a>
                    </div>
                </div>)HTML";

    return html;
}
