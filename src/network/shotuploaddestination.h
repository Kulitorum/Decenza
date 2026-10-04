#pragma once

#include <QString>
#include <functional>

class QSqlDatabase;

// A place saved shots are uploaded to (Visualizer, the Decent account).
// ShotUploads decides when and queues; a destination only knows how. A
// destination gets one shot at a time: sendSavedShot() is never called while
// busy().
class ShotUploadDestination {
public:
    enum class Send {
        UploadOrUpdate,  // first upload, or bring the uploaded copy up to date
        UpdateOnly,      // only a shot already uploaded here; otherwise nothing
    };

    virtual ~ShotUploadDestination() = default;

    // For MCP responses: "visualizer", "decent".
    virtual QString name() const = 0;
    // Switched on, with an account that can be used now.
    virtual bool isActive() const = 0;
    virtual bool busy() const = 0;
    // Whether the shot is already uploaded here. Reads only `db`, so it runs on a
    // worker thread.
    virtual bool holdsShot(QSqlDatabase& db, qint64 shotId) const = 0;
    // Reads the saved row, after any write already queued, and sends it. Ends
    // with notifyIdle(), whether or not anything was sent; never called while busy().
    virtual void sendSavedShot(qint64 shotId, Send how) = 0;
    // Every successful edit of a saved shot, whether or not anything is sent.
    virtual void noteEdited(qint64 shotId) { Q_UNUSED(shotId); }

    void setIdleCallback(std::function<void()> onIdle) { m_onIdle = std::move(onIdle); }

protected:
    void notifyIdle() { if (m_onIdle) m_onIdle(); }

private:
    std::function<void()> m_onIdle;
};
