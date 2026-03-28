#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QFile>
#include "hiddevice.h"
#include "iaprotocol.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget * parent = nullptr);
    ~MainWindow();

  private slots:
    void onSelectFileClicked();
    void onStartUpgradeClicked();
    void onDeviceConnected();
    void onDeviceDisconnected();
    void onDataReceived(const QByteArray & data);
    void onErrorOccurred(const QString & error);

  private:
    void setupUi();
    void log(const QString & message);
    void updateStatus(const QString & status);
    bool sendCommand(const QByteArray & cmd);
    QByteArray sendAndReceive(const QByteArray & cmd, int timeoutMs = 2000);
    bool startIap();
    bool sendAddress(uint32_t address);
    uint32_t loadFirmwareFile(const QString & filePath);

    // UI components
    QLabel * m_statusLabel;
    QLabel * m_statusIndicator;
    QPushButton * m_selectFileBtn;
    QPushButton * m_startBtn;
    QLineEdit * m_filePathEdit;
    QLineEdit * m_addressEdit;
    QLineEdit * m_vidEdit;
    QLineEdit * m_pidEdit;
    QProgressBar * m_progressBar;
    QTextEdit * m_logEdit;

    // Device and protocol
    HidDevice * m_hidDevice;

    // Firmware data
    QByteArray m_firmwareData;
    QString m_firmwarePath;
    QString m_lastDirPath;

    // Device IDs
    unsigned short m_vendorId;
    unsigned short m_productId;
};

#endif // MAINWINDOW_H
