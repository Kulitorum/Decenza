#include "diagnostics.h"

#include <QString>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>

EM_JS(int, countVideoElements, (), {
    return document.querySelectorAll("video").length;
});
#endif

int MediaDiagnostics::videoElementCount() const
{
#ifdef __EMSCRIPTEN__
    return countVideoElements();
#else
    return -1;
#endif
}

QString MediaDiagnostics::browserMediaReport() const
{
#ifdef __EMSCRIPTEN__
    char *report = reinterpret_cast<char *>(EM_ASM_PTR({
        const videos = document.querySelectorAll("video");
        const video = videos.length ? videos[videos.length - 1] : null;
        const canPlay = type => video ? video.canPlayType(type) : "no video element";
        const error = video && video.error;
        const value = ({
            videoElements: document.querySelectorAll("video").length,
            canPlayH264Baseline: canPlay('video/mp4; codecs="avc1.42E01E"'),
            canPlayH264Main: canPlay('video/mp4; codecs="avc1.4D401E"'),
            canPlayH264High: canPlay('video/mp4; codecs="avc1.64001E"'),
            canPlayWebMVP9: canPlay('video/webm; codecs="vp9"'),
            currentVideo: video ? {
                errorCode: error ? error.code : null,
                errorMessage: error ? error.message : "",
                readyState: video.readyState,
                networkState: video.networkState,
                ended: video.ended,
                currentTime: video.currentTime,
                duration: Number.isFinite(video.duration) ? video.duration : null,
                paused: video.paused
            } : null
        });
        return stringToNewUTF8(JSON.stringify(value));
    }));
    if (!report)
        return QStringLiteral("No browser media report available");
    const QString result = QString::fromUtf8(report);
    std::free(report);
    return result;
#else
    return QStringLiteral("Browser diagnostics require WebAssembly");
#endif
}
