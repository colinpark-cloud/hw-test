#include "commtest.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <cstdio>
#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <windows.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/wait.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <linux/serial.h>
#  include <unistd.h>
#endif
#include <cstring>

/* ── style helpers ─────────────────────────────────────── */
static QString panelStyle() {
    return "QWidget{background:#f7f9fc; border:1px solid #d8e0ea; border-radius:14px;}";
}
static QString titleStyle() {
    return "QLabel{font-size:18px; font-weight:800; color:#17212f;}";
}
static QString detailStyle() {
    return "QLabel{font-size:14px; color:#5f6b7a;}";
}
static QString runStyle(bool running) {
    return running
        ? "QPushButton{font-size:16px;font-weight:800;background:#22c55e;color:white;"
          "border:1px solid #16a34a;border-radius:14px;padding:12px 14px;}"
          "QPushButton:pressed{padding-top:14px;padding-bottom:10px;}"
        : "QPushButton{font-size:16px;font-weight:800;background:#e8f1fb;color:#0f1724;"
          "border:1px solid #b8c7d9;border-radius:14px;padding:12px 14px;}"
          "QPushButton:pressed{padding-top:14px;padding-bottom:10px;}";
}
static QString badgeStyle(const QString& bg, const QString& fg, const QString& border) {
    return QString("QLabel{font-size:22px;font-weight:900;background:%1;color:%2;"
                   "border:1px solid %3;border-radius:10px;min-height:48px;}").arg(bg, fg, border);
}
static QString typeButtonStyle(bool checked) {
    return checked
        ? "QPushButton{font-size:12px;font-weight:700;background:#cdebd9;color:#0f3d27;"
          "border:1px solid #8bc59e;border-radius:9px;padding:3px 8px;}"
        : "QPushButton{font-size:12px;font-weight:700;background:#f1f5f9;color:#17212f;"
          "border:1px solid #c9d3df;border-radius:9px;padding:3px 8px;}";
}
static QString flowButtonStyle(bool checked) {
    return checked
        ? "QPushButton{font-size:11px;font-weight:700;background:#ede9fe;color:#3b0764;"
          "border:1px solid #a78bfa;border-radius:9px;padding:2px 6px;}"
        : "QPushButton{font-size:11px;font-weight:700;background:#f1f5f9;color:#17212f;"
          "border:1px solid #c9d3df;border-radius:9px;padding:2px 6px;}";
}
static QString txrxButtonStyle(bool checked, bool isTx) {
    /* TX = blue tint, RX = orange tint when selected */
    if (checked && isTx)
        return "QPushButton{font-size:12px;font-weight:700;background:#dbeafe;color:#1e3a8a;"
               "border:1px solid #93c5fd;border-radius:9px;padding:3px 8px;}";
    if (checked && !isTx)
        return "QPushButton{font-size:12px;font-weight:700;background:#ffedd5;color:#7c2d12;"
               "border:1px solid #fdba74;border-radius:9px;padding:3px 8px;}";
    return "QPushButton{font-size:12px;font-weight:700;background:#f1f5f9;color:#17212f;"
           "border:1px solid #c9d3df;border-radius:9px;padding:3px 8px;}";
}
static QString ipButtonStyle(bool checked) {
    return checked
        ? "QPushButton{font-size:12px;font-weight:700;background:#cdebd9;color:#0f3d27;"
          "border:1px solid #8bc59e;border-radius:9px;padding:2px 4px;}"
        : "QPushButton{font-size:12px;font-weight:700;background:#f1f5f9;color:#17212f;"
          "border:1px solid #c9d3df;border-radius:9px;padding:2px 4px;}";
}

/* ── network helpers ───────────────────────────────────── */
static bool applyIpAddress(const QString& iface, const QString& ip) {
    const QString cidr = ip + "/24";
    // nmcli: update persistent NM config and apply immediately
    if (QProcess::execute("nmcli", {"connection", "modify", iface,
            "ipv4.addresses", cidr, "ipv4.method", "manual"}) == 0) {
        QProcess::execute("nmcli", {"connection", "up", iface});
        return true;
    }
    // fallback: ip addr replace
    QProcess::execute("ip", {"addr", "flush", "dev", iface});
    return QProcess::execute("ip", {"addr", "add", cidr, "dev", iface}) == 0;
}

/* ── constructor ────────────────────────────────────────── */
CommTest::CommTest(QWidget* parent) : QWidget(parent) {
    buildUi();
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &CommTest::onTick);
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &CommTest::updateClock);
    m_clockTimer->start();
    setRunButton();
}

/* ── buildUi ────────────────────────────────────────────── */
void CommTest::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 4);
    layout->setSpacing(1);

    /* clock row: label + NTP button */
    auto *clockRow = new QWidget;
    auto *clockRowLayout = new QHBoxLayout(clockRow);
    clockRowLayout->setContentsMargins(0, 0, 0, 0);
    clockRowLayout->setSpacing(6);

    m_clockLabel = new QLabel;
    m_clockLabel->setFixedHeight(32);
    m_clockLabel->setStyleSheet("QLabel{font-size:14px;font-weight:800;color:#1f2937;"
        "background:#ffffff;border:1px solid #cdd6e1;border-radius:10px;padding:1px 10px;}"
        "QLabel:hover{background:#f0f4ff;border-color:#93c5fd;}");
    m_clockLabel->setCursor(Qt::PointingHandCursor);
    m_clockLabel->installEventFilter(this);
    clockRowLayout->addWidget(m_clockLabel, 1);

    const QString syncBtnStyle =
        "QPushButton{font-size:11px;font-weight:700;background:#dbeafe;"
        "color:#1e40af;border:1px solid #93c5fd;border-radius:8px;padding:0 10px;}"
        "QPushButton:pressed{background:#bfdbfe;}";

    auto *ntpBtn = new QPushButton("NTP 동기화");
    ntpBtn->setFixedHeight(32);
    ntpBtn->setStyleSheet(syncBtnStyle);
    connect(ntpBtn, &QPushButton::clicked, this, &CommTest::syncNtp);
    clockRowLayout->addWidget(ntpBtn);

    auto *sshBtn = new QPushButton("서버 동기화");
    sshBtn->setFixedHeight(32);
    sshBtn->setStyleSheet(syncBtnStyle);
    connect(sshBtn, &QPushButton::clicked, this, &CommTest::syncTimeFromServer);
    clockRowLayout->addWidget(sshBtn);

    layout->addWidget(clockRow);
    updateClock();

    /* master TX/RX control for all COM ports */
    auto *masterRow = new QWidget;
    masterRow->setFixedHeight(32);
    masterRow->setStyleSheet("QWidget{background:#eef3f8;border:1px solid #c9d3df;border-radius:8px;}"
                             "QLabel{background:transparent;border:none;}");
    auto *masterLayout = new QHBoxLayout(masterRow);
    masterLayout->setContentsMargins(8, 2, 8, 2);
    masterLayout->setSpacing(6);
    auto *masterLbl = new QLabel("전체 COM");
    masterLbl->setStyleSheet("font-size:12px;font-weight:800;color:#17212f;");
    m_masterTxBtn = new QPushButton("TX 송신");
    m_masterRxBtn = new QPushButton("RX 수신");
    for (auto *b : {m_masterTxBtn, m_masterRxBtn}) {
        b->setFixedHeight(24);
        b->setFixedWidth(76);
    }
    const bool defTx = false; // always RX by default
    m_masterTxBtn->setStyleSheet(txrxButtonStyle(defTx,  true));
    m_masterRxBtn->setStyleSheet(txrxButtonStyle(!defTx, false));
    connect(m_masterTxBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 3; ++i) {
            m_rows[i].txrxMode = 0;
            for (int k = 0; k < 2; ++k)
                if (m_rows[i].txrxButtons[k])
                    m_rows[i].txrxButtons[k]->setStyleSheet(txrxButtonStyle(k == 0, k == 0));
        }
        m_masterTxBtn->setStyleSheet(txrxButtonStyle(true,  true));
        m_masterRxBtn->setStyleSheet(txrxButtonStyle(false, false));
    });
    connect(m_masterRxBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 3; ++i) {
            m_rows[i].txrxMode = 1;
            for (int k = 0; k < 2; ++k)
                if (m_rows[i].txrxButtons[k])
                    m_rows[i].txrxButtons[k]->setStyleSheet(txrxButtonStyle(k == 1, k == 0));
        }
        m_masterTxBtn->setStyleSheet(txrxButtonStyle(false, true));
        m_masterRxBtn->setStyleSheet(txrxButtonStyle(true,  false));
    });
    masterLayout->addWidget(masterLbl);
    masterLayout->addWidget(m_masterTxBtn);
    masterLayout->addWidget(m_masterRxBtn);
    masterLayout->addStretch();
    {
        const char* labels[3] = {"Board모드", "PC (Linux)", "PC (Windows)"};
        for (int i = 0; i < 3; ++i) {
            m_modeBtns[i] = new QPushButton(labels[i]);
            m_modeBtns[i]->setFixedHeight(24);
            m_modeBtns[i]->setFixedWidth(90);
            connect(m_modeBtns[i], &QPushButton::clicked, this, [this, i]() {
                switchBoardMode(i);
            });
            masterLayout->addWidget(m_modeBtns[i]);
        }
        /* apply initial highlight */
        for (int i = 0; i < 3; ++i) {
            bool sel = (i == m_boardMode);
            m_modeBtns[i]->setStyleSheet(sel
                ? "QPushButton{font-size:11px;font-weight:700;background:#dbeafe;color:#1e3a8a;"
                  "border:1px solid #93c5fd;border-radius:8px;padding:2px 6px;}"
                : "QPushButton{font-size:11px;font-weight:700;background:#f1f5f9;color:#374151;"
                  "border:1px solid #c9d3df;border-radius:8px;padding:2px 6px;}");
        }
    }
    layout->addWidget(masterRow);

    /* grid header */
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(0, 4);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);

    for (auto [text, col, color] : {
            std::tuple<const char*,int,const char*>{" ", 0, "#5f6b7a"},
            {"Count", 1, "#5f6b7a"}, {"PASS", 2, "#16a34a"}, {"FAIL", 3, "#dc2626"}}) {
        auto *h = new QLabel(text);
        h->setAlignment(Qt::AlignCenter);
        h->setStyleSheet(QString("font-size:19px;font-weight:900;color:%1;").arg(color));
        grid->addWidget(h, 0, col);
    }

    /* row data — default mode: 0=iMX8MP, 1=Linux PC, 2=Windows */
#ifdef Q_OS_WIN
    m_boardMode = 2;
#else
    m_boardMode = QCoreApplication::arguments().contains("--pc") ? 1 : 0;
#endif
    const bool pcMode = (m_boardMode != 0);
    const int defaultTxRx = 1; // always RX
    {
        TargetRow r; r.kind=TargetKind::Com1;
#ifdef Q_OS_WIN
        r.title = "COM3";
        const QString d = "COM3";
#else
        r.title = "COM1";
        const QString d = pcMode ? "/dev/ttyUSB0" : "/dev/ttymxc3";
#endif
        r.detail=d; r.device=d; r.txrxMode=defaultTxRx; m_rows.append(r);
    }
    {
        TargetRow r; r.kind=TargetKind::Com2; r.title="COM2";
#ifdef Q_OS_WIN
        const QString d = "COM2";
#else
        const QString d = pcMode ? "/dev/ttyUSB1" : "/dev/ttymxc2";
#endif
        r.detail=d; r.device=d; r.txrxMode=defaultTxRx; m_rows.append(r);
    }
    {
        TargetRow r; r.kind=TargetKind::Com3;
#ifdef Q_OS_WIN
        r.title = "COM1";
        const QString d = "COM1";
        r.detail = "RS485  COM1";
#else
        r.title = "COM3";
        const QString d = pcMode ? "/dev/ttyUSB2" : "/dev/ttyACM0";
        r.detail = (pcMode ? "RS485  /dev/ttyUSB2" : "RS485  /dev/ttyACM0");
#endif
        r.device=d; r.comType=1; r.txrxMode=defaultTxRx; m_rows.append(r);
    }
    {
        TargetRow r; r.kind=TargetKind::Lan1; r.title="LAN1";
        const QString lan1Ip = defTx ? "192.168.1.101" : "192.168.1.100";
        r.detail=lan1Ip; r.ip=lan1Ip; m_rows.append(r);
    }
    {
        TargetRow r; r.kind=TargetKind::Lan2; r.title="LAN2";
        const QString lan2Ip = defTx ? "192.168.2.101" : "192.168.2.100";
        r.detail=lan2Ip; r.ip=lan2Ip; m_rows.append(r);
    }

    /* build each row widget */
    for (int i = 0; i < m_rows.size(); ++i) {
        auto &row = m_rows[i];

        auto *rowWidget = new QWidget;
        rowWidget->setStyleSheet(panelStyle());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(10, 4, 10, 4);
        rowLayout->setSpacing(10);

        auto *left = new QWidget;
        auto *leftLayout = new QVBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(2);

        auto *titleRow = new QWidget;
        auto *titleRowLayout = new QHBoxLayout(titleRow);
        titleRowLayout->setContentsMargins(0, 0, 0, 0);
        titleRowLayout->setSpacing(8);
        row.titleLabel = new QLabel(row.title);
        row.titleLabel->setStyleSheet(titleStyle());
        titleRowLayout->addWidget(row.titleLabel);
        if (row.kind == TargetKind::Lan1 || row.kind == TargetKind::Lan2) {
            const QString initIp = (row.kind == TargetKind::Lan1)
                ? (defTx ? "192.168.1.101" : "192.168.1.100")
                : (defTx ? "192.168.2.101" : "192.168.2.100");
            row.currentIpLabel = new QLabel(initIp);
            row.currentIpLabel->setStyleSheet(
                "QLabel{font-size:14px;font-weight:700;color:#17212f;}");
            titleRowLayout->addWidget(row.currentIpLabel);
        }
        titleRowLayout->addStretch();
        leftLayout->addWidget(titleRow);

        /* editable device path + baud rate for all COM rows */
        if (row.kind == TargetKind::Com1 || row.kind == TargetKind::Com2
                || row.kind == TargetKind::Com3) {
            auto *devRow = new QWidget;
            auto *devLayout = new QHBoxLayout(devRow);
            devLayout->setContentsMargins(0, 0, 0, 0);
            devLayout->setSpacing(4);

            row.deviceEdit = new QLineEdit(row.device);
            row.deviceEdit->setFixedHeight(24);
            row.deviceEdit->setStyleSheet(
                "QLineEdit{font-size:11px;color:#374151;background:#f9fafb;"
                "border:1px solid #d1d5db;border-radius:6px;padding:1px 6px;}");
            connect(row.deviceEdit, &QLineEdit::textChanged, this,
                [this, i](const QString& text) {
                    if (i < m_rows.size()) m_rows[i].device = text;
                });

#ifdef Q_OS_WIN
            // Windows: replace plain edit with combo listing COM1-COM5
            row.deviceEdit->hide();
            row.deviceCombo = new QComboBox;
            row.deviceCombo->setFixedHeight(24);
            row.deviceCombo->setStyleSheet(
                "QComboBox{font-size:11px;color:#374151;background:#f9fafb;"
                "border:1px solid #d1d5db;border-radius:6px;padding:1px 6px;}"
                "QComboBox::drop-down{border:none;width:18px;}"
                "QComboBox QAbstractItemView{font-size:11px;}");
            for (int p = 1; p <= 5; ++p)
                row.deviceCombo->addItem(QString("COM%1").arg(p));
            { int idx = row.deviceCombo->findText(row.device);
              row.deviceCombo->setCurrentIndex(idx >= 0 ? idx : 0); }
            connect(row.deviceCombo, &QComboBox::currentTextChanged, this,
                [this, i](const QString& text) {
                    if (i < m_rows.size()) {
                        m_rows[i].device = text;
                        if (m_rows[i].deviceEdit) m_rows[i].deviceEdit->setText(text);
                    }
                });
            devLayout->addWidget(row.deviceCombo, 1);
#else
            devLayout->addWidget(row.deviceEdit, 1);
#endif
            leftLayout->addWidget(devRow);
        }

        if (row.kind == TargetKind::Com1 || row.kind == TargetKind::Com2) {
            /* row1: RS232 / RS422/485 type buttons */
            auto *typeRow = new QWidget;
            auto *typeLayout = new QHBoxLayout(typeRow);
            typeLayout->setContentsMargins(0, 0, 0, 0);
            typeLayout->setSpacing(6);
            const QStringList types = {"RS232", "RS422/485"};
            for (int j = 0; j < 2; ++j) {
                row.typeButtons[j] = new QPushButton(types[j]);
                row.typeButtons[j]->setCheckable(true);
                row.typeButtons[j]->setFixedWidth(88);
                row.typeButtons[j]->setMinimumHeight(28);
                row.typeButtons[j]->setStyleSheet(typeButtonStyle(j == 0));
                typeLayout->addWidget(row.typeButtons[j]);
                connect(row.typeButtons[j], &QPushButton::clicked, this,
                    [this, i, j]() {
                        auto &r = m_rows[i];
                        r.comType = j;
                        for (int k = 0; k < 2; ++k)
                            if (r.typeButtons[k])
                                r.typeButtons[k]->setStyleSheet(typeButtonStyle(k == j));
                    });
            }
            row.typeButtons[0]->setChecked(true);
            typeLayout->addStretch();
            leftLayout->addWidget(typeRow);

            /* COM2 only: flow control — 없음 / 흐름제어(RTS/CTS), on same row as type buttons */
            if (row.kind == TargetKind::Com2) {
                auto *sepLbl = new QLabel("|");
                sepLbl->setStyleSheet("color:#c9d3df; font-size:16px;");
                typeLayout->addWidget(sepLbl);
                const QStringList flowLabels = {"없음", "흐름제어"};
                for (int j = 0; j < 2; ++j) {
                    row.flowButtons[j] = new QPushButton(flowLabels[j]);
                    row.flowButtons[j]->setCheckable(true);
                    row.flowButtons[j]->setMinimumHeight(28);
                    row.flowButtons[j]->setStyleSheet(flowButtonStyle(j == 0));
                    typeLayout->addWidget(row.flowButtons[j]);
                    connect(row.flowButtons[j], &QPushButton::clicked, this,
                        [this, i, j]() {
                            auto &r = m_rows[i];
                            r.flowCtrl = j;
                            for (int k = 0; k < 2; ++k)
                                if (r.flowButtons[k])
                                    r.flowButtons[k]->setStyleSheet(flowButtonStyle(k == j));
                        });
                }
                row.flowButtons[0]->setChecked(true);
            }

        } else if (row.kind == TargetKind::Com3) {
            /* COM3: fixed RS485 label */
            row.detailLabel = new QLabel(row.detail);
            row.detailLabel->setStyleSheet(detailStyle());
            leftLayout->addWidget(row.detailLabel);
        }

        /* TX / RX mode buttons — all COM rows */
        if (row.kind == TargetKind::Com1 || row.kind == TargetKind::Com2
                || row.kind == TargetKind::Com3) {
            auto *txrxRow = new QWidget;
            auto *txrxLayout = new QHBoxLayout(txrxRow);
            txrxLayout->setContentsMargins(0, 0, 0, 0);
            txrxLayout->setSpacing(6);
            const QStringList labels = {"TX 송신", "RX 수신"};
            for (int j = 0; j < 2; ++j) {
                row.txrxButtons[j] = new QPushButton(labels[j]);
                row.txrxButtons[j]->setCheckable(true);
                row.txrxButtons[j]->setFixedWidth(82);
                row.txrxButtons[j]->setMinimumHeight(28);
                const bool sel = defTx ? (j == 0) : (j == 1);
                row.txrxButtons[j]->setStyleSheet(txrxButtonStyle(sel, j == 0));
                txrxLayout->addWidget(row.txrxButtons[j]);
                connect(row.txrxButtons[j], &QPushButton::clicked, this,
                    [this, i, j]() {
                        auto &r = m_rows[i];
                        r.txrxMode = j;
                        for (int k = 0; k < 2; ++k)
                            if (r.txrxButtons[k])
                                r.txrxButtons[k]->setStyleSheet(txrxButtonStyle(k == j, k == 0));
                    });
            }
            row.txrxButtons[defTx ? 0 : 1]->setChecked(true);
            txrxLayout->addStretch();
            leftLayout->addWidget(txrxRow);

        } else {  /* LAN1 / LAN2 */
            /* LAN1 / LAN2: IP selector buttons */
            auto *btnRow = new QWidget;
            auto *btnLayout = new QHBoxLayout(btnRow);
            btnLayout->setContentsMargins(0, 0, 0, 0);
            btnLayout->setSpacing(6);
            const QString iface = (row.kind == TargetKind::Lan1) ? "eth0" : "eth1";
            const QStringList baseIps = (row.kind == TargetKind::Lan1)
                ? QStringList({"192.168.1.100","192.168.1.101","192.168.1.102","192.168.1.103"})
                : QStringList({"192.168.2.100","192.168.2.101","192.168.2.102","192.168.2.103"});
            const int defIpIdx = defTx ? 1 : 0;
            row.ip = baseIps[defIpIdx];
            for (int j = 0; j < 4; ++j) {
                /* show only last octet to save space: "192.168.1.100" → ".100" */
                const QString shortLabel = "." + baseIps[j].section('.', -1);
                row.ipButtons[j] = new QPushButton(shortLabel);
                row.ipButtons[j]->setToolTip(baseIps[j]);
                row.ipButtons[j]->setCheckable(true);
                row.ipButtons[j]->setFixedWidth(52);
                row.ipButtons[j]->setMinimumHeight(30);
                row.ipButtons[j]->setStyleSheet(ipButtonStyle(j == defIpIdx));
                btnLayout->addWidget(row.ipButtons[j]);
                connect(row.ipButtons[j], &QPushButton::clicked, this,
                    [this, i, baseIps, iface, j]() {
                        auto &r = m_rows[i];
                        for (int k = 0; k < 4; ++k)
                            if (r.ipButtons[k])
                                r.ipButtons[k]->setStyleSheet(ipButtonStyle(k == j));
                        r.ip = baseIps[j];
                        if (r.currentIpLabel)
                            r.currentIpLabel->setText(baseIps[j]);
                        applyIpAddress(iface, baseIps[j]);
                    });
            }
            row.ipButtons[defIpIdx]->setChecked(true);
            leftLayout->addWidget(btnRow);
        }

        row.totalLabel = new QLabel("0");
        row.passLabel  = new QLabel("0");
        row.failLabel  = new QLabel("0");
        for (auto *lbl : {row.totalLabel, row.passLabel, row.failLabel}) {
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setMinimumWidth(84);
        }
        row.totalLabel->setStyleSheet(badgeStyle("#eef3f8","#17212f","#c9d3df"));
        row.passLabel ->setStyleSheet(badgeStyle("#dff3e6","#16a34a","#8bc59e"));
        row.failLabel ->setStyleSheet(badgeStyle("#fae0e2","#dc2626","#f1aeb5"));

        rowLayout->addWidget(left, 4);
        rowLayout->addWidget(row.totalLabel, 1);
        rowLayout->addWidget(row.passLabel, 1);
        rowLayout->addWidget(row.failLabel, 1);
        grid->addWidget(rowWidget, i + 1, 0, 1, 4);
    }

    /* run button + brightness in bottom row */
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 2, 0, 0);
    bottom->setSpacing(10);
    m_runBtn = new QPushButton("off");
    m_runBtn->setFixedHeight(56);
    m_runBtn->setMinimumWidth(140);
    m_runBtn->setStyleSheet(runStyle(false));

#ifndef Q_OS_WIN
    /* brightness: ☀ 밝기 [====slider====] 255 — centered in the empty space */
    auto *brightWidget = new QWidget;
    brightWidget->setStyleSheet("QWidget{background:#f1f5f9;border:1px solid #c9d3df;border-radius:10px;}"
                                "QLabel{background:transparent;border:none;}");
    auto *bl = new QHBoxLayout(brightWidget);
    bl->setContentsMargins(12, 6, 12, 6);
    bl->setSpacing(8);

    auto *bIcon = new QLabel("☀");
    bIcon->setStyleSheet("font-size:18px;color:#f59e0b;");
    bl->addWidget(bIcon);

    auto *bLbl = new QLabel("밝기");
    bLbl->setStyleSheet("font-size:13px;font-weight:700;color:#374151;");
    bl->addWidget(bLbl);

    auto *bSlider = new QSlider(Qt::Horizontal);
    bSlider->setRange(20, 255);
    { QFile f("/sys/class/backlight/backlight-lvds/brightness");
      if (f.open(QIODevice::WriteOnly)) f.write("255"); }
    { QFile f("/sys/class/backlight/backlight-lvds/power/control");
      if (f.open(QIODevice::WriteOnly)) f.write("on"); }
    bSlider->setValue(255);
    bSlider->setStyleSheet(
        "QSlider::groove:horizontal{height:8px;background:#d1d5db;border-radius:4px;}"
        "QSlider::handle:horizontal{width:28px;height:28px;margin:-10px 0;background:#3b82f6;"
        "border-radius:14px;border:2px solid #fff;}"
        "QSlider::sub-page:horizontal{background:#3b82f6;border-radius:4px;}");
    bl->addWidget(bSlider, 1);

    auto *bVal = new QLabel("255");
    bVal->setFixedWidth(30);
    bVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bVal->setStyleSheet("font-size:13px;font-weight:700;color:#374151;");
    bl->addWidget(bVal);

    connect(bSlider, &QSlider::valueChanged, this, [bVal](int v) {
        bVal->setText(QString::number(v));
        QFile f("/sys/class/backlight/backlight-lvds/brightness");
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray::number(v));
    });

    bottom->addWidget(brightWidget, 1);
#else
    bottom->addStretch(1);
#endif
    bottom->addWidget(m_runBtn);

    layout->addLayout(grid);
    layout->addLayout(bottom);

    connect(m_runBtn, &QPushButton::clicked, this, &CommTest::toggleRun);
}

/* ── helpers ────────────────────────────────────────────── */
void CommTest::setRunButton() {
    m_runBtn->setText(m_running ? "on" : "off");
    m_runBtn->setStyleSheet(runStyle(m_running));
}

void CommTest::resetCounters() {
    m_cycle = 0;
    for (auto &row : m_rows) {
        row.totalCount = row.passCount = row.failCount = 0;
        updateRow(row);
    }
}

void CommTest::updateRow(TargetRow& row) {
    if (row.totalLabel) row.totalLabel->setText(QString::number(row.totalCount));
    if (row.passLabel)  row.passLabel->setText(QString::number(row.passCount));
    if (row.failLabel)  row.failLabel->setText(QString::number(row.failCount));
}

bool CommTest::checkComPort(const QString& device) const {
#ifdef Q_OS_WIN
    QString portName = device.startsWith("\\\\.\\") ? device : ("\\\\.\\" + device);
    HANDLE h = CreateFileW(portName.toStdWString().c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
#else
    int fd = ::open(device.toStdString().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    ::close(fd);
    return true;
#endif
}

bool CommTest::checkLanLink(const QString& ip) const {
#ifdef Q_OS_WIN
    QByteArray cmd = QString("ping -n 1 -w 1000 %1 >nul 2>&1").arg(ip).toLocal8Bit();
    return ::system(cmd.constData()) == 0;
#else
    QByteArray cmd = QString("ping -n -c 1 -W 1 %1 >/dev/null 2>&1").arg(ip).toLocal8Bit();
    FILE* f = ::popen(cmd.constData(), "r");
    if (!f) return false;
    int ret = ::pclose(f);
    return WIFEXITED(ret) && WEXITSTATUS(ret) == 0;
#endif
}

#ifdef Q_OS_WIN
static bool checkArpComplete(const QString& ip) {
    FILE* f = _popen("arp -a", "r");
    if (!f) return false;
    char buf[256]{};
    const QByteArray needle = ip.toLatin1();
    bool found = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, needle.constData())) { found = true; break; }
    }
    _pclose(f);
    return found;
}
#else
static bool checkArpComplete(const QString& ip) {
    int fd = ::open("/proc/net/arp", O_RDONLY);
    if (fd < 0) return false;
    char buf[4096]{};
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    const QByteArray needle = ip.toLatin1();
    const char* p = buf;
    while (*p) {
        const char* eol = p;
        while (*eol && *eol != '\n') ++eol;
        if (qstrncmp(p, needle.constData(), needle.size()) == 0 && p[needle.size()] == ' ') {
            const char* tok = p;
            for (int field = 0; field < 2; ++field) {
                while (*tok && *tok != ' ') ++tok;
                while (*tok == ' ') ++tok;
            }
            const char* flagEnd = tok;
            while (flagEnd < eol && *flagEnd != ' ') ++flagEnd;
            bool ok;
            int flags = QByteArray(tok, int(flagEnd - tok)).toInt(&ok, 16);
            return ok && (flags & 0x2);
        }
        p = (*eol == '\n') ? eol + 1 : eol;
    }
    return false;
}
#endif

bool CommTest::checkLanAnyLink(const QStringList& ips, const QString& skipIp) const {
    for (const auto& ip : ips) {
        if (!skipIp.isEmpty() && ip == skipIp) continue;
        if (checkArpComplete(ip)) return true;
    }
#ifdef Q_OS_WIN
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock != INVALID_SOCKET) {
        for (const auto& ip : ips) {
            if (!skipIp.isEmpty() && ip == skipIp) continue;
            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(7);
            inet_pton(AF_INET, ip.toLocal8Bit().constData(), &addr.sin_addr);
            ::sendto(sock, "\x00", 1, 0, (sockaddr*)&addr, sizeof(addr));
        }
        ::closesocket(sock);
        Sleep(50);
        for (const auto& ip : ips) {
            if (!skipIp.isEmpty() && ip == skipIp) continue;
            if (checkArpComplete(ip)) return true;
        }
    }
#else
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        for (const auto& ip : ips) {
            if (!skipIp.isEmpty() && ip == skipIp) continue;
            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(7);
            if (::inet_aton(ip.toLocal8Bit().constData(), &addr.sin_addr))
                ::sendto(sock, "\x00", 1, 0, (sockaddr*)&addr, sizeof(addr));
        }
        ::close(sock);
        ::usleep(50000);
        for (const auto& ip : ips) {
            if (!skipIp.isEmpty() && ip == skipIp) continue;
            if (checkArpComplete(ip)) return true;
        }
    }
#endif
    return false;
}

#ifndef Q_OS_WIN
bool CommTest::checkSerialRoundTrip(const QString& device) const {
    const QByteArray payload("test\n");
    int fd = ::open(device.toStdString().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    struct termios tio{};
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &tio);
    if (::write(fd, payload.constData(), payload.size()) != payload.size()) {
        ::close(fd); return false;
    }
    char buf[64]{};
    fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
    timeval tv{0, 200000};
    bool ok = false;
    if (::select(fd+1, &rfds, nullptr, nullptr, &tv) > 0 && FD_ISSET(fd, &rfds))
        ok = (::read(fd, buf, sizeof(buf)) > 0);
    ::close(fd);
    return ok;
}
#endif  /* !Q_OS_WIN — end of checkSerialRoundTrip */

#ifdef Q_OS_WIN
/* ── Windows serial ─────────────────────────────────────── */
static HANDLE openSerial(const QString& device, int flowCtrl) {
    QString portName = device.startsWith("\\\\.\\") ? device : ("\\\\.\\" + device);
    HANDLE h = CreateFileW(portName.toStdWString().c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fBinary  = TRUE;
    dcb.fOutxCtsFlow = (flowCtrl == 1) ? TRUE : FALSE;
    dcb.fRtsControl  = (flowCtrl == 1) ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    dcb.fOutX = dcb.fInX = (flowCtrl == 2) ? TRUE : FALSE;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    COMMTIMEOUTS ct{};
    ct.ReadIntervalTimeout = MAXDWORD;  // non-blocking read
    SetCommTimeouts(h, &ct);
    PurgeComm(h, PURGE_TXCLEAR);
    return h;
}

bool CommTest::checkSerialTx(const QString& device, const QByteArray& payload, int flowCtrl, int /*comType*/) const {
    HANDLE h = openSerial(device, flowCtrl);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, payload.constData(), (DWORD)payload.size(), &written, nullptr)
              && written == (DWORD)payload.size();
    CloseHandle(h);
    return ok;
}

bool CommTest::checkSerialRx(void* h) const {
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    char buf[256]{};
    DWORD n = 0;
    return ReadFile((HANDLE)h, buf, sizeof(buf), &n, nullptr) && n > 0;
}

void CommTest::openComFds() {
    for (int i = 0; i < 3; ++i) {
        if (m_comFds[i] != INVALID_HANDLE_VALUE) { CloseHandle((HANDLE)m_comFds[i]); m_comFds[i] = (void*)(intptr_t)-1; }
        // TX mode: open/close per-tick — don't hold exclusive handle here
        if (i < m_rows.size() && m_rows[i].txrxMode == 1) {
            const auto &r = m_rows[i];
            m_comFds[i] = (void*)openSerial(r.device, r.flowCtrl);
        }
    }
}

void CommTest::closeComFds() {
    for (int i = 0; i < 3; ++i) {
        if (m_comFds[i] != INVALID_HANDLE_VALUE) {
            CloseHandle((HANDLE)m_comFds[i]);
            m_comFds[i] = (void*)(intptr_t)-1;
        }
    }
}

#else
/* ── Linux serial ───────────────────────────────────────── */
static speed_t toBaudConst(int baud) {
    switch (baud) {
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

static int openSerial(const QString& device, int flowCtrl, int baudRate = 9600, int comType = 0) {
    int fd = ::open(device.toStdString().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios tio{};
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    speed_t spd = toBaudConst(baudRate);
    cfsetispeed(&tio, spd); cfsetospeed(&tio, spd);
    if (flowCtrl == 1) tio.c_cflag |= CRTSCTS;
    else if (flowCtrl == 2) tio.c_iflag |= (IXON | IXOFF);
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd, TCSANOW, &tio);
    if (comType == 1) {
        struct serial_rs485 rs485{};
        rs485.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
        ioctl(fd, TIOCSRS485, &rs485);
    }
    return fd;
}

bool CommTest::checkSerialTx(const QString& device, const QByteArray& payload, int flowCtrl, int comType) const {
    int fd = openSerial(device, flowCtrl, 9600, comType);
    if (fd < 0) return false;
    bool ok = (::write(fd, payload.constData(), payload.size()) == payload.size());
    ::close(fd);
    return ok;
}

bool CommTest::checkSerialRx(int fd) const {
    if (fd < 0) return false;
    char buf[256]{};
    fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
    timeval tv{0, 200000}; // 200ms wait
    if (::select(fd+1, &rfds, nullptr, nullptr, &tv) > 0 && FD_ISSET(fd, &rfds)) {
        ssize_t rd = ::read(fd, buf, sizeof(buf) - 1);
        return rd > 0;
    }
    return false;
}

void CommTest::openComFds() {
    for (int i = 0; i < 3; ++i) {
        if (m_comFds[i] >= 0) { ::close(m_comFds[i]); m_comFds[i] = -1; }
        // TX mode: open/close per-tick — only keep RX ports open
        if (i < m_rows.size() && m_rows[i].txrxMode == 1) {
            const auto &r = m_rows[i];
            m_comFds[i] = openSerial(r.device, r.flowCtrl, 9600, r.comType);
        }
    }
}

void CommTest::closeComFds() {
    for (int i = 0; i < 3; ++i) {
        if (m_comFds[i] >= 0) { ::close(m_comFds[i]); m_comFds[i] = -1; }
    }
}
#endif  /* Q_OS_WIN */

void CommTest::bumpTotal(int idx) {
    if (idx < 0 || idx >= m_rows.size()) return;
    m_rows[idx].totalCount++;
    updateRow(m_rows[idx]);
}
void CommTest::addResult(int idx, bool ok) {
    if (idx < 0 || idx >= m_rows.size()) return;
    if (ok) m_rows[idx].passCount++; else m_rows[idx].failCount++;
    updateRow(m_rows[idx]);
}

/* ── periodic tick ──────────────────────────────────────── */
void CommTest::onTick() {
    if (!m_running) return;
    ++m_cycle;
    updateClock();

    if (m_working) return;  // previous check still running, skip
    m_working = true;

    /* snapshot row data needed by background thread */
    struct RowSnap {
        int idx; bool isLan;
        QString device, ip; int txrxMode, flowCtrl, comType;
        QByteArray payload; QStringList peers;
#ifdef Q_OS_WIN
        void* persistFd = (void*)(intptr_t)-1;
#else
        int persistFd = -1;
#endif
    };
    static const QByteArray payloads[3] = {"test1", "test2", "test3"};
    const QStringList lan1Peers = {"192.168.1.100","192.168.1.101","192.168.1.102","192.168.1.103"};
    const QStringList lan2Peers = {"192.168.2.100","192.168.2.101","192.168.2.102","192.168.2.103"};

    QVector<RowSnap> snaps;
    for (int i = 0; i < 3; ++i) {
        const auto &r = m_rows[i];
#ifdef Q_OS_WIN
        void* pfd = (r.txrxMode == 1) ? m_comFds[i] : (void*)(intptr_t)-1;
#else
        int pfd = (r.txrxMode == 1) ? m_comFds[i] : -1;
#endif
        snaps.append({i, false, r.device, {}, r.txrxMode, r.flowCtrl, r.comType, payloads[i], {}, pfd});
    }
    snaps.append({3, true, {}, m_rows[3].ip, 0, 0, 0, {}, lan1Peers});
    snaps.append({4, true, {}, m_rows[4].ip, 0, 0, 0, {}, lan2Peers});

    /* run all row checks in parallel, then update all counts at once */
    const int n = snaps.size();
    auto *results  = new QVector<QPair<int,bool>>(n);
    auto *pending  = new std::atomic<int>(n);
    for (int i = 0; i < n; ++i) {
        const auto s = snaps[i];
        QThread *t = QThread::create([this, s, i, results, pending]() {
            bool ok;
            if (s.isLan) {
                ok = checkLanAnyLink(s.peers, s.ip);
            } else if (s.txrxMode == 0) {
                // TX: open exclusively per-tick (no persistent fd)
                ok = checkSerialTx(s.device, s.payload + "\n", s.flowCtrl, s.comType);
            } else {
                // RX: use persistent fd opened at Run time
                ok = checkSerialRx(s.persistFd);
            }
            (*results)[i] = {s.idx, ok};
            if (--(*pending) == 0) {
                /* all checks done — update every row simultaneously */
                QVector<QPair<int,bool>> copy = *results;
                delete results;
                delete pending;
                QMetaObject::invokeMethod(this, [this, copy]() {
                    if (m_running) {
                        for (const auto &r : copy) {
                            bumpTotal(r.first);
                            addResult(r.first, r.second);
                        }
                    }
                    m_working = false;
                }, Qt::QueuedConnection);
            }
        });
        t->start();
        connect(t, &QThread::finished, t, &QThread::deleteLater);
    }
}

/* ── clock ──────────────────────────────────────────────── */
void CommTest::updateClock() {
    if (!m_clockLabel) return;
    const auto now = QDateTime::currentDateTime();
    const QString days[] = {"일","월","화","수","목","금","토"};
    m_clockLabel->setText(QString("%1/%2/%3(%4) %5:%6:%7")
        .arg(now.date().year())
        .arg(now.date().month(),  2,10,QChar('0'))
        .arg(now.date().day(),    2,10,QChar('0'))
        .arg(days[now.date().dayOfWeek()-1])
        .arg(now.time().hour(),   2,10,QChar('0'))
        .arg(now.time().minute(), 2,10,QChar('0'))
        .arg(now.time().second(), 2,10,QChar('0')));
}

bool CommTest::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_clockLabel && event->type() == QEvent::MouseButtonRelease) {
        showTimePicker();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void CommTest::syncNtp() {
#ifndef Q_OS_WIN
    // Try chronyc first; if no NTP source reachable, fall back to server time API
    QProcess p;
    p.start("sh", {"-c",
        "chronyc makestep 2>/dev/null && chronyc waitsync 1 0 0 3 2>/dev/null && hwclock -w 2>/dev/null && exit 0;"
        "T=$(curl -s --max-time 3 http://192.168.1.103:8888/time 2>/dev/null);"
        "[ -n \"$T\" ] && date -s \"$T\" && hwclock -w 2>/dev/null"});
    p.waitForFinished(8000);
    updateClock();
#endif
}

void CommTest::syncTimeFromServer() {
#ifndef Q_OS_WIN
    QProcess p;
    p.start("sh", {"-c",
        "T=$(curl -s --max-time 5 http://192.168.1.103:8888/time 2>/dev/null);"
        "[ -n \"$T\" ] && date -s \"$T\" && hwclock -w 2>/dev/null"});
    p.waitForFinished(8000);
    updateClock();
#endif
}

void CommTest::showTimePicker() {
    // Use inline overlay widget instead of QDialog (Weston kiosk forces dialogs fullscreen)
    if (m_timePickerOverlay) {
        m_timePickerOverlay->setVisible(!m_timePickerOverlay->isVisible());
        if (m_timePickerOverlay->isVisible()) {
            // Refresh spinbox values to current time
            QDateTime now = QDateTime::currentDateTime();
            const int vals[6] = {
                now.date().year(), now.date().month(), now.date().day(),
                now.time().hour(), now.time().minute(), now.time().second()
            };
            for (int i = 0; i < 6; ++i)
                if (m_timeSpins[i]) m_timeSpins[i]->setValue(vals[i]);
            m_timePickerOverlay->raise();
        }
        return;
    }

    // Build overlay once
    m_timePickerOverlay = new QWidget(this, Qt::Widget);
    m_timePickerOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_timePickerOverlay->setStyleSheet(
        "QWidget#timePicker{background:#fff;border:2px solid #3b82f6;border-radius:10px;}"
        "QLabel{background:transparent;border:none;}");
    m_timePickerOverlay->setObjectName("timePicker");

    auto *ol = new QVBoxLayout(m_timePickerOverlay);
    ol->setSpacing(4);
    ol->setContentsMargins(8, 8, 8, 8);

    QDateTime now = QDateTime::currentDateTime();
    const int initVals[6] = {
        now.date().year(), now.date().month(), now.date().day(),
        now.time().hour(), now.time().minute(), now.time().second()
    };
    const int mins[6] = {2020, 1, 1, 0, 0, 0};
    const int maxs[6] = {2099, 12, 31, 23, 59, 59};
    const char *lbls[6] = {"년", "월", "일", "시", "분", "초"};

    const QString upStyle  = "QPushButton{font-size:10px;background:#f1f5f9;border:1px solid #d1d5db;"
                             "border-radius:3px;}QPushButton:pressed{background:#dbeafe;}";
    const QString spinStyle= "QSpinBox{font-size:13px;font-weight:700;border:1px solid #3b82f6;"
                             "border-radius:3px;color:#1f2937;background:#fff;}";

    auto makeCol = [&](int i) -> QWidget* {
        auto *col = new QWidget;
        auto *colL = new QVBoxLayout(col);
        colL->setSpacing(2); colL->setContentsMargins(0,0,0,0);

        auto *up = new QPushButton("▲");
        up->setFixedSize(36, 20); up->setStyleSheet(upStyle);

        m_timeSpins[i] = new QSpinBox;
        m_timeSpins[i]->setRange(mins[i], maxs[i]);
        m_timeSpins[i]->setValue(initVals[i]);
        m_timeSpins[i]->setFixedSize(36, 26);
        m_timeSpins[i]->setAlignment(Qt::AlignCenter);
        m_timeSpins[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_timeSpins[i]->setStyleSheet(spinStyle);

        auto *dn = new QPushButton("▼");
        dn->setFixedSize(36, 20); dn->setStyleSheet(upStyle);

        auto *lbl = new QLabel(lbls[i]);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("font-size:9px;color:#6b7280;");

        QSpinBox *sp = m_timeSpins[i];
        connect(up, &QPushButton::clicked, sp, [sp]{ sp->stepUp(); });
        connect(dn, &QPushButton::clicked, sp, [sp]{ sp->stepDown(); });

        colL->addWidget(up); colL->addWidget(m_timeSpins[i]);
        colL->addWidget(dn); colL->addWidget(lbl);
        return col;
    };

    {
        auto *r = new QWidget; auto *rl = new QHBoxLayout(r);
        rl->setSpacing(3); rl->setContentsMargins(0,0,0,0);
        for (int i = 0; i < 6; ++i) rl->addWidget(makeCol(i));
        ol->addWidget(r);
    }

    const QString btnStyle = "QPushButton{font-size:11px;font-weight:600;padding:2px 10px;"
                             "border-radius:4px;border:1px solid #d1d5db;background:#f8fafc;}"
                             "QPushButton:pressed{background:#dbeafe;}";
    auto *btnRow = new QWidget; auto *bl = new QHBoxLayout(btnRow);
    bl->setSpacing(6); bl->setContentsMargins(0,0,0,0);
    auto *applyBtn  = new QPushButton("적용");
    auto *cancelBtn = new QPushButton("취소");
    applyBtn->setFixedHeight(24); cancelBtn->setFixedHeight(24);
    applyBtn->setStyleSheet(btnStyle); cancelBtn->setStyleSheet(btnStyle);
    bl->addWidget(applyBtn); bl->addWidget(cancelBtn);
    ol->addWidget(btnRow);

    m_timePickerOverlay->adjustSize();

    // Position below clock label
    auto pos = m_clockLabel->mapTo(this, QPoint(0, m_clockLabel->height() + 2));
    m_timePickerOverlay->move(pos);
    m_timePickerOverlay->raise();
    m_timePickerOverlay->show();

    connect(cancelBtn, &QPushButton::clicked, m_timePickerOverlay, &QWidget::hide);
    connect(applyBtn,  &QPushButton::clicked, this, [this]() {
        m_timePickerOverlay->hide();
#ifdef Q_OS_WIN
        SYSTEMTIME st{};
        st.wYear=(WORD)m_timeSpins[0]->value(); st.wMonth =(WORD)m_timeSpins[1]->value();
        st.wDay =(WORD)m_timeSpins[2]->value(); st.wHour  =(WORD)m_timeSpins[3]->value();
        st.wMinute=(WORD)m_timeSpins[4]->value();st.wSecond=(WORD)m_timeSpins[5]->value();
        st.wMilliseconds=0; SetLocalTime(&st);
#else
        const QString cmd = QString("date -s \"%1-%2-%3 %4:%5:%6\"")
            .arg(m_timeSpins[0]->value())
            .arg(m_timeSpins[1]->value(),2,10,QChar('0'))
            .arg(m_timeSpins[2]->value(),2,10,QChar('0'))
            .arg(m_timeSpins[3]->value(),2,10,QChar('0'))
            .arg(m_timeSpins[4]->value(),2,10,QChar('0'))
            .arg(m_timeSpins[5]->value(),2,10,QChar('0'));
        QProcess::execute("sh", {"-c", cmd});
        QProcess::execute("sh", {"-c", "hwclock -w 2>/dev/null || true"});
#endif
        updateClock();
    });
}

/* ── active state ───────────────────────────────────────── */
void CommTest::setActive(bool active) {
    if (active) {
        if (m_clockTimer) m_clockTimer->start();
    } else {
        if (m_clockTimer) m_clockTimer->stop();
        if (m_timer && m_running) m_timer->stop();
    }
}

void CommTest::toggleRun() {
    m_running = !m_running;
    setRunButton();
    if (m_running) {
        openComFds();
        if (m_timer) m_timer->start();
    } else {
        if (m_timer) m_timer->stop();
        closeComFds();
        resetCounters();
    }
}

void CommTest::switchBoardMode(int mode) {
    m_boardMode = mode;
    static const QString devices[3][3] = {
        {"/dev/ttymxc3",  "/dev/ttymxc2", "/dev/ttyACM0"}, // 0: iMX8MP
        {"/dev/ttyUSB0", "/dev/ttyUSB1",  "/dev/ttyUSB2"}, // 1: Linux PC
        {"COM3",         "COM2",          "COM1"},          // 2: Windows
    };
    static const QString details[3][3] = {
        {"/dev/ttymxc3",        "/dev/ttymxc2",        "RS485  /dev/ttyACM0"}, // 0: iMX8MP
        {"/dev/ttyUSB0",        "/dev/ttyUSB1",        "RS485  /dev/ttyUSB2"}, // 1: Linux PC
        {"COM3",                "COM2",                "RS485  COM1"},          // 2: Windows
    };
    // Row titles: Windows mode shows Windows port numbers (COM3/COM2/COM1), others show physical (COM1/COM2/COM3)
    static const QString titles[3][3] = {
        {"COM1", "COM2", "COM3"},  // 0: iMX8MP
        {"COM1", "COM2", "COM3"},  // 1: Linux PC
        {"COM3", "COM2", "COM1"},  // 2: Windows
    };
    for (int i = 0; i < 3 && i < m_rows.size(); ++i) {
        m_rows[i].device = devices[mode][i];
        m_rows[i].detail = details[mode][i];
        m_rows[i].title  = titles[mode][i];
        if (m_rows[i].titleLabel)
            m_rows[i].titleLabel->setText(titles[mode][i]);
        if (m_rows[i].deviceEdit)
            m_rows[i].deviceEdit->setText(devices[mode][i]);
        if (m_rows[i].deviceCombo) {
            int idx = m_rows[i].deviceCombo->findText(devices[mode][i]);
            m_rows[i].deviceCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        if (m_rows[i].detailLabel)
            m_rows[i].detailLabel->setText(details[mode][i]);
    }
    for (int i = 0; i < 3; ++i) {
        if (!m_modeBtns[i]) continue;
        bool sel = (i == mode);
        m_modeBtns[i]->setStyleSheet(sel
            ? "QPushButton{font-size:11px;font-weight:700;background:#dbeafe;color:#1e3a8a;"
              "border:1px solid #93c5fd;border-radius:8px;padding:2px 6px;}"
            : "QPushButton{font-size:11px;font-weight:700;background:#f1f5f9;color:#374151;"
              "border:1px solid #c9d3df;border-radius:8px;padding:2px 6px;}");
    }
}
