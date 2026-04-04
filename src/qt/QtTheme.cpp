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
    palette.setColor(QPalette::Window, QColor(30, 30, 30));
    palette.setColor(QPalette::WindowText, QColor(204, 204, 204));
    palette.setColor(QPalette::Base, QColor(37, 37, 38));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    palette.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
    palette.setColor(QPalette::ToolTipText, QColor(204, 204, 204));
    palette.setColor(QPalette::Text, QColor(204, 204, 204));
    palette.setColor(QPalette::Button, QColor(51, 51, 51));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(0, 122, 204));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Light, QColor(60, 60, 60));
    palette.setColor(QPalette::Midlight, QColor(55, 55, 55));
    palette.setColor(QPalette::Mid, QColor(45, 45, 45));
    palette.setColor(QPalette::Dark, QColor(25, 25, 25));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0));
    palette.setColor(QPalette::Link, QColor(64, 166, 255));
    palette.setColor(QPalette::LinkVisited, QColor(104, 140, 204));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor(37, 37, 38));

    app.setPalette(palette);

    QFont font(QStringLiteral("Segoe UI"), 10);
    app.setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget, QDialog {
            background-color: #1e1e1e;
            color: #cccccc;
        }

        QScrollArea, QScrollArea > QWidget > QWidget {
            background-color: transparent;
            border: none;
        }

        QDockWidget {
            border: 1px solid #3c3c3c;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
        }

        QDockWidget::title {
            background-color: #252526;
            color: #ffffff;
            text-align: left;
            padding: 4px 8px;
            font-weight: 600;
            border-bottom: 1px solid #3c3c3c;
        }

        QGroupBox {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            margin-top: 16px;
            padding: 8px 4px 4px 4px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            top: 0px;
            padding: 0 4px;
            color: #007acc;
            background-color: transparent;
            font-weight: bold;
        }

        QLabel {
            background: transparent;
        }

        QLineEdit, QListWidget, QComboBox, QDoubleSpinBox, QSpinBox, QTextEdit, QPlainTextEdit {
            background-color: #2d2d30;
            color: #cccccc;
            border: 1px solid #3c3c3c;
            border-radius: 3px;
            padding: 2px 4px;
            selection-background-color: #007acc;
            selection-color: #ffffff;
        }

        QLineEdit:focus, QListWidget:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
            border: 1px solid #007acc;
            background-color: #333337;
        }

        QListWidget {
            outline: none;
        }

        QListWidget::item {
            padding: 2px 4px;
            border-radius: 2px;
        }

        QListWidget::item:selected {
            background-color: #04395e;
            color: #ffffff;
            border: 1px solid #007acc;
        }
        
        QListWidget::item:hover:!selected {
            background-color: #2a2d2e;
        }

        QPushButton {
            background-color: #333333;
            color: #ffffff;
            border: 1px solid #3c3c3c;
            border-radius: 3px;
            padding: 4px 10px;
            outline: none;
        }

        QPushButton:hover {
            background-color: #404040;
            border: 1px solid #4a4a4a;
        }

        QPushButton:pressed {
            background-color: #007acc;
            border: 1px solid #007acc;
            color: #ffffff;
        }

        QPushButton:disabled {
            background-color: #252526;
            color: #666666;
            border: 1px solid #2d2d2d;
        }

        QCheckBox, QRadioButton {
            spacing: 8px;
            outline: none;
            background: transparent;
        }

        QCheckBox::indicator, QRadioButton::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #3c3c3c;
            border-radius: 3px;
            background-color: #2d2d30;
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
            height: 4px;
            background: #2d2d30;
            border-radius: 2px;
        }

        QSlider::handle:horizontal {
            width: 12px;
            height: 12px;
            margin: -4px 0;
            border-radius: 6px;
            background: #007acc;
            border: 1px solid #1e1e1e;
        }

        QSlider::handle:horizontal:hover {
            background: #0098ff;
            border: 1px solid #ffffff;
        }

        QSlider::handle:horizontal:pressed {
            background: #005c99;
        }

        QSlider::sub-page:horizontal {
            background: #007acc;
            border-radius: 2px;
        }
    )"));
}