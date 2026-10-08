#pragma once

#include <QString>
#include <functional>

class QObject;
class QSqlDatabase;

// A place saved shots are uploaded to (Visualizer, the Decent account).
// ShotUploads decides when, makes the attempts and records failures and
// rejections; a destination makes one attempt, reads its server's answer and
// records its own upload. A destination gets one shot at a time.
class ShotUploadDestination {
public:
    enum class Send {
        UploadOrUpdate,  // first upload, or bring the uploaded copy up to date
        UpdateOnly,      // only a shot already uploaded here; otherwise nothing
    };

    // What one attempt came to, the same for every destination (D15).
    enum class Outcome {
        Sent,            // the destination holds the shot as saved
        NothingToSend,   // nothing sent, nothing recorded: ineligible, not held, unchanged, unreadable, no machine or account
        Transient,       // no answer, or one that says try again (retried, then failed)
        AuthFailed,      // the account's credentials were refused
        AccountRefused,  // the account cannot take shots (Decent: unregistered machine)
        Rejected,        // this shot is refused for good
    };
    struct Attempt {
        Outcome outcome = Outcome::NothingToSend;
        int httpStatus = 0;
        int attempts = 1;   // how many the send made, set by ShotUploads before sendFinished()
        bool background = false;   // sent by Upload missing shots, also set by ShotUploads
    };

    virtual ~ShotUploadDestination() = default;

    // What an HTTP answer to a send means, the same for every destination (D15).
    // A destination handles its server's own exceptions first (a Visualizer PATCH 404).
    static Outcome responseOutcome(int httpStatus, bool transportError) {
        if (transportError) return Outcome::Transient;
        if (httpStatus >= 200 && httpStatus < 300) return Outcome::Sent;
        if (httpStatus == 401) return Outcome::AuthFailed;
        // Decent: the machine's serial is not in the account. Visualizer: the account is refused.
        if (httpStatus == 403) return Outcome::AccountRefused;
        // 404/405/410 say the endpoint is wrong, 408/429 say try later: none says the shot is bad (Decaid's table).
        if (httpStatus == 404 || httpStatus == 405 || httpStatus == 408 || httpStatus == 410 || httpStatus == 429)
            return Outcome::Transient;
        if (httpStatus >= 400 && httpStatus < 500) return Outcome::Rejected;
        return Outcome::Transient;
    }

    // For MCP responses and the per-destination columns: "visualizer", "decent".
    virtual QString name() const = 0;
    // Switched on, with an account that can be used now.
    virtual bool isActive() const = 0;
    // Whether the shot is already uploaded here. Reads only `db`, so it runs on a
    // worker thread.
    virtual bool holdsShot(QSqlDatabase& db, qint64 shotId) const = 0;
    // SQL conditions on `shots`, for Upload missing shots (D14): the shot is
    // uploaded here, and it has an edit this destination has not received.
    virtual QString heldCondition() const = 0;
    virtual QString unsentEditCondition() const = 0;
    // One attempt: reads the saved row, after any write already queued, sends
    // it, and ends with finishAttempt(). A retry calls it again.
    virtual void attemptSavedShot(qint64 shotId, Send how) = 0;
    // The send is over: no more attempts follow. `last` is its final attempt.
    virtual void sendFinished(qint64 shotId, Attempt last) = 0;
    // Every successful edit of a saved shot, whether or not anything is sent.
    virtual void noteEdited(qint64 shotId) { Q_UNUSED(shotId); }
    // Whether a background send (Upload missing shots) of this kind could go now;
    // false holds it until ShotUploads::readinessChanged().
    virtual bool backgroundSendReady(Send how) const { Q_UNUSED(how); return true; }
    // What a destination that is not ready waits for, as ShotUploads::missing() names it.
    virtual QString notReadyStatus() const { return QStringLiteral("waitingForMachine"); }
    // Runs `send` when a background send (Upload missing shots) may go, paced with
    // the destination's other background requests; dropped if `context` is destroyed.
    virtual void paceBackground(QObject* context, std::function<void()> send) { Q_UNUSED(context); send(); }

    void setAttemptCallback(std::function<void(Attempt)> onAttempt) { m_onAttempt = std::move(onAttempt); }

protected:
    void finishAttempt(Attempt attempt) { if (m_onAttempt) m_onAttempt(attempt); }

private:
    std::function<void(Attempt)> m_onAttempt;
};
