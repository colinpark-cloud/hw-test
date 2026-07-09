#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QTimer>

class CameraView;
class CommTest;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    QSize minimumSizeHint() const override { return QSize(0, 0); }
    QSize sizeHint() const override { return QSize(1024, 600); }

private slots:
    void updateStatus();
    void pollIrda();

private:
    QWidget* makePanel(const QString& title, QLabel** valueOut);
    quint16 i2cGetWord(int bus, int addr, int reg, bool* ok);

    CameraView* m_camera   = nullptr;
    CommTest*   m_commTest = nullptr;
    QTimer*     m_timer    = nullptr;
    QTimer*     m_irdaTimer = nullptr;
    int         m_irdaFd   = -1;

    QLabel* m_proxVal      = nullptr;
    QLabel* m_proxRaw      = nullptr;
    QLabel* m_alsVal       = nullptr;
    QLabel* m_alsRaw       = nullptr;
    QLabel* m_irdaVal      = nullptr;
    QLabel* m_irdaRaw      = nullptr;
    QLabel* m_timeLabel    = nullptr;
    QStringList m_irdaBuf;
};
