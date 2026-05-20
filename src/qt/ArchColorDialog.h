#pragma once

#include <QColor>
#include <QString>
#include <functional>

class QWidget;

class ArchColorDialog
{
public:
    static QColor getColor(const QColor &initial,
                           QWidget *parent,
                           const QString &title,
                           const std::function<void(const QColor &)> &previewChanged = {});
    static void showColor(const QColor &initial,
                          QWidget *parent,
                          const QString &title,
                          const std::function<void(const QColor &)> &previewChanged,
                          const std::function<void(const QColor &)> &accepted,
                          const std::function<void()> &rejected);

    static QColor colorForKelvin(double kelvin);
};
