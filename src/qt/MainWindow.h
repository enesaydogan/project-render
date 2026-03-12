#pragma once

#include <QMainWindow>

class DX12View;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // access to the central DX12 rendering widget
    DX12View *view() const { return m_view; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createToolBar();
    void createDocks();

    DX12View *m_view;
};
