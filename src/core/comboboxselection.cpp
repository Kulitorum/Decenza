#include "comboboxselection.h"

#include <QVariant>

ComboBoxSelection::ComboBoxSelection(QObject* parent)
    : QObject(parent)
{
}

void ComboBoxSelection::select(QObject* comboBox, int index)
{
    if (comboBox)
        comboBox->setProperty("currentIndex", index);
}
