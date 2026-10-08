#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// Applies a user's pick to a ComboBox the way Qt's own popup does, so a caller's
// `currentIndex: <binding>` survives it. A QML assignment `combo.currentIndex = i` removes
// that binding (qv4qobjectwrapper.cpp:724); a write through the C++ setter does not
// (QQuickComboBox::setCurrentIndex, qquickcombobox.cpp:1240), which is the path Qt's
// delegate click takes (itemClicked -> hidePopup(true), qquickcombobox.cpp:374-381, 425-433).
// Line numbers are qtdeclarative 6.12.0.
class ComboBoxSelection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ComboBoxSelection(QObject* parent = nullptr);

    Q_INVOKABLE void select(QObject* comboBox, int index);
};
