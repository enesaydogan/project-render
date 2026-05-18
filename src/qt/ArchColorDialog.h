#pragma once

#include <QColor>
#include <QString>

class QWidget;

class ArchColorDialog
{
public:
    static QColor getColor(const QColor &initial,
                           QWidget *parent,
                           const QString &title);

    static QColor colorForKelvin(double kelvin);
};
