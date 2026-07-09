#pragma once

#include <QWidget>
#include <QSize>
#ifndef Q_OS_WIN
#  include <linux/videodev2.h>
#endif

class QLabel;
class QComboBox;
class QPushButton;
class QTimer;
class QImage;
class QBuffer;

class CameraView : public QWidget {
    Q_OBJECT
public:
    explicit CameraView(QWidget* parent = nullptr);
    ~CameraView();
    bool isRunning() const { return m_running; }

public slots:
    void startCamera();
    void stopCamera();

private slots:
    void refreshDevices();
    void pollFrame();

private:
    void setStatus(const QString& text);
    bool openDevice(const QString& path);
    void closeDevice();
    bool initCapture();
    bool readFrame(QImage& out);
    void releaseBuffers();

    QComboBox* m_deviceCombo = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_timer = nullptr;
    int m_fd = -1;
    bool m_running = false;
    QSize m_frameSize;
#ifndef Q_OS_WIN
    v4l2_pix_format m_pixfmt{};
    static constexpr int kBufferCount = 4;
    void* m_buffers[kBufferCount] = {nullptr, nullptr, nullptr, nullptr};
    size_t m_bufferLengths[kBufferCount] = {0, 0, 0, 0};
#endif
};
