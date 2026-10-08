#include "comboboxselection.h"

ComboBoxSelection::ComboBoxSelection(QObject* parent)
    : QObject(parent)
{
}

void ComboBoxSelection::select(QObject* comboBox, int index)
{
    if (comboBox)
        comboBox->setProperty("currentIndex", index);
}
