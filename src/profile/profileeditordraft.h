#pragma once

#include "profile.h"

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// A QML-facing isolated copy of a profile being edited. It deliberately has no
// controller, settings, storage, or machine dependency; saving is a separate,
// explicit host operation.
class ProfileEditorDraft : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileChanged FINAL)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged FINAL)
    Q_PROPERTY(QString sourceFilename READ sourceFilename NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString sourceRevision READ sourceRevision NOTIFY sourceChanged FINAL)
    Q_PROPERTY(bool paramsBased READ paramsBased NOTIFY profileChanged FINAL)
    Q_PROPERTY(double maxSettableFlow READ maxSettableFlow CONSTANT FINAL)
    Q_PROPERTY(double defaultPressureFlowLimit READ defaultPressureFlowLimit CONSTANT FINAL)

public:
    explicit ProfileEditorDraft(QObject* parent = nullptr);

    QVariantMap profile() const;
    bool modified() const { return m_modified; }
    QString sourceFilename() const { return m_sourceFilename; }
    QString sourceRevision() const { return m_sourceRevision; }
    bool paramsBased() const;
    double maxSettableFlow() const { return Profile::kMaxSettableFlow; }
    double defaultPressureFlowLimit() const { return Profile::kDefaultPressureFlowLimit; }

    Q_INVOKABLE bool load(const QVariantMap& profile, const QString& sourceFilename,
                          const QString& sourceRevision = {});
    Q_INVOKABLE bool createNewProfile(const QString& editorType, const QString& title);
    Q_INVOKABLE bool setProfile(const QVariantMap& profile);
    Q_INVOKABLE bool applyParams(const QVariantMap& params);
    Q_INVOKABLE bool setRecommendedDose(double doseG);
    Q_INVOKABLE bool addFrame(int afterIndex = -1);
    Q_INVOKABLE bool deleteFrame(int index);
    Q_INVOKABLE bool moveFrameUp(int index);
    Q_INVOKABLE bool moveFrameDown(int index);
    Q_INVOKABLE bool duplicateFrame(int index);
    Q_INVOKABLE bool setFrameProperty(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE QVariantMap getFrameAt(int index) const;
    Q_INVOKABLE int frameCount() const;
    Q_INVOKABLE QVariantMap getOrConvertProfileParams() const;
    Q_INVOKABLE void markClean();

signals:
    void profileChanged();
    void modifiedChanged();
    void sourceChanged();

private:
    void assign(Profile profile);

    Profile m_profile;
    bool m_hasProfile = false;
    bool m_modified = false;
    QString m_sourceFilename;
    QString m_sourceRevision;
};
