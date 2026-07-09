#include "cameraview.h"

#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#ifndef Q_OS_WIN
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
/* ── Windows stub: no V4L2 camera support ─────────────────── */
CameraView::CameraView(QWidget* parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *lbl = new QLabel("카메라 미지원 (Windows)");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color:#9ca3af; font-size:16px; font-weight:700;");
    layout->addWidget(lbl, 1);
    m_statusLabel = new QLabel("N/A");
    layout->addWidget(m_statusLabel);
}
CameraView::~CameraView() {}
void CameraView::startCamera() {}
void CameraView::stopCamera() {}
void CameraView::refreshDevices() {}
void CameraView::pollFrame() {}
void CameraView::setStatus(const QString&) {}
bool CameraView::openDevice(const QString&) { return false; }
void CameraView::closeDevice() {}
bool CameraView::initCapture() { return false; }
bool CameraView::readFrame(QImage&) { return false; }
void CameraView::releaseBuffers() {}

#else  /* Linux */

static QString panelStyle() {
    return "QWidget{background:#f7f9fc; border:1px solid #d8e0ea; border-radius:14px;}";
}

static bool xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ::ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r == 0;
}

CameraView::CameraView(QWidget* parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *top = new QWidget;
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(8);

    m_deviceCombo = new QComboBox;
    m_refreshBtn = new QPushButton("Refresh");
    m_startBtn = new QPushButton("Start");
    m_stopBtn = new QPushButton("Stop");
    m_statusLabel = new QLabel("No camera selected");
    m_statusLabel->setStyleSheet("color:#5f6b7a; font-size:14px;");

    for (auto *b : {m_refreshBtn, m_startBtn, m_stopBtn}) {
        b->setMinimumHeight(40);
        b->setStyleSheet("font-size:15px; font-weight:700; background:#e2e8f0; color:#17212f; border:1px solid #b8c7d9; border-radius:10px; padding:8px 12px;");
    }

    topLayout->addWidget(m_deviceCombo, 1);
    topLayout->addWidget(m_refreshBtn);
    topLayout->addWidget(m_startBtn);
    topLayout->addWidget(m_stopBtn);

    m_imageLabel = new QLabel;
    m_imageLabel->setMinimumHeight(100);
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("QLabel{background:#f0f4f8; border-radius:8px; color:#5f6b7a;}");
    m_imageLabel->setText("Camera preview will appear here");

    layout->addWidget(top);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_imageLabel, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &CameraView::pollFrame);
    connect(m_refreshBtn, &QPushButton::clicked, this, &CameraView::refreshDevices);
    connect(m_startBtn, &QPushButton::clicked, this, &CameraView::startCamera);
    connect(m_stopBtn, &QPushButton::clicked, this, &CameraView::stopCamera);

    refreshDevices();
}

CameraView::~CameraView() {
    stopCamera();
}

void CameraView::setStatus(const QString& text) {
    if (m_statusLabel) m_statusLabel->setText(text);
}

void CameraView::refreshDevices() {
    if (!m_deviceCombo) return;
    const QString current = m_deviceCombo->currentText();
    m_deviceCombo->clear();
    const QDir dir("/dev");
    const QStringList devs = dir.entryList(QStringList() << "video*", QDir::System | QDir::Readable, QDir::Name);
    for (const auto& d : devs) m_deviceCombo->addItem("/dev/" + d);
    if (m_deviceCombo->count() == 0) {
        m_deviceCombo->addItem("/dev/video0");
        setStatus("No /dev/video* device found");
    } else {
        const int idx = m_deviceCombo->findText(current);
        if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
        setStatus(QString("Found %1 device(s)").arg(m_deviceCombo->count()));
    }
}

bool CameraView::openDevice(const QString& path) {
    closeDevice();
    m_fd = ::open(path.toStdString().c_str(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        setStatus(QString("Failed to open %1").arg(path));
        return false;
    }

    v4l2_capability cap{};
    if (!xioctl(m_fd, VIDIOC_QUERYCAP, &cap)) {
        setStatus(QString("VIDIOC_QUERYCAP failed on %1").arg(path));
        closeDevice();
        return false;
    }
    setStatus(QString("Opened %1 (%2)").arg(path, reinterpret_cast<const char*>(cap.card)));
    return true;
}

bool CameraView::initCapture() {
    if (m_fd < 0) return false;

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(m_fd, VIDIOC_G_FMT, &fmt)) return false;

    m_frameSize = QSize(int(fmt.fmt.pix.width), int(fmt.fmt.pix.height));
    m_pixfmt = fmt.fmt.pix;

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(m_fd, VIDIOC_G_PARM, &parm);

    v4l2_requestbuffers req{};
    req.count = kBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (!xioctl(m_fd, VIDIOC_REQBUFS, &req)) {
        setStatus("VIDIOC_REQBUFS failed");
        return false;
    }
    if (req.count < 2) {
        setStatus("Not enough camera buffers");
        return false;
    }

    for (uint32_t i = 0; i < req.count && i < kBufferCount; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (!xioctl(m_fd, VIDIOC_QUERYBUF, &buf)) {
            setStatus("VIDIOC_QUERYBUF failed");
            return false;
        }
        m_bufferLengths[i] = buf.length;
        m_buffers[i] = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (m_buffers[i] == MAP_FAILED) {
            m_buffers[i] = nullptr;
            setStatus("mmap failed");
            return false;
        }
        if (!xioctl(m_fd, VIDIOC_QBUF, &buf)) {
            setStatus("VIDIOC_QBUF failed");
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(m_fd, VIDIOC_STREAMON, &type)) {
        setStatus("VIDIOC_STREAMON failed");
        return false;
    }
    return true;
}

void CameraView::releaseBuffers() {
    if (m_fd >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(m_fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < kBufferCount; ++i) {
        if (m_buffers[i]) {
            ::munmap(m_buffers[i], m_bufferLengths[i]);
            m_buffers[i] = nullptr;
            m_bufferLengths[i] = 0;
        }
    }
}

void CameraView::closeDevice() {
    releaseBuffers();
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool CameraView::readFrame(QImage& out) {
    if (m_fd < 0) {
        setStatus("Camera fd not open");
        return false;
    }
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (!xioctl(m_fd, VIDIOC_DQBUF, &buf)) {
        setStatus("VIDIOC_DQBUF failed");
        return false;
    }
    if (buf.index >= kBufferCount || !m_buffers[buf.index]) return false;

    const uchar* src = reinterpret_cast<const uchar*>(m_buffers[buf.index]);
    const int w = int(m_pixfmt.width);
    const int h = int(m_pixfmt.height);

    if (m_pixfmt.pixelformat == V4L2_PIX_FMT_MJPEG) {
        QByteArray data(reinterpret_cast<const char*>(src), int(buf.bytesused));
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        out = reader.read();
    } else if (m_pixfmt.pixelformat == V4L2_PIX_FMT_YUYV || m_pixfmt.pixelformat == V4L2_PIX_FMT_UYVY) {
        QImage img(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            uchar* dst = img.scanLine(y);
            const uchar* line = src + y * m_pixfmt.bytesperline;
            for (int x = 0; x < w; x += 2) {
                int y0, u, y1, v;
                if (m_pixfmt.pixelformat == V4L2_PIX_FMT_YUYV) {
                    y0 = line[0]; u = line[1] - 128; y1 = line[2]; v = line[3] - 128;
                } else {
                    u = line[0] - 128; y0 = line[1]; v = line[2] - 128; y1 = line[3];
                }
                auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
                auto convert = [&](int yy, int uu, int vv, uchar* outp) {
                    int r = int(yy + 1.402 * vv);
                    int g = int(yy - 0.344136 * uu - 0.714136 * vv);
                    int b = int(yy + 1.772 * uu);
                    outp[0] = clamp(r);
                    outp[1] = clamp(g);
                    outp[2] = clamp(b);
                };
                convert(y0, u, v, dst);
                if (x + 1 < w) convert(y1, u, v, dst + 3);
                line += 4;
                dst += 6;
            }
        }
        out = img;
    } else {
        setStatus(QString("Unsupported pixel format: 0x%1").arg(m_pixfmt.pixelformat, 0, 16));
    }

    if (buf.index < kBufferCount) {
        xioctl(m_fd, VIDIOC_QBUF, &buf);
    }
    return !out.isNull();
}

void CameraView::startCamera() {
    if (m_running) return;
    if (m_deviceCombo && m_deviceCombo->currentText().isEmpty() && m_deviceCombo->count() > 0) {
        m_deviceCombo->setCurrentIndex(0);
    }
    if (!m_deviceCombo || m_deviceCombo->currentText().isEmpty()) {
        setStatus("No device selected");
        return;
    }
    if (!openDevice(m_deviceCombo->currentText())) return;
    if (!initCapture()) {
        closeDevice();
        return;
    }
    m_running = true;
    m_timer->start();
    setStatus(QString("Streaming %1").arg(m_deviceCombo->currentText()));
}

void CameraView::stopCamera() {
    if (!m_running) {
        closeDevice();
        return;
    }
    m_timer->stop();
    m_running = false;
    closeDevice();
    setStatus("Stopped");
}

void CameraView::pollFrame() {
    if (m_fd < 0) return;
    QImage frame;
    if (!readFrame(frame)) return;
    setStatus(QString("Streaming %1x%2").arg(frame.width()).arg(frame.height()));
    if (frame.isNull()) return;
    frame = frame.mirrored(true, false);
    const QSize target = m_imageLabel->size();
    QPixmap px = QPixmap::fromImage(frame).scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(px);
}

#endif  /* !Q_OS_WIN */
