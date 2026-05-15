#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

class ViewsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ViewsPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromViews();
    int selectedViewIndex() const;

    bool m_syncing = false;
    QListWidget *m_viewList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_updateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_addToAnimationButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
