#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class CommTest : public QWidget {
    Q_OBJECT
public:
    explicit CommTest(QWidget* parent = nullptr);
    void setActive(bool active);

private slots:
    void toggleRun();
    void onTick();

private:
    enum class TargetKind { Com1, Com2, Com3, Lan1, Lan2 };
    struct TargetRow {
        TargetKind kind       = TargetKind::Com1;
        QString    title;
        QString    detail;
        QString    device;
        QString    ip;
        int        comType    = 0;  // 0=RS232, 1=RS422/485
        int        txrxMode   = 0;  // 0=TX(송신), 1=RX(수신)
        int        flowCtrl   = 0;  // 0=None, 1=HW(RTS/CTS), 2=SW(XON/XOFF)
        QLabel*    titleLabel   = nullptr;
        QLabel*    currentIpLabel = nullptr;  // LAN1/LAN2 current IP display
        QPushButton* typeButtons[2] = {};   // COM1/COM2 type select
        QPushButton* txrxButtons[2] = {};   // TX/RX mode select (all COM)
        QPushButton* flowButtons[2] = {};   // COM1 flow control (없음 / 흐름제어)
        QPushButton* ipButtons[4]   = {};   // LAN IP select
        QLineEdit* deviceEdit   = nullptr;
        QLabel*    detailLabel = nullptr;
        QLabel*    totalLabel  = nullptr;
        QLabel*    passLabel   = nullptr;
        QLabel*    failLabel   = nullptr;
        int totalCount = 0;
        int passCount  = 0;
        int failCount  = 0;
    };

    void buildUi();
    void resetCounters();
    void updateRow(TargetRow& row);
    bool checkComPort(const QString& device) const;
    bool checkLanLink(const QString& ip) const;
    bool checkLanAnyLink(const QStringList& ips, const QString& skipIp = {}) const;
#ifndef Q_OS_WIN
    bool checkSerialRoundTrip(const QString& device) const;
#endif
    bool checkSerialTx(const QString& device, const QByteArray& payload, int flowCtrl = 0) const;
#ifdef Q_OS_WIN
    bool checkSerialRx(void* h) const;
#else
    bool checkSerialRx(int fd) const;
#endif
    void openComFds();
    void closeComFds();
    void setRunButton();
    void updateClock();
    void bumpTotal(int rowIndex);
    void addResult(int rowIndex, bool ok);

    QVector<TargetRow> m_rows;
    QLabel*      m_clockLabel   = nullptr;
    QPushButton* m_runBtn       = nullptr;
    QPushButton* m_masterTxBtn  = nullptr;
    QPushButton* m_masterRxBtn  = nullptr;
    QTimer*      m_timer        = nullptr;
    QTimer*      m_clockTimer   = nullptr;
    int  m_cycle   = 0;
    bool m_running = false;
    bool m_working = false;
#ifdef Q_OS_WIN
    void* m_comFds[3] = {(void*)(intptr_t)-1, (void*)(intptr_t)-1, (void*)(intptr_t)-1};
#else
    int   m_comFds[3] = {-1, -1, -1};
#endif
};
