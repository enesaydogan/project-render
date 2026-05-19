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
    palette.setColor(QPalette::Window, QColor(33, 33, 33));
    palette.setColor(QPalette::WindowText, QColor(224, 224, 224));
    palette.setColor(QPalette::Base, QColor(36, 36, 36));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    palette.setColor(QPalette::ToolTipBase, QColor(47, 47, 47));
    palette.setColor(QPalette::ToolTipText, QColor(235, 235, 235));
    palette.setColor(QPalette::Text, QColor(226, 226, 226));
    palette.setColor(QPalette::Button, QColor(48, 48, 48));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(58, 177, 220));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Light, QColor(74, 74, 74));
    palette.setColor(QPalette::Midlight, QColor(62, 62, 62));
    palette.setColor(QPalette::Mid, QColor(46, 46, 46));
    palette.setColor(QPalette::Dark, QColor(29, 29, 29));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0));
    palette.setColor(QPalette::Link, QColor(91, 205, 245));
    palette.setColor(QPalette::LinkVisited, QColor(125, 180, 205));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(34, 34, 34));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor(40, 40, 40));

    app.setPalette(palette);

    QFont font(QStringLiteral("Segoe UI"), 9);
    app.setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget, QDialog {
            background-color: #212121;
            color: #e2e2e2;
        }

        QScrollArea, QScrollArea > QWidget > QWidget {
            background-color: transparent;
            border: none;
        }

        QAbstractScrollArea {
            border: none;
            background-color: transparent;
        }

        QMainWindow::separator {
            background-color: #2f2f2f;
            width: 1px;
            height: 1px;
        }

        QMainWindow::separator:hover {
            background-color: #58d0f4;
        }

        QDockWidget {
            border: none;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
        }

        QDockWidget::title {
            background-color: #262626;
            color: #ffffff;
            text-align: left;
            padding: 7px 10px;
            font-weight: 600;
            border: none;
        }

        QGroupBox {
            background-color: transparent;
            border: none;
            border-radius: 0px;
            margin-top: 16px;
            padding: 8px 0px 4px 0px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 0px;
            top: 4px;
            padding: 0px;
            color: #58d0f4;
            background-color: transparent;
            font-weight: bold;
        }

        QLabel {
            background: transparent;
        }

        QLineEdit, QListWidget, QComboBox, QDoubleSpinBox, QSpinBox, QTextEdit, QPlainTextEdit {
            background-color: #303030;
            color: #f0f0f0;
            border: 1px solid transparent;
            border-radius: 2px;
            padding: 3px 6px;
            selection-background-color: #1d6680;
            selection-color: #ffffff;
            min-height: 19px;
        }

        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 20px;
            border-left: none;
        }

        QComboBox::down-arrow {
            width: 0; 
            height: 0; 
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid #d8d8d8;
            margin-top: 2px;
        }

        QComboBox QAbstractItemView {
            background-color: #2b2b2b;
            border: 1px solid #383838;
            selection-background-color: #20495a;
            selection-color: #ffffff;
            outline: 0;
        }

        QLineEdit:focus, QListWidget:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
            border: 1px solid #58d0f4;
            background-color: #343434;
        }

        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::up-button, QSpinBox::down-button {
            width: 16px;
            border-left: none;
            background: #303030;
        }

        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover, QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background: #3d3d3d;
        }

        QListWidget {
            outline: none;
            border: none;
        }

        QListWidget::item {
            padding: 4px 6px;
            border-radius: 3px;
        }

        QListWidget::item:selected {
            background-color: #263f4b;
            color: #ffffff;
            border: none;
        }
        
        QListWidget::item:hover:!selected {
            background-color: #3a3a3a;
        }

        QPushButton {
            background: #4a4a4a;
            color: #ffffff;
            border: 1px solid #5c5c5c;
            border-radius: 2px;
            padding: 5px 11px;
            outline: none;
            min-height: 22px;
            font-weight: 500;
        }

        QPushButton:hover {
            background: #565656;
            border: 1px solid #6a6a6a;
        }

        QPushButton:pressed, QPushButton:checked {
            background: #244858;
            border: 1px solid #58d0f4;
            color: #ffffff;
        }

        QPushButton:focus {
            border: 1px solid #74d9f6;
        }

        QPushButton:disabled {
            background: #2e2e2e;
            color: #7a7a7a;
            border: 1px solid #343434;
        }

        QCheckBox, QRadioButton {
            spacing: 8px;
            outline: none;
            background: transparent;
        }

        QCheckBox:disabled, QRadioButton:disabled {
            color: #777777;
        }

        QCheckBox::indicator, QRadioButton::indicator {
            width: 14px;
            height: 14px;
            border-radius: 3px;
        }

        QRadioButton::indicator {
            border-radius: 8px;
        }

        QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {
            background-color: #303030;
            border: 1px solid #555555;
        }

        QCheckBox::indicator:hover:unchecked, QRadioButton::indicator:hover:unchecked {
            border: 1px solid #58d0f4;
            background-color: #393939;
        }

        QCheckBox::indicator:checked, QRadioButton::indicator:checked {
            background-color: #2698bd;
            border: 1px solid #58d0f4;
        }

        QCheckBox::indicator:checked {
            image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="%23ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>');
        }

        QRadioButton::indicator:checked {
            image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="5" fill="%23ffffff"/></svg>');
        }

        QCheckBox::indicator:hover:checked, QRadioButton::indicator:hover:checked {
            background-color: #32b8e4;
            border: 1px solid #78defb;
        }

        QCheckBox::indicator:disabled:unchecked, QRadioButton::indicator:disabled:unchecked {
            background-color: #282828;
            border: 1px solid #444444;
        }

        QCheckBox::indicator:disabled:checked {
            background-color: #2b2b2b;
            border: 1px solid #444444;
            image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="%23666666" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>');
        }

        QRadioButton::indicator:disabled:checked {
            background-color: #2b2b2b;
            border: 1px solid #444444;
            image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="5" fill="%23666666"/></svg>');
        }

        QTabWidget::pane {
            border: none;
            background: #212121;
            top: 0px;
            border-radius: 0px;
        }

        QTabBar {
            background: #242424;
        }

        QTabBar::tab {
            background: #242424;
            color: #c6c6c6;
            border: 1px solid transparent;
            border-bottom: 1px solid transparent;
            padding: 5px 11px;
            margin: 0px;
            border-radius: 0px;
        }

        QTabBar::tab:selected {
            background: #303030;
            color: #64d6f8;
            border-bottom: 2px solid #64d6f8;
            font-weight: 600;
        }

        QTabBar::tab:bottom:selected {
            border-top: 2px solid #64d6f8;
            border-bottom: 1px solid transparent;
        }

        QTabBar::tab:top:selected {
            border-bottom: 2px solid #64d6f8;
        }

        QTabBar::tab:hover:!selected {
            background: #2d2d2d;
            color: #eeeeee;
        }

        QTabBar::tab:!selected {
            margin-top: 0px;
        }

        QProgressBar {
            background: #2b2b2b;
            border: none;
            border-radius: 2px;
            text-align: center;
            color: #ffffff;
        }

        QProgressBar::chunk {
            background: #58d0f4;
            border-radius: 2px;
        }

        QMenuBar {
            background: #1e1e1e;
            color: #eeeeee;
            border-bottom: none;
        }

        QMenuBar::item {
            background: transparent;
            padding: 5px 11px;
        }

        QMenuBar::item:selected, QMenu::item:selected {
            background: #253f4b;
            color: #ffffff;
        }

        QMenu {
            background: #292929;
            border: 1px solid #383838;
            padding: 4px 0px;
        }

        QMenu::item {
            padding: 5px 26px 5px 22px;
        }

        QToolBar {
            background: #282828;
            border: none;
            spacing: 2px;
            padding: 3px 6px;
        }

        QToolBar::separator {
            background: #505050;
            width: 1px;
            margin: 4px 8px;
        }

        QToolButton {
            background: #3e3e3e;
            color: #d9d9d9;
            border: 1px solid #4a4a4a;
            border-radius: 2px;
            padding: 4px;
            min-width: 24px;
            min-height: 24px;
        }

        QToolButton:hover {
            background: #4b4b4b;
            border: 1px solid #5f5f5f;
        }

        QToolButton:pressed, QToolButton:checked {
            background: #20495a;
            border: 1px solid #58d0f4;
            color: #ffffff;
        }

        QToolButton:disabled {
            color: #7a7a7a;
            background: #2b2b2b;
            border-color: #313131;
        }

        QComboBox#ToolbarCombo {
            min-height: 24px;
            padding: 2px 24px 2px 8px;
            background: #303030;
            border: 1px solid transparent;
        }

        QStatusBar {
            background: #1e1e1e;
            color: #dedede;
            border-top: none;
        }

        QToolTip {
            background: #2c2c2c;
            color: #e6e6e6;
            border: 1px solid #4a4a4a;
            padding: 4px 8px;
            border-radius: 3px;
        }

        QSlider::groove:horizontal {
            height: 4px;
            background: #424242;
            border-radius: 2px;
        }

        QSlider::handle:horizontal {
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
            background: #9b9b9b;
        }

        QSlider::handle:horizontal:hover {
            background: #d2d2d2;
        }

        QSlider::handle:horizontal:pressed {
            background: #58d0f4;
        }

        QSlider::sub-page:horizontal {
            background: #3095b8;
            border-radius: 2px;
        }

        QScrollBar:vertical {
            border: none;
            background: #222222;
            width: 10px;
            margin: 0px 0px 0px 0px;
        }

        QScrollBar::handle:vertical {
            background: #555555;
            min-height: 20px;
            border-radius: 3px;
            margin: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background: #666666;
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
            background: #222222;
            height: 10px;
            margin: 0px 0px 0px 0px;
        }

        QScrollBar::handle:horizontal {
            background: #555555;
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
