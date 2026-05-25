#pragma once

#include <QPointF>
#include <QWidget>

class QTimer;
class QDragEnterEvent;
class QDropEvent;

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
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void showPendingCloneOptions();
    void cancelBoxSelection();
    void updateBoxSelectionBand(const QPointF &currentGlobalPos);

    QPointF m_lastGlobalMousePos;
    QPointF m_boxStartGlobalPos;
    QPoint m_boxStartLocalPos;
    bool m_hasLastMousePos = false;
    bool m_boxSelecting = false;
    bool m_cloneOptionsDialogOpen = false;
    QWidget *m_boxSelectionBand = nullptr;
    QTimer *m_cloneOptionsTimer = nullptr;
};
