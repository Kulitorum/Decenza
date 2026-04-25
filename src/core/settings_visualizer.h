#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// Visualizer (visualizer.coffee) upload settings + default shot rating.
class SettingsVisualizer : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString visualizerUsername READ visualizerUsername WRITE setVisualizerUsername NOTIFY visualizerUsernameChanged)
    Q_PROPERTY(QString visualizerPassword READ visualizerPassword WRITE setVisualizerPassword NOTIFY visualizerPasswordChanged)
    Q_PROPERTY(bool visualizerAutoUpload READ visualizerAutoUpload WRITE setVisualizerAutoUpload NOTIFY visualizerAutoUploadChanged)
    Q_PROPERTY(double visualizerMinDuration READ visualizerMinDuration WRITE setVisualizerMinDuration NOTIFY visualizerMinDurationChanged)
    Q_PROPERTY(bool visualizerExtendedMetadata READ visualizerExtendedMetadata WRITE setVisualizerExtendedMetadata NOTIFY visualizerExtendedMetadataChanged)
    Q_PROPERTY(bool visualizerShowAfterShot READ visualizerShowAfterShot WRITE setVisualizerShowAfterShot NOTIFY visualizerShowAfterShotChanged)
    Q_PROPERTY(bool visualizerClearNotesOnStart READ visualizerClearNotesOnStart WRITE setVisualizerClearNotesOnStart NOTIFY visualizerClearNotesOnStartChanged)
    Q_PROPERTY(int defaultShotRating READ defaultShotRating WRITE setDefaultShotRating NOTIFY defaultShotRatingChanged)

public:
    explicit SettingsVisualizer(QObject* parent = nullptr);

    QString visualizerUsername() const;
    void setVisualizerUsername(const QString& username);

    QString visualizerPassword() const;
    void setVisualizerPassword(const QString& password);

    bool visualizerAutoUpload() const;
    void setVisualizerAutoUpload(bool enabled);

    double visualizerMinDuration() const;
    void setVisualizerMinDuration(double seconds);

    bool visualizerExtendedMetadata() const;
    void setVisualizerExtendedMetadata(bool enabled);

    bool visualizerShowAfterShot() const;
    void setVisualizerShowAfterShot(bool enabled);

    bool visualizerClearNotesOnStart() const;
    void setVisualizerClearNotesOnStart(bool enabled);

    int defaultShotRating() const;
    void setDefaultShotRating(int rating);

signals:
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerAutoUploadChanged();
    void visualizerMinDurationChanged();
    void visualizerExtendedMetadataChanged();
    void visualizerShowAfterShotChanged();
    void visualizerClearNotesOnStartChanged();
    void defaultShotRatingChanged();

private:
    mutable QSettings m_settings;
};
