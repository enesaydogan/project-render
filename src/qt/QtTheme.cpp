#include "QtTheme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

void ApplyQtTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(37, 44, 55));
    palette.setColor(QPalette::WindowText, QColor(220, 228, 239));
    palette.setColor(QPalette::Base, QColor(29, 35, 44));
    palette.setColor(QPalette::AlternateBase, QColor(42, 49, 61));
    palette.setColor(QPalette::ToolTipBase, QColor(49, 58, 72));
    palette.setColor(QPalette::ToolTipText, QColor(232, 238, 247));
    palette.setColor(QPalette::Text, QColor(220, 228, 239));
    palette.setColor(QPalette::Button, QColor(48, 56, 69));
    palette.setColor(QPalette::ButtonText, QColor(220, 228, 239));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(68, 121, 193));
    palette.setColor(QPalette::HighlightedText, QColor(244, 248, 255));
    palette.setColor(QPalette::Light, QColor(76, 86, 102));
    palette.setColor(QPalette::Midlight, QColor(58, 68, 82));
    palette.setColor(QPalette::Mid, QColor(72, 82, 95));
    palette.setColor(QPalette::Dark, QColor(22, 27, 34));
    palette.setColor(QPalette::Shadow, QColor(13, 17, 22));
    palette.setColor(QPalette::Link, QColor(104, 168, 255));
    palette.setColor(QPalette::LinkVisited, QColor(141, 166, 219));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(124, 135, 152));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(124, 135, 152));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(124, 135, 152));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(33, 39, 48));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor(41, 47, 58));

    app.setPalette(palette);

    QFont font(QStringLiteral("Segoe UI"), 10);
    app.setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #252c37;
            color: #dce4ef;
        }

        QDockWidget {
            border: 1px solid #384454;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
        }

        QDockWidget::title {
            background: #2d3744;
            color: #eef4fb;
            text-align: left;
            padding: 8px 10px;
            border-bottom: 1px solid #415064;
            font-weight: 600;
        }

        QGroupBox {
            background: #2b3340;
            border: 1px solid #3d4959;
            border-radius: 8px;
            margin-top: 10px;
            padding: 12px 10px 10px 10px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: #8ebaf5;
            background: #2b3340;
            font-weight: 600;
        }

        QLabel {
            background: transparent;
        }

        QLineEdit, QListWidget, QComboBox, QDoubleSpinBox, QSpinBox, QTextEdit {
            background: #1d232c;
            border: 1px solid #465467;
            border-radius: 6px;
            padding: 5px 7px;
            selection-background-color: #4479c1;
            selection-color: #f4f8ff;
        }

        QLineEdit:focus, QListWidget:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus, QTextEdit:focus {
            border: 1px solid #68a8ff;
        }

        QListWidget {
            outline: none;
        }

        QListWidget::item {
            padding: 5px 6px;
            border-radius: 4px;
        }

        QListWidget::item:selected {
            background: #4479c1;
            color: #f4f8ff;
        }

        QPushButton {
            background: #354255;
            color: #ebf1f9;
            border: 1px solid #4c617b;
            border-radius: 6px;
            padding: 6px 10px;
        }

        QPushButton:hover {
            background: #405069;
            border-color: #6a8ab3;
        }

        QPushButton:pressed {
            background: #2d3848;
        }

        QPushButton:disabled {
            background: #303845;
            color: #7c8798;
            border-color: #3d4958;
        }

        QCheckBox, QRadioButton {
            spacing: 8px;
        }

        QCheckBox::indicator, QRadioButton::indicator {
            width: 16px;
            height: 16px;
        }

        QCheckBox::indicator {
            border: 1px solid #5d7088;
            border-radius: 4px;
            background: #1f2630;
        }

        QCheckBox::indicator:checked {
            background: #4479c1;
            border-color: #68a8ff;
        }

        QRadioButton::indicator {
            border: 1px solid #5d7088;
            border-radius: 8px;
            background: #1f2630;
        }

        QRadioButton::indicator:checked {
            background: #4479c1;
            border-color: #68a8ff;
        }

        QTabWidget::pane {
            border: 1px solid #3d4959;
            background: #2b3340;
            top: -1px;
        }

        QTabBar::tab {
            background: #313b49;
            border: 1px solid #435164;
            border-bottom: none;
            padding: 7px 12px;
            margin-right: 3px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
        }

        QTabBar::tab:selected {
            background: #2b3340;
            color: #9dc5ff;
        }

        QTabBar::tab:!selected {
            margin-top: 2px;
        }

        QProgressBar {
            background: #1f2630;
            border: 1px solid #435164;
            border-radius: 6px;
            text-align: center;
            color: #e6edf7;
        }

        QProgressBar::chunk {
            background: #4a83d1;
            border-radius: 5px;
        }

        QScrollArea {
            border: none;
            background: transparent;
        }

        QMenuBar {
            background: #2a3340;
            color: #e8eef8;
            border-bottom: 1px solid #415064;
        }

        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
        }

        QMenuBar::item:selected, QMenu::item:selected {
            background: #4479c1;
            color: #f4f8ff;
        }

        QMenu {
            background: #252d39;
            border: 1px solid #415064;
        }

        QStatusBar {
            background: #2a3340;
            color: #d6dfec;
            border-top: 1px solid #415064;
        }

        QToolTip {
            background: #364152;
            color: #eef3fb;
            border: 1px solid #5879a3;
            padding: 4px 6px;
        }

        QSlider::groove:horizontal {
            height: 6px;
            background: #1c222b;
            border-radius: 3px;
        }

        QSlider::handle:horizontal {
            width: 16px;
            margin: -6px 0;
            border-radius: 8px;
            background: #5f93dc;
            border: 1px solid #7db0ff;
        }

        QSlider::sub-page:horizontal {
            background: #3f70b5;
            border-radius: 3px;
        }
    )"));
}