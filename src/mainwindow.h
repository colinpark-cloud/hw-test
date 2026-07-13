#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
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
    void testAudio();
    void testUsb(int idx);   // 0=USB1(/dev/sda) 1=USB2(/dev/sdb)
    void testSd();

private:
    QWidget* makePanel(const QString& title, QLabel** valueOut);
    quint16 i2cGetWord(int bus, int addr, int reg, bool* ok);
    bool runStorageRW(const QString& devNode, const QString& label);
    void setDevResult(QLabel* lbl, QPushButton* btn, bool ok);

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

    // Device test widgets
    QPushButton* m_audioBtn  = nullptr;
    QPushButton* m_usb1Btn   = nullptr;
    QPushButton* m_usb2Btn   = nullptr;
    QPushButton* m_sdBtn     = nullptr;
    QLabel*      m_audioRes  = nullptr;
    QLabel*      m_usb1Res   = nullptr;
    QLabel*      m_usb2Res   = nullptr;
    QLabel*      m_sdRes     = nullptr;
};
