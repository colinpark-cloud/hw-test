#include "mainwindow.h"
#include "cameraview.h"
#include "commtest.h"

#include <QApplication>
#include <QCryptographicHash>
#include <cmath>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QProcess>
#include <QPushButton>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#ifndef Q_OS_WIN
#  include <fcntl.h>
#  include <linux/i2c.h>
#  include <linux/i2c-dev.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

/* ── helpers ─────────────────────────────────────────────── */

#ifdef Q_OS_WIN
quint16 MainWindow::i2cGetWord(int, int, int, bool* ok) { *ok = false; return 0; }
static void initVCNL4200() {}
#else
quint16 MainWindow::i2cGetWord(int bus, int addr, int reg, bool* ok) {
    *ok = false;
    const QString dev = QString("/dev/i2c-%1").arg(bus);
    int fd = ::open(dev.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) return 0;
    if (::ioctl(fd, I2C_SLAVE, addr) < 0) { ::close(fd); return 0; }

    /* SMBus word read — same as i2cget -y N addr reg w */
    union i2c_smbus_data data{};
    i2c_smbus_ioctl_data args{};
    args.read_write = I2C_SMBUS_READ;
    args.command    = (uint8_t)reg;
    args.size       = I2C_SMBUS_WORD_DATA;
    args.data       = &data;
    if (::ioctl(fd, I2C_SMBUS, &args) < 0) { ::close(fd); return 0; }
    ::close(fd);
    *ok = true;
    return (quint16)(data.word & 0xFFFF);
}

static bool i2cSetWord(int bus, int addr, int reg, quint16 val) {
    const QString dev = QString("/dev/i2c-%1").arg(bus);
    int fd = ::open(dev.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) return false;
    if (::ioctl(fd, I2C_SLAVE, addr) < 0) { ::close(fd); return false; }
    uint8_t buf[3] = { (uint8_t)reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8) };
    bool ok = (::write(fd, buf, 3) == 3);
    ::close(fd);
    return ok;
}

static void initVCNL4200() {
    i2cSetWord(8, 0x51, 0x00, 0x0100);
    i2cSetWord(8, 0x51, 0x03, 0x000E);
}
#endif  /* !Q_OS_WIN */

/* ── panel builder ──────────────────────────────────────── */

QWidget* MainWindow::makePanel(const QString& title, QLabel** valueOut) {
    auto *w = new QWidget;
    w->setStyleSheet(
        "QWidget { background:#f0f4f8; border-radius:10px; }"
        "QLabel  { background:transparent; border:none; }");

    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(3);

    auto *titleLbl = new QLabel(title);
    titleLbl->setStyleSheet("font-size:11px; font-weight:700; color:#455a74;");

    auto *valLbl = new QLabel("...");
    valLbl->setStyleSheet("font-size:13px; font-weight:600; color:#17212f;");
    valLbl->setWordWrap(true);

    vl->addWidget(titleLbl);
    vl->addWidget(valLbl);

    if (valueOut) *valueOut = valLbl;
    return w;
}

/* ── constructor ────────────────────────────────────────── */

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("hw-test");
    setStyleSheet("QMainWindow { background:#ffffff; }");
    if (layout()) layout()->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumSize(0, 0);

    auto *central = new QWidget;
    central->setStyleSheet("background:#ffffff;");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    /* ── header ── */
    auto *header = new QWidget;
    header->setFixedHeight(36);
    header->setStyleSheet("background:#17304c; border-radius:8px;");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 0, 12, 0);

    auto *appTitle = new QLabel("hw-test  |  HW Dashboard");
    appTitle->setStyleSheet("color:white; font-size:14px; font-weight:700;");

    m_timeLabel = new QLabel;
    m_timeLabel->setStyleSheet("color:#a8c0d8; font-size:12px;");
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hl->addWidget(appTitle);
    hl->addStretch();
    hl->addWidget(m_timeLabel);

    /* ── content row ── */
    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(6);

    /* left column: camera (top) + sensor panels (bottom) */
    auto *leftCol = new QVBoxLayout;
    leftCol->setSpacing(6);

    auto *camFrame = new QWidget;
    camFrame->setStyleSheet("background:#f8fafc; border-radius:10px;");
    auto *camLayout = new QVBoxLayout(camFrame);
    camLayout->setContentsMargins(0, 0, 0, 0);
    m_camera = new CameraView(camFrame);
    camLayout->addWidget(m_camera);
    leftCol->addWidget(camFrame, 55);

    /* sensor panels — bigger, more visible */
    auto *sensorRow = new QHBoxLayout;
    sensorRow->setSpacing(6);

    auto makeSensor = [&](const QString& title, const QString& unit,
                          QLabel** bigVal, QLabel** rawVal) -> QWidget* {
        auto *w = new QWidget;
        w->setStyleSheet("QWidget{background:#f0f4f8;border-radius:10px;}"
                         "QLabel{background:transparent;border:none;}");
        w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *vl = new QVBoxLayout(w);
        vl->setContentsMargins(6, 6, 6, 6);
        vl->setSpacing(2);
        auto *lbl = new QLabel(title);
        lbl->setStyleSheet("font-size:11px;font-weight:700;color:#455a74;");
        lbl->setWordWrap(true);
        auto *big = new QLabel("---");
        big->setAlignment(Qt::AlignCenter);
        big->setWordWrap(true);
        big->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        big->setStyleSheet("font-size:26px;font-weight:900;color:#17212f;");
        auto *raw = new QLabel(unit);
        raw->setAlignment(Qt::AlignCenter);
        raw->setWordWrap(true);
        raw->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        raw->setStyleSheet("font-size:11px;color:#6b7a8d;");
        vl->addWidget(lbl);
        vl->addWidget(big, 1);
        vl->addWidget(raw);
        if (bigVal) *bigVal = big;
        if (rawVal) *rawVal = raw;
        return w;
    };

    sensorRow->addWidget(makeSensor("근접센서 (PS)", "VCNL4200 raw", &m_proxVal, &m_proxRaw), 1);
    sensorRow->addWidget(makeSensor("조도센서 (ALS)", "lux", &m_alsVal, &m_alsRaw), 1);
    sensorRow->addWidget(makeSensor("IrDA (/dev/ttyACM1)", "수신 데이터", &m_irdaVal, &m_irdaRaw), 1);
    leftCol->addLayout(sensorRow, 25);

    /* ── device test row: Audio / USB1 / USB2 / SD Card ── */
    const QString devBtnStyle =
        "QPushButton{font-size:11px;font-weight:700;background:#17304c;color:white;"
        "border-radius:6px;padding:4px 2px;}"
        "QPushButton:pressed{background:#0f2035;}";
    const QString devResStyle =
        "font-size:11px;font-weight:700;color:#6b7a8d;"
        "background:#f0f4f8;border:1px solid #d1d9e0;border-radius:5px;";

    auto makeDevCard = [&](const QString& label, QPushButton** btnOut, QLabel** resOut) -> QWidget* {
        auto *card = new QWidget;
        card->setStyleSheet("QWidget{background:#f0f4f8;border-radius:8px;}"
                            "QLabel{background:transparent;border:none;}");
        card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *vl = new QVBoxLayout(card);
        vl->setContentsMargins(4, 4, 4, 4);
        vl->setSpacing(3);
        auto *btn = new QPushButton(label);
        btn->setMinimumHeight(28);
        btn->setStyleSheet(devBtnStyle);
        auto *res = new QLabel("---");
        res->setAlignment(Qt::AlignCenter);
        res->setFixedHeight(20);
        res->setStyleSheet(devResStyle);
        vl->addWidget(btn);
        vl->addWidget(res);
        if (btnOut) *btnOut = btn;
        if (resOut) *resOut = res;
        return card;
    };

    auto *devRow = new QHBoxLayout;
    devRow->setSpacing(5);
    devRow->addWidget(makeDevCard("오디오",     &m_audioBtn, &m_audioRes), 1);
    devRow->addWidget(makeDevCard("USB1",        &m_usb1Btn,  &m_usb1Res),  1);
    devRow->addWidget(makeDevCard("USB2",        &m_usb2Btn,  &m_usb2Res),  1);
    devRow->addWidget(makeDevCard("SD카드",      &m_sdBtn,    &m_sdRes),    1);
    devRow->addWidget(makeDevCard("eMMC",        &m_emmcBtn,  &m_emmcRes),  1);
    devRow->addWidget(makeDevCard("EXP",         &m_expBtn,   &m_expRes),   1);
    leftCol->addLayout(devRow, 10);

    connect(m_audioBtn, &QPushButton::clicked, this, &MainWindow::testAudio);
    connect(m_usb1Btn,  &QPushButton::clicked, this, [this]{ testUsb(0); });
    connect(m_usb2Btn,  &QPushButton::clicked, this, [this]{ testUsb(1); });
    connect(m_sdBtn,    &QPushButton::clicked, this, &MainWindow::testSd);
    connect(m_emmcBtn,  &QPushButton::clicked, this, &MainWindow::testEmmc);
    connect(m_expBtn,   &QPushButton::clicked, this, &MainWindow::testExp);

    contentRow->addLayout(leftCol, 35);

    /* right column: CommTest widget */
    m_commTest = new CommTest;
    m_commTest->setStyleSheet("background:#f8fafc; border-radius:10px;");
    contentRow->addWidget(m_commTest, 65);

    root->addWidget(header);
    root->addLayout(contentRow, 1);

    /* ── timer ── */
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_timer->start(1000);

    QTimer::singleShot(500, m_camera, &CameraView::startCamera);
    m_commTest->setActive(true);

    /* IrDA serial: open /dev/ttyACM1 non-blocking (Linux only) */
#ifndef Q_OS_WIN
    m_irdaFd = ::open("/dev/ttyACM1", O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (m_irdaFd >= 0) {
        struct termios tio{};
        cfsetispeed(&tio, B115200);
        tio.c_cflag = CS8 | CLOCAL | CREAD;
        tio.c_iflag = IGNPAR;
        tcflush(m_irdaFd, TCIFLUSH);
        tcsetattr(m_irdaFd, TCSANOW, &tio);
    }
#endif
    m_irdaTimer = new QTimer(this);
    m_irdaTimer->setInterval(100);
    connect(m_irdaTimer, &QTimer::timeout, this, &MainWindow::pollIrda);
    m_irdaTimer->start();

    initVCNL4200();
#ifndef Q_OS_WIN
    { QFile f("/sys/class/backlight/backlight-lvds/brightness");
      if (f.open(QIODevice::WriteOnly)) f.write("255"); }
    { QFile f("/sys/class/backlight/backlight-lvds/power/control");
      if (f.open(QIODevice::WriteOnly)) f.write("on"); }
    QProcess::execute("sh", {"-c",
        "amixer -c 1 sset 'Speaker' 100% on 2>/dev/null;"
        "amixer -c 1 sset 'Headphone' 100% on 2>/dev/null"});
#endif
    /* 600ms delay — sensor needs time after power-on before first measurement */
    QTimer::singleShot(600, this, &MainWindow::updateStatus);
}

MainWindow::~MainWindow() {
#ifndef Q_OS_WIN
    if (m_irdaFd >= 0) { ::close(m_irdaFd); m_irdaFd = -1; }
#endif
}

/* ── IrDA poll ──────────────────────────────────────────── */

void MainWindow::pollIrda() {
    if (!m_irdaVal || !m_irdaRaw) return;
#ifdef Q_OS_WIN
    m_irdaVal->setText("N/A");
    m_irdaVal->setStyleSheet("font-size:20px;font-weight:900;color:#9ca3af;");
    m_irdaRaw->setText("Windows 미지원");
    return;
#else
    if (m_irdaFd < 0) {
        m_irdaFd = ::open("/dev/ttyACM1", O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (m_irdaFd >= 0) {
            struct termios tio{};
            cfsetispeed(&tio, B115200);
            tio.c_cflag = CS8 | CLOCAL | CREAD;
            tio.c_iflag = IGNPAR;
            tcflush(m_irdaFd, TCIFLUSH);
            tcsetattr(m_irdaFd, TCSANOW, &tio);
        } else {
            m_irdaVal->setText("X");
            m_irdaVal->setStyleSheet("font-size:20px;font-weight:900;color:#f87171;");
            m_irdaRaw->setText("장치 없음");
            m_irdaRaw->setStyleSheet("font-size:11px;color:#f87171;");
            return;
        }
    }
    char buf[256]{};
    ssize_t n = ::read(m_irdaFd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        QString data = QString::fromLatin1(buf, n).simplified();
        if (!data.isEmpty()) {
            m_irdaBuf.prepend(data.left(24));
            while (m_irdaBuf.size() > 3) m_irdaBuf.removeLast();
            m_irdaVal->setText(m_irdaBuf.join("\n"));
            m_irdaVal->setStyleSheet("font-size:13px;font-weight:700;color:#0ea5e9;"
                                     "font-family:monospace;");
            m_irdaRaw->setText(QString("%1 byte").arg(n));
            m_irdaRaw->setStyleSheet("font-size:11px;color:#6b7a8d;");
        }
    }
#endif  /* !Q_OS_WIN */
}

/* ── periodic update ────────────────────────────────────── */

void MainWindow::updateStatus() {
    m_timeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

    /* VCNL4200 sensor (i2c-8, addr 0x51) */
    bool okPs = false, okAls = false;
    quint16 ps  = i2cGetWord(8, 0x51, 0x08, &okPs);
    quint16 als = i2cGetWord(8, 0x51, 0x09, &okAls);

    if (okPs) {
        /* color: green < 50, yellow 50-200, red > 200 */
        const QString col = ps > 200 ? "#ef4444" : ps > 50 ? "#f59e0b" : "#22c55e";
        m_proxVal->setText(QString::number(ps));
        m_proxVal->setStyleSheet(QString("font-size:26px;font-weight:900;color:%1;").arg(col));
        m_proxRaw->setText(ps > 200 ? "가까움" : ps > 50 ? "보통" : "멀음");
        m_proxRaw->setStyleSheet(QString("font-size:11px;font-weight:700;color:%1;").arg(col));
    } else {
        m_proxVal->setText("X");
        m_proxVal->setStyleSheet("font-size:26px;font-weight:900;color:#f87171;");
        m_proxRaw->setText("센서 없음");
        m_proxRaw->setStyleSheet("font-size:11px;color:#f87171;");
    }

    if (okAls) {
        double lux = als * 0.0024;
        m_alsVal->setText(QString::number(lux, 'f', 1));
        m_alsVal->setStyleSheet("font-size:26px;font-weight:900;color:#0ea5e9;");
        m_alsRaw->setText(QString("lux  (raw: %1)").arg(als));
        m_alsRaw->setStyleSheet("font-size:11px;color:#6b7a8d;");
    } else {
        m_alsVal->setText("X");
        m_alsVal->setStyleSheet("font-size:26px;font-weight:900;color:#f87171;");
        m_alsRaw->setText("센서 없음");
        m_alsRaw->setStyleSheet("font-size:11px;color:#f87171;");
    }
}

/* ── device test helpers ─────────────────────────────────── */

void MainWindow::setDevResult(QLabel* lbl, QPushButton* btn, bool ok) {
    lbl->setText(ok ? "PASS" : "FAIL");
    lbl->setStyleSheet(ok
        ? "font-size:11px;font-weight:700;color:#0f3d27;background:#cdebd9;"
          "border:1px solid #8bc59e;border-radius:5px;"
        : "font-size:11px;font-weight:700;color:#842029;background:#f8d7da;"
          "border:1px solid #f1aeb5;border-radius:5px;");
    Q_UNUSED(btn)
}

bool MainWindow::runStorageRW(const QString& devNode, const QString& label) {
    // Find mount point for the device
    QString root;
    const auto mounts = QStorageInfo::mountedVolumes();
    for (const auto& m : mounts) {
        if (!m.isValid() || !m.isReady()) continue;
        const QString dev = QString::fromLocal8Bit(m.device());
        if (dev.contains(devNode, Qt::CaseInsensitive) ||
            m.rootPath().contains(devNode, Qt::CaseInsensitive)) {
            root = m.rootPath();
            break;
        }
    }
    if (root.isEmpty()) return false;

    const QString path = QDir(root).filePath(QString("hwtest_%1.bin").arg(label));
    QByteArray data(64 * 1024, '\xA5');
    { QFile f(path);
      if (!f.open(QIODevice::WriteOnly)) return false;
      f.write(data); }

    QByteArray readBack;
    { QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return false;
      readBack = f.readAll(); }
    QFile::remove(path);

    auto wHash = QCryptographicHash::hash(data,    QCryptographicHash::Sha256);
    auto rHash = QCryptographicHash::hash(readBack, QCryptographicHash::Sha256);
    return wHash == rHash;
}

void MainWindow::testAudio() {
    m_audioRes->setText("...");
    m_audioRes->setStyleSheet("font-size:11px;color:#6b7a8d;background:#f0f4f8;"
                              "border:1px solid #d1d9e0;border-radius:5px;");
#ifdef Q_OS_WIN
    setDevResult(m_audioRes, m_audioBtn, false);
#else
    // Play left.wav (Left channel only) then right.wav (Right channel only)
    static const QString kLeft  = "/usr/share/hw-test/left.wav";
    static const QString kRight = "/usr/share/hw-test/right.wav";
    if (!QFile::exists(kLeft) || !QFile::exists(kRight)) {
        setDevResult(m_audioRes, m_audioBtn, false);
        return;
    }
    auto *p = new QProcess(this);
    connect(p, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p, kRight](int code, QProcess::ExitStatus) {
        p->deleteLater();
        if (code != 0) { setDevResult(m_audioRes, m_audioBtn, false); return; }
        auto *p2 = new QProcess(this);
        connect(p2, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, p2](int c2, QProcess::ExitStatus) {
            setDevResult(m_audioRes, m_audioBtn, c2 == 0);
            p2->deleteLater();
        });
        p2->start("aplay", {"-D", "hw:1,0", kRight});
    });
    p->start("aplay", {"-D", "hw:1,0", kLeft});
#endif
}

void MainWindow::testUsb(int idx) {
    QLabel*      res = idx == 0 ? m_usb1Res : m_usb2Res;
    QPushButton* btn = idx == 0 ? m_usb1Btn : m_usb2Btn;
    res->setText("...");
    res->setStyleSheet("font-size:11px;color:#6b7a8d;background:#f0f4f8;"
                       "border:1px solid #d1d9e0;border-radius:5px;");
#ifdef Q_OS_WIN
    setDevResult(res, btn, false);
#else
    const QString usbPath = idx == 0 ? "1-1.3" : "1-1.4";
    // Find block device from USB port → check /proc/mounts → write/read test
    const QString cmd = QString(
        "P=$(readlink -f /sys/bus/usb/devices/%1 2>/dev/null);"
        "DEV=$(ls \"$P\"/*/host*/target*/*/block/ 2>/dev/null | head -n 1);"
        "[ -z \"$DEV\" ] && exit 2;"
        "MOUNT=$(awk -v d=\"/dev/$DEV\" '$1==d||$1==d\"1\"{print $2;exit}' /proc/mounts 2>/dev/null);"
        "[ -z \"$MOUNT\" ] && exit 3;"
        "DATA=\"HWTEST_$(date +%%s)\";"
        "echo \"$DATA\" > \"$MOUNT/hwtest_tmp.bin\" && sync;"
        "READ=$(cat \"$MOUNT/hwtest_tmp.bin\" 2>/dev/null);"
        "rm -f \"$MOUNT/hwtest_tmp.bin\";"
        "[ \"$DATA\" = \"$READ\" ] && exit 0 || exit 1"
    ).arg(usbPath);
    auto *p = new QProcess(this);
    connect(p, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, res, btn, p](int code, QProcess::ExitStatus) {
        p->deleteLater();
        if (code == 2) {
            res->setText("없음");
            res->setStyleSheet("font-size:11px;font-weight:700;color:#6b7280;"
                               "background:#f3f4f6;border:1px solid #d1d5db;border-radius:5px;");
        } else {
            setDevResult(res, btn, code == 0);
        }
    });
    p->start("sh", {"-c", cmd});
#endif
}

void MainWindow::testSd() {
    m_sdRes->setText("...");
    m_sdRes->setStyleSheet("font-size:11px;color:#6b7a8d;background:#f0f4f8;"
                           "border:1px solid #d1d9e0;border-radius:5px;");
#ifdef Q_OS_WIN
    setDevResult(m_sdRes, m_sdBtn, false);
#else
    auto *p = new QProcess(this);
    connect(p, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int, QProcess::ExitStatus) {
        setDevResult(m_sdRes, m_sdBtn, runStorageRW("mmcblk1", "sd"));
        p->deleteLater();
    });
    p->start("sh", {"-c", "ls /dev/mmcblk1 2>/dev/null"});
#endif
}

void MainWindow::testExp() {
    m_expRes->setText("...");
    m_expRes->setStyleSheet("font-size:11px;color:#6b7a8d;background:#f0f4f8;"
                            "border:1px solid #d1d9e0;border-radius:5px;");
#ifdef Q_OS_WIN
    setDevResult(m_expRes, m_expBtn, false);
#else
    // USB-to-eMMC adapter on port 1-1.1.3.1 — raw device, no filesystem; use direct dd RW test
    const QString cmd =
        "P=$(readlink -f /sys/bus/usb/devices/1-1.1.3.1 2>/dev/null);"
        "DEV=$(ls \"$P\"/*/host*/target*/*/block/ 2>/dev/null | head -n 1);"
        "[ -z \"$DEV\" ] && exit 2;"
        "SECTORS=$(cat /sys/block/$DEV/size 2>/dev/null);"
        "[ -z \"$SECTORS\" ] || [ \"$SECTORS\" -lt 16 ] && exit 1;"
        "OFFSET=$(( SECTORS - 8 ));"
        "dd if=/dev/urandom bs=4096 count=1 of=/tmp/hwtest_exp_ref.bin 2>/dev/null;"
        "dd if=/tmp/hwtest_exp_ref.bin bs=512 count=8 of=/dev/$DEV seek=$OFFSET 2>/dev/null && sync;"
        "dd if=/dev/$DEV bs=512 count=8 skip=$OFFSET of=/tmp/hwtest_exp_rd.bin 2>/dev/null;"
        "cmp /tmp/hwtest_exp_ref.bin /tmp/hwtest_exp_rd.bin; RET=$?;"
        "rm -f /tmp/hwtest_exp_ref.bin /tmp/hwtest_exp_rd.bin;"
        "exit $RET";
    auto *p = new QProcess(this);
    connect(p, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int code, QProcess::ExitStatus) {
        p->deleteLater();
        if (code == 2) {
            m_expRes->setText("없음");
            m_expRes->setStyleSheet("font-size:11px;font-weight:700;color:#6b7280;"
                                    "background:#f3f4f6;border:1px solid #d1d5db;border-radius:5px;");
        } else {
            setDevResult(m_expRes, m_expBtn, code == 0);
        }
    });
    p->start("sh", {"-c", cmd});
#endif
}

void MainWindow::testEmmc() {
    m_emmcRes->setText("...");
    m_emmcRes->setStyleSheet("font-size:11px;color:#6b7a8d;background:#f0f4f8;"
                             "border:1px solid #d1d9e0;border-radius:5px;");
#ifdef Q_OS_WIN
    setDevResult(m_emmcRes, m_emmcBtn, false);
#else
    // Write/read test directly to /mnt/data (eMMC user data partition)
    const QString path = "/mnt/data/hwtest_emmc.bin";
    QByteArray data(64 * 1024, '\x5A');
    bool ok = false;
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) { f.write(data); ok = true; }
    }
    if (ok) {
        QByteArray readBack;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) readBack = f.readAll();
        QFile::remove(path);
        auto wh = QCryptographicHash::hash(data,    QCryptographicHash::Sha256);
        auto rh = QCryptographicHash::hash(readBack, QCryptographicHash::Sha256);
        ok = (wh == rh);
    }
    setDevResult(m_emmcRes, m_emmcBtn, ok);
#endif
}
