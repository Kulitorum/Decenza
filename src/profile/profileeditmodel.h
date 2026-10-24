#pragma once

#include "profile.h"

#include <QStringList>
#include <QVariantMap>

// Service-free profile editing operations shared by the native controller and
// isolated editor drafts. These functions only change the supplied value object;
// they never touch the active machine profile, settings, storage, or BLE device.
class ProfileEditModel
{
public:
    static QVariantMap toVariantMap(const Profile& profile);
    static Profile fromVariantMap(const QVariantMap& value, bool* ok = nullptr);

    // Returns true when an editor's parameters could not safely regenerate its
    // frames because the source frame layout was not readable by that editor.
    static bool applyParams(Profile& profile, const QVariantMap& values,
                            QStringList* validationIssues = nullptr);
    static QVariantMap paramsForProfile(const Profile& profile);
    static Profile newEditorProfile(const QString& editorType, const QString& title);

    static bool addFrame(Profile& profile, int afterIndex = -1);
    static bool deleteFrame(Profile& profile, int index);
    static bool moveFrame(Profile& profile, int index, int delta);
    static bool duplicateFrame(Profile& profile, int index);
    static bool setFrameProperty(Profile& profile, int index, const QString& property,
                                 const QVariant& value);
    static QVariantMap frameAt(const Profile& profile, int index);
};
