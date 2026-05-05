#pragma once

#include <QPointF>
#include <QWidget>

class QTimer;

class DX12View : public QWidget
{
    Q_OBJECT
public:
    explicit DX12View(QWidget *parent = nullptr);
    ~DX12View();

protected:
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void showPendingCloneOptions();

    QPointF m_lastGlobalMousePos;
    bool m_hasLastMousePos = false;
    bool m_cloneOptionsDialogOpen = false;
    QTimer *m_cloneOptionsTimer = nullptr;
};
