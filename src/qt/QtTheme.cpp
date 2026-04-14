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
            padding: 8px 10px;
            font-weight: 600;
            border-bottom: 1px solid #3c3c3c;
        }

        QGroupBox {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            margin-top: 24px;
            padding: 12px 10px 10px 10px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            top: 6px;
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
            border-radius: 4px;
            padding: 4px 6px;
            selection-background-color: #007acc;
            selection-color: #ffffff;
            min-height: 20px;
        }

        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 20px;
            border-left: 1px solid #3c3c3c;
        }

        QComboBox::down-arrow {
            width: 0; 
            height: 0; 
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid #cccccc;
            margin-top: 2px;
        }

        QComboBox QAbstractItemView {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            selection-background-color: #04395e;
            selection-color: #ffffff;
            outline: 0;
        }

        QLineEdit:focus, QListWidget:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
            border: 1px solid #007acc;
            background-color: #333337;
        }

        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::up-button, QSpinBox::down-button {
            width: 16px;
            border-left: 1px solid #3c3c3c;
            background: #2d2d30;
        }

        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover, QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background: #3e3e42;
        }

        QListWidget {
            outline: none;
        }

        QListWidget::item {
            padding: 4px 6px;
            border-radius: 2px;
        }

        QListWidget::item:selected {
            background-color: #04395e;
            color: #ffffff;
            border: 1px solid #007acc;
        }
        
        QListWidget::item:hover:!selected {
            background-color: #3e3e42;
        }

        QPushButton {
            background-color: #333333;
            color: #ffffff;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            padding: 6px 12px;
            outline: none;
            min-height: 20px;
        }

        QPushButton:hover {
            background-color: #3e3e42;
            border: 1px solid #4a4a4a;
        }

        QPushButton:pressed, QPushButton:checked {
            background-color: #04395e;
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

        QCheckBox:disabled, QRadioButton:disabled {
            color: #666666;
        }

        QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {
            background-color: #0f2537;
            border: 1px solid #173f61;
        }

        QCheckBox::indicator:hover:unchecked, QRadioButton::indicator:hover:unchecked {
            border: 1px solid #1392ff;
            background-color: #ffffff;
        }

        QCheckBox::indicator:checked, QRadioButton::indicator:checked {
            background-color: #007acc;
            border: 1px solid #007acc;
            image: url(data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="%23ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>);
        }

        QCheckBox::indicator:hover:checked, QRadioButton::indicator:hover:checked {
            background-color: #1392ff;
            border: 1px solid #1392ff;
        }

        QCheckBox::indicator:disabled:unchecked, QRadioButton::indicator:disabled:unchecked {
            background-color: #252526;
            border: 1px solid #3c3c3c;
        }

        QCheckBox::indicator:disabled:checked, QRadioButton::indicator:disabled:checked {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            image: url(data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="%23555555" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>);
        }

        QRadioButton::indicator {
            border-radius: 8px;
        }

        QTabWidget::pane {
            border: 1px solid #3c3c3c;
            background: #1e1e1e;
            top: -1px;
            border-radius: 4px;
        }

        QTabBar::tab {
            background: #252526;
            color: #cccccc;
            border: 1px solid #3c3c3c;
            border-bottom: none;
            padding: 8px 16px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }

        QTabBar::tab:selected {
            background: #1e1e1e;
            color: #ffffff;
            border-bottom: 2px solid #007acc;
        }

        QTabBar::tab:hover:!selected {
            background: #2d2d30;
        }

        QTabBar::tab:!selected {
            margin-top: 2px;
        }

        QProgressBar {
            background: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            text-align: center;
            color: #ffffff;
        }

        QProgressBar::chunk {
            background: #007acc;
            border-radius: 3px;
        }

        QMenuBar {
            background: #252526;
            color: #cccccc;
            border-bottom: 1px solid #3c3c3c;
        }

        QMenuBar::item {
            background: transparent;
            padding: 6px 12px;
        }

        QMenuBar::item:selected, QMenu::item:selected {
            background: #04395e;
            color: #ffffff;
        }

        QMenu {
            background: #252526;
            border: 1px solid #3c3c3c;
            padding: 4px 0px;
        }

        QStatusBar {
            background: #007acc;
            color: #ffffff;
        }

        QToolTip {
            background: #252526;
            color: #cccccc;
            border: 1px solid #3c3c3c;
            padding: 4px 8px;
            border-radius: 4px;
        }

        QSlider::groove:horizontal {
            height: 4px;
            background: #3c3c3c;
            border-radius: 2px;
        }

        QSlider::handle:horizontal {
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
            background: #007acc;
        }

        QSlider::handle:horizontal:hover {
            background: #3399ff;
        }

        QSlider::handle:horizontal:pressed {
            background: #005c99;
        }

        QSlider::sub-page:horizontal {
            background: #007acc;
            border-radius: 2px;
        }

        QScrollBar:vertical {
            border: none;
            background: #1e1e1e;
            width: 12px;
            margin: 0px 0px 0px 0px;
        }

        QScrollBar::handle:vertical {
            background: #424242;
            min-height: 20px;
            border-radius: 3px;
            margin: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background: #4f4f4f;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            border: none;
            background: none;
            height: 0px;
        }

        QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical, QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }

        QScrollBar:horizontal {
            border: none;
            background: #1e1e1e;
            height: 12px;
            margin: 0px 0px 0px 0px;
        }

        QScrollBar::handle:horizontal {
            background: #424242;
            min-width: 20px;
            border-radius: 3px;
            margin: 2px;
        }

        QScrollBar::handle:horizontal:hover {
            background: #4f4f4f;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            border: none;
            background: none;
            width: 0px;
        }

        QScrollBar::up-arrow:horizontal, QScrollBar::down-arrow:horizontal, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: none;
        }
    )"));
}
