#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QFrame>
#include <QGraphicsDropShadowEffect>

MainWindow::MainWindow(QWidget * parent)
  : QMainWindow(parent)
  , m_hidDevice(new HidDevice(this))
  , m_vendorId(0x2E3C)
  , m_productId(0xAF01)
{
  setupUi();

  connect(m_hidDevice, &HidDevice::dataReceived, this, &MainWindow::onDataReceived);
  connect(m_hidDevice, &HidDevice::errorOccurred, this, &MainWindow::onErrorOccurred);

  m_startBtn->setEnabled(false);
}

MainWindow::~MainWindow()
{
  if(m_hidDevice->isOpen())
  {
    m_hidDevice->close();
  }
}

void MainWindow::setupUi()
{
  // 设置窗口整体样式
  setStyleSheet(R"(
    QMainWindow {
      background-color: #f5f7fa;
    }
    QLabel {
      color: #2c3e50;
      font-size: 13px;
    }
    QLineEdit {
      padding: 4px 10px;
      border: 2px solid #e1e8ed;
      border-radius: 8px;
      background-color: white;
      color: #2c3e50;
      font-size: 13px;
      min-height: 16px;
      selection-background-color: #3498db;
    }
    QLineEdit:focus {
      border-color: #3498db;
    }
    QLineEdit:disabled {
      background-color: #ecf0f1;
      color: #95a5a6;
    }
    QPushButton {
      padding: 10px 24px;
      border: none;
      border-radius: 10px;
      font-size: 13px;
      font-weight: bold;
      color: white;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #667eea, stop:1 #764ba2);
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #5a6fd6, stop:1 #6a4190);
    }
    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4e5fc4, stop:1 #5e3780);
    }
    QPushButton:disabled {
      background: #bdc3c7;
      color: #ecf0f1;
    }
    QProgressBar {
      border: none;
      border-radius: 8px;
      background-color: #e1e8ed;
      text-align: center;
      font-weight: bold;
      color: #2c3e50;
      height: 24px;
    }
    QProgressBar::chunk {
      border-radius: 8px;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #11998e, stop:1 #38ef7d);
    }
    QTextEdit {
      border: 2px solid #e1e8ed;
      border-radius: 12px;
      background-color: #1e293b;
      color: #e2e8f0;
      font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
      font-size: 12px;
      padding: 12px;
      selection-background-color: #3498db;
    }
    QScrollBar:vertical {
      border: none;
      background: #f1f5f9;
      width: 10px;
      border-radius: 5px;
    }
    QScrollBar::handle:vertical {
      background: #cbd5e1;
      border-radius: 5px;
      min-height: 30px;
    }
    QScrollBar::handle:vertical:hover {
      background: #94a3b8;
    }
  )");

  QWidget * centralWidget = new QWidget(this);
  QVBoxLayout * mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->setSpacing(5);
  mainLayout->setContentsMargins(5, 5, 5, 5);

  // 状态卡片
  QFrame * statusCard = new QFrame(this);
  statusCard->setStyleSheet(R"(
    QFrame {
      background-color: #e2e8f0;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * statusShadow = new QGraphicsDropShadowEffect(this);
  statusShadow->setBlurRadius(20);
  statusShadow->setColor(QColor(0, 0, 0, 30));
  statusShadow->setOffset(0, 4);
  statusCard->setGraphicsEffect(statusShadow);

  QHBoxLayout * statusCardLayout = new QHBoxLayout(statusCard);
  statusCardLayout->setContentsMargins(12, 8, 12, 8);

  QLabel * statusTitle = new QLabel(tr("Device Status"), this);
  statusTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  statusCardLayout->addWidget(statusTitle);

  statusCardLayout->addStretch();

  // 状态指示器
  m_statusIndicator = new QLabel(this);
  m_statusIndicator->setFixedSize(12, 12);
  m_statusIndicator->setStyleSheet("QLabel { background-color: #ef4444; border-radius: 6px; }");
  statusCardLayout->addWidget(m_statusIndicator);

  m_statusLabel = new QLabel(tr("Disconnected"), this);
  m_statusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; font-size: 14px; }");
  statusCardLayout->addWidget(m_statusLabel);

  mainLayout->addWidget(statusCard);

  // 文件选择卡片
  QFrame * fileCard = new QFrame(this);
  fileCard->setStyleSheet(R"(
    QFrame {
      background-color: #dbeafe;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * fileShadow = new QGraphicsDropShadowEffect(this);
  fileShadow->setBlurRadius(20);
  fileShadow->setColor(QColor(0, 0, 0, 30));
  fileShadow->setOffset(0, 4);
  fileCard->setGraphicsEffect(fileShadow);

  QVBoxLayout * fileCardLayout = new QVBoxLayout(fileCard);
  fileCardLayout->setContentsMargins(12, 12, 12, 12);
  fileCardLayout->setSpacing(8);

  QLabel * fileTitle = new QLabel(tr("Firmware File"), this);
  fileTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  fileCardLayout->addWidget(fileTitle);

  QHBoxLayout * fileLayout = new QHBoxLayout();
  fileLayout->setSpacing(12);
  m_filePathEdit = new CustomLineEdit(this);
  m_filePathEdit->setReadOnly(true);
  m_filePathEdit->setPlaceholderText(tr("Select firmware file (.bin or .hex)"));
  fileLayout->addWidget(m_filePathEdit);

  m_selectFileBtn = new QPushButton(tr("Browse"), this);
  m_selectFileBtn->setFixedWidth(120);
  m_selectFileBtn->setFixedHeight(36);
  fileLayout->addWidget(m_selectFileBtn);
  fileCardLayout->addLayout(fileLayout);

  mainLayout->addWidget(fileCard);

  // 设备配置卡片
  QFrame * configCard = new QFrame(this);
  configCard->setStyleSheet(R"(
    QFrame {
      background-color: #dcfce7;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * configShadow = new QGraphicsDropShadowEffect(this);
  configShadow->setBlurRadius(20);
  configShadow->setColor(QColor(0, 0, 0, 30));
  configShadow->setOffset(0, 4);
  configCard->setGraphicsEffect(configShadow);

  QVBoxLayout * configCardLayout = new QVBoxLayout(configCard);
  configCardLayout->setContentsMargins(12, 12, 12, 12);
  configCardLayout->setSpacing(8);

  QLabel * configTitle = new QLabel(tr("Device Configuration"), this);
  configTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  configCardLayout->addWidget(configTitle);

  // VID/PID 输入
  QHBoxLayout * deviceIdLayout = new QHBoxLayout();
  deviceIdLayout->setSpacing(20);

  QVBoxLayout * vidLayout = new QVBoxLayout();
  vidLayout->setSpacing(6);
  QLabel * vidLabel = new QLabel(tr("Vendor ID (VID)"), this);
  vidLabel->setStyleSheet("QLabel { color: #64748b; font-size: 12px; }");
  vidLayout->addWidget(vidLabel);
  m_vidEdit = new CustomLineEdit(this);
  m_vidEdit->setText("0x2E3C");
  m_vidEdit->setPlaceholderText(tr("Enter VID (hex)"));
  m_vidEdit->setMinimumWidth(100);
  m_vidEdit->setFixedHeight(28);
  vidLayout->addWidget(m_vidEdit);
  deviceIdLayout->addLayout(vidLayout);

  QVBoxLayout * pidLayout = new QVBoxLayout();
  pidLayout->setSpacing(6);
  QLabel * pidLabel = new QLabel(tr("Product ID (PID)"), this);
  pidLabel->setStyleSheet("QLabel { color: #64748b; font-size: 12px; }");
  pidLayout->addWidget(pidLabel);
  m_pidEdit = new CustomLineEdit(this);
  m_pidEdit->setText("0xAF01");
  m_pidEdit->setPlaceholderText(tr("Enter PID (hex)"));
  m_pidEdit->setMinimumWidth(100);
  m_pidEdit->setFixedHeight(28);
  pidLayout->addWidget(m_pidEdit);
  deviceIdLayout->addLayout(pidLayout);

  QVBoxLayout * addrLayout = new QVBoxLayout();
  addrLayout->setSpacing(6);
  QLabel * addrLabel = new QLabel(tr("Flash Address"), this);
  addrLabel->setStyleSheet("QLabel { color: #64748b; font-size: 12px; }");
  addrLayout->addWidget(addrLabel);
  m_addressEdit = new CustomLineEdit(this);
  m_addressEdit->setText("0x08005000");
  m_addressEdit->setPlaceholderText(tr("Enter flash address (hex)"));
  m_addressEdit->setEnabled(false);
  m_addressEdit->setMinimumWidth(120);
  m_addressEdit->setFixedHeight(28);
  addrLayout->addWidget(m_addressEdit);
  deviceIdLayout->addLayout(addrLayout);

  deviceIdLayout->addStretch();
  configCardLayout->addLayout(deviceIdLayout);

  mainLayout->addWidget(configCard);

  // 进度卡片
  QFrame * progressCard = new QFrame(this);
  progressCard->setStyleSheet(R"(
    QFrame {
      background-color: #fef3c7;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * progressShadow = new QGraphicsDropShadowEffect(this);
  progressShadow->setBlurRadius(20);
  progressShadow->setColor(QColor(0, 0, 0, 30));
  progressShadow->setOffset(0, 4);
  progressCard->setGraphicsEffect(progressShadow);

  QVBoxLayout * progressCardLayout = new QVBoxLayout(progressCard);
  progressCardLayout->setContentsMargins(12, 12, 12, 12);
  progressCardLayout->setSpacing(8);

  QLabel * progressTitle = new QLabel(tr("Upgrade Progress"), this);
  progressTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  progressCardLayout->addWidget(progressTitle);

  QHBoxLayout * progressRowLayout = new QHBoxLayout();
  progressRowLayout->setSpacing(12);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFormat("%p%");
  m_progressBar->setValue(0);
  m_progressBar->setFixedHeight(28);
  progressRowLayout->addWidget(m_progressBar);

  // 升级按钮
  m_startBtn = new QPushButton(tr("Start Upgrade"), this);
  m_startBtn->setFixedWidth(120);
  m_startBtn->setFixedHeight(36);
  m_startBtn->setStyleSheet(R"(
    QPushButton {
      padding: 8px 20px;
      border: none;
      border-radius: 10px;
      font-size: 13px;
      font-weight: bold;
      color: white;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #11998e, stop:1 #38ef7d);
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0d8579, stop:1 #2dd46a);
    }
    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0a7166, stop:1 #25b85a);
    }
    QPushButton:disabled {
      background: #bdc3c7;
      color: #ecf0f1;
    }
  )");
  m_startBtn->setEnabled(false);
  progressRowLayout->addWidget(m_startBtn);

  progressCardLayout->addLayout(progressRowLayout);

  mainLayout->addWidget(progressCard);

  // 日志卡片
  QFrame * logCard = new QFrame(this);
  logCard->setStyleSheet(R"(
    QFrame {
      background-color: #ffffff;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * logShadow = new QGraphicsDropShadowEffect(this);
  logShadow->setBlurRadius(20);
  logShadow->setColor(QColor(0, 0, 0, 30));
  logShadow->setOffset(0, 4);
  logCard->setGraphicsEffect(logShadow);

  QVBoxLayout * logCardLayout = new QVBoxLayout(logCard);
  logCardLayout->setContentsMargins(12, 12, 12, 12);
  logCardLayout->setSpacing(8);

  QLabel * logTitle = new QLabel(tr("Activity Log"), this);
  logTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  logCardLayout->addWidget(logTitle);

  m_logEdit = new CustomTextEdit(this);
  m_logEdit->setReadOnly(true);
  m_logEdit->setMinimumHeight(100);
  logCardLayout->addWidget(m_logEdit, 1);

  mainLayout->addWidget(logCard);

  setCentralWidget(centralWidget);
  setWindowTitle(tr("iap-programmer"));
  setMinimumSize(600, 500);
  resize(800, 500);

  // Connect signals
  connect(m_selectFileBtn, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);
  connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartUpgradeClicked);
}

void MainWindow::log(const QString & message)
{
  QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");
  QString color;

  if(message.contains("Error", Qt::CaseInsensitive) || message.contains("Failed", Qt::CaseInsensitive))
  {
    color = "#f87171"; // 红色错误
  }
  else if(message.contains("Success", Qt::CaseInsensitive) || message.contains("completed", Qt::CaseInsensitive))
  {
    color = "#4ade80"; // 绿色成功
  }
  else if(message.contains("Warning", Qt::CaseInsensitive))
  {
    color = "#fbbf24"; // 黄色警告
  }
  else
  {
    color = "#94a3b8"; // 灰色普通
  }

  m_logEdit->append(QString("<span style='color:#64748b;'>%1</span> <span style='color:%2;'>%3</span>")
                    .arg(timestamp)
                    .arg(color)
                    .arg(message.toHtmlEscaped()));
}

void MainWindow::updateStatus(const QString & status)
{
  m_statusLabel->setText(status);

  if(status == tr("Connected"))
  {
    m_statusIndicator->setStyleSheet("QLabel { background-color: #22c55e; border-radius: 6px; box-shadow: 0 0 8px #22c55e; }");
    m_statusLabel->setStyleSheet("QLabel { color: #22c55e; font-weight: bold; font-size: 14px; }");
  }
  else
  {
    m_statusIndicator->setStyleSheet("QLabel { background-color: #ef4444; border-radius: 6px; }");
    m_statusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; font-size: 14px; }");
  }
}

void MainWindow::onSelectFileClicked()
{
  QString filePath = QFileDialog::getOpenFileName(this, tr("Select Firmware File"),
                                                  m_lastDirPath, tr("Firmware Files (*.bin *.hex);;Binary Files (*.bin);;Hex Files (*.hex);;All Files (*)"));

  if(!filePath.isEmpty())
  {
    // Save the directory path for next time
    m_lastDirPath = QFileInfo(filePath).path();

    uint32_t size = loadFirmwareFile(filePath);
    if(size > 0)
    {
      m_firmwarePath = filePath;
      m_filePathEdit->setText(filePath);
      m_filePathEdit->setStyleSheet("QLineEdit { padding: 10px 14px; border: 2px solid #11998e; border-radius: 10px; background-color: white; color: #2c3e50; font-size: 13px; }");
      m_addressEdit->setEnabled(true);
      log(tr("Loaded firmware: %1 (%2 bytes)").arg(filePath).arg(size));

      m_startBtn->setEnabled(true);
    }
  }
}

uint32_t MainWindow::loadFirmwareFile(const QString & filePath)
{
  QFile file(filePath);
  if(!file.open(QIODevice::ReadOnly))
  {
    log(tr("Failed to open file: %1").arg(file.errorString()));
    return 0;
  }

  // Check if file is hex format
  if(filePath.endsWith(".hex", Qt::CaseInsensitive))
  {
    // Parse hex file
    m_firmwareData.clear();
    uint32_t baseAddress = 0;
    uint32_t startAddress = 0xFFFFFFFF;
    QTextStream in(&file);

    while(!in.atEnd())
    {
      QString line = in.readLine().trimmed();
      if(line.isEmpty() || line[0] != ':')
      {
        continue;
      }

      // Parse hex record
      bool ok;
      int byteCount = line.mid(1, 2).toInt(&ok, 16);
      if(!ok || byteCount == 0) continue;

      int address = line.mid(3, 4).toInt(&ok, 16);
      if(!ok) continue;

      int recordType = line.mid(7, 2).toInt(&ok, 16);
      if(!ok) continue;

      // Handle different record types
      switch(recordType)
      {
        case 0x00: // Data record
          {
            uint32_t absoluteAddress = baseAddress + address;
            if(startAddress == 0xFFFFFFFF)
            {
              startAddress = absoluteAddress;
            }

            // Read data bytes
            for(int i = 0; i < byteCount; i++)
            {
              int pos = 9 + i * 2;
              if(pos + 2 <= line.length())
              {
                QString byteStr = line.mid(pos, 2);
                char byte = static_cast<char>(byteStr.toInt(&ok, 16));
                if(ok)
                {
                  m_firmwareData.append(byte);
                }
              }
            }
            break;
          }
        case 0x04: // Extended linear address record
          {
            if(byteCount == 2)
            {
              QString addressStr = line.mid(9, 4);
              baseAddress = addressStr.toInt(&ok, 16) << 16;
            }
            break;
          }
        case 0x01: // End of file record
          goto endOfFile;
      }
    }

endOfFile:
    file.close();

    if(startAddress != 0xFFFFFFFF)
    {
      // Set the address input to the start address from hex file
      m_addressEdit->setText(QString("0x%1").arg(startAddress, 8, 16, QChar('0')));
      log(tr("Loaded hex firmware: %1 (%2 bytes), start address: 0x%3").arg(filePath).arg(m_firmwareData.size()).arg(
            startAddress, 8, 16, QChar('0')));
    }
    else
    {
      log(tr("Failed to parse hex file: %1").arg(filePath));
      return 0;
    }
  }
  else
  {
    // Load as binary file
    m_firmwareData = file.readAll();
    file.close();
    log(tr("Loaded binary firmware: %1 (%2 bytes)").arg(filePath).arg(m_firmwareData.size()));
  }

  return m_firmwareData.size();
}

void MainWindow::onStartUpgradeClicked()
{
  // Read VID and PID from UI
  bool ok;
  unsigned short vid = m_vidEdit->text().toUShort(&ok, 16);
  if(!ok)
  {
    log(tr("Invalid VID format, using default 0x2E3C"));
    vid = 0x2E3C;
  }
  m_vendorId = vid;

  unsigned short pid = m_pidEdit->text().toUShort(&ok, 16);
  if(!ok)
  {
    log(tr("Invalid PID format, using default 0xAF01"));
    pid = 0xAF01;
  }
  m_productId = pid;

  // Reload firmware file to ensure we have the latest content
  if(!m_firmwarePath.isEmpty())
  {
    uint32_t size = loadFirmwareFile(m_firmwarePath);
    if(size == 0)
    {
      log(tr("Failed to reload firmware file: %1").arg(m_firmwarePath));
      return;
    }
    log(tr("Reloaded firmware: %1 (%2 bytes)").arg(m_firmwarePath).arg(size));
  }

  m_startBtn->setEnabled(false);
  m_selectFileBtn->setEnabled(false);
  m_progressBar->setValue(0);

  QApplication::processEvents();

  log(tr("Connecting to device (VID: 0x%1, PID: 0x%2)...")
      .arg(m_vendorId, 4, 16, QChar('0'))
      .arg(m_productId, 4, 16, QChar('0')));

  if(m_hidDevice->open(m_vendorId, m_productId))
  {
    m_statusLabel->setText(tr("Connected"));
    m_statusIndicator->setStyleSheet("QLabel { background-color: #22c55e; border-radius: 6px; box-shadow: 0 0 8px #22c55e; }");
    m_statusLabel->setStyleSheet("QLabel { color: #22c55e; font-weight: bold; font-size: 14px; }");

    log(tr("Device connected successfully"));

    // Try to get device info
    QByteArray response = sendAndReceive(IapProtocol::buildGetCommand(), 1000);
    if(!response.isEmpty())
    {
      IapProtocol::Command cmd;
      IapProtocol::Response result;
      if(IapProtocol::parseResponse(response, cmd, result) && result == IapProtocol::ACK)
      {
        uint32_t appAddr = IapProtocol::parseGetResponse(response);
        log(tr("Application address: 0x%1").arg(appAddr, 8, 16, QChar('0')));

        // Try IDLE command to see if device is in bootloader mode
        response = sendAndReceive(IapProtocol::buildIdleCommand(), 1000);
        if(IapProtocol::parseResponse(response, cmd, result) && result == IapProtocol::ACK)
        {
          log(tr("Device is in IAP mode (bootloader), ready for upgrade"));
        }
        else
        {
          log(tr("Device may be in application mode, will trigger IAP mode on upgrade"));
        }
      }
      else
      {
        log(tr("Device response error, will retry during upgrade"));
      }
    }
    else
    {
      log(tr("Device not responding to GET command"));
    }
  }
  else
  {
    log(tr("Failed to connect: %1").arg(HidDevice::errorString()));
    QMessageBox::critical(this, tr("Connection Failed"),
                          tr("Could not connect to HID device.\n\n"
                             "Please ensure:\n"
                             "1. The device is connected via USB\n"
                             "2. You have proper USB permissions\n\n"
                             "Error: %1").arg(HidDevice::errorString()));
  }

  QThread::msleep(1000);
  m_hidDevice->close();
  m_hidDevice->open(m_vendorId, m_productId);

  bool success = startIap();

  m_startBtn->setEnabled(true);
  m_selectFileBtn->setEnabled(true);

  if(success)
  {
    m_progressBar->setValue(100);
    log(tr("Upgrade completed successfully!"));
    QMessageBox::information(this, tr("Success"), tr("Firmware upgrade completed successfully!"));
  }
  else
  {
    log(tr("Upgrade failed!"));
    QMessageBox::critical(this, tr("Failed"), tr("Firmware upgrade failed. Check the log for details."));
  }
}

bool MainWindow::sendCommand(const QByteArray & cmd)
{
  if(!m_hidDevice->isOpen())
  {
    log(tr("Device not connected, attempting to reconnect..."));
    if(!m_hidDevice->open(m_vendorId, m_productId))
    {
      log(tr("Reconnect failed: %1").arg(HidDevice::errorString()));
      return false;
    }
    log(tr("Device reconnected"));
  }

  int result = m_hidDevice->write(cmd);
  if(result < 0)
  {
    log(tr("Write failed, device may have disconnected"));
    // Try to reconnect once
    m_hidDevice->close();
    if(m_hidDevice->open(m_vendorId, m_productId))
    {
      log(tr("Reconnected, retrying write..."));
      result = m_hidDevice->write(cmd);
    }
  }
  return result > 0;
}

QByteArray MainWindow::sendAndReceive(const QByteArray & cmd, int timeoutMs)
{
  if(!sendCommand(cmd))
  {
    return QByteArray();
  }

  return m_hidDevice->read(timeoutMs);
}

bool MainWindow::startIap()
{
  log(tr("Starting IAP upgrade process..."));

  IapProtocol::Command cmd;
  IapProtocol::Response result;

  // Step 1: Send IDLE command to initialize device
  log(tr("Step 1: Sending IDLE command..."));
  QByteArray response = sendAndReceive(IapProtocol::buildIdleCommand(), 2000);
  if(response.isEmpty())
  {
    log(tr("Error: No response to IDLE command"));
    log(tr("Tip: Make sure device is connected and in IAP mode"));
    return false;
  }

  log(tr("IDLE response: %1 bytes, hex: %2").arg(response.size()).arg(response.toHex()));

  if(!IapProtocol::parseResponse(response, cmd, result))
  {
    log(tr("Error: Cannot parse IDLE response"));
    return false;
  }

  log(tr("IDLE command: 0x%1, result: 0x%2").arg(cmd, 4, 16, QChar('0')).arg(result, 4, 16, QChar('0')));

  if(result != IapProtocol::ACK)
  {
    log(tr("Error: IDLE command NACK"));
    return false;
  }

  // Step 2: Send START command
  log(tr("Step 2: Sending START command..."));
  response = sendAndReceive(IapProtocol::buildStartCommand(), 2000);

  if(response.isEmpty())
  {
    log(tr("Error: No response to START command"));
    return false;
  }

  log(tr("START response: %1 bytes, hex: %2").arg(response.size()).arg(response.toHex()));

  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    log(tr("Error: START command failed, result: 0x%1").arg(result, 4, 16, QChar('0')));
    return false;
  }

  log(tr("START command acknowledged"));

  // Step 3: Send firmware data
  // Device has 1KB buffer, only writes to flash and responds when buffer is full
  log(tr("Sending firmware data (%1 bytes)...").arg(m_firmwareData.size()));

  const int BUFFER_SIZE = 1024;  // 1KB buffer on device
  const int CHUNK_SIZE = 60;     // Max data payload per packet (64 - 4 header bytes)

  int totalBytes = m_firmwareData.size();
  bool ok;
  uint32_t currentAddr = m_addressEdit->text().toUInt(&ok, 16);
  if(!ok)
  {
    log(tr("Invalid flash address format, using default 0x08005000"));
    currentAddr = IapProtocol::FLASH_APP_ADDRESS;
  }
  int bytesSent = 0;
  int bufferOffset = 0;  // Offset within current 1KB buffer

  while(bytesSent < totalBytes)
  {
    // Send address command at the start of each 1KB sector
    if(bufferOffset == 0)
    {
      log(tr("Setting address: 0x%1").arg(currentAddr, 8, 16, QChar('0')));
      if(!sendAddress(currentAddr))
      {
        log(tr("Error: Failed to send address command"));
        return false;
      }
    }

    // Calculate how many bytes to send in this chunk
    int remainingInBuffer = BUFFER_SIZE - bufferOffset;
    int remainingInFile = totalBytes - bytesSent;
    int chunkSize = qMin(CHUNK_SIZE, qMin(remainingInBuffer, remainingInFile));

    // Prepare and send data chunk (without waiting for response)
    QByteArray chunk = m_firmwareData.mid(bytesSent, chunkSize);
    QByteArray dataCmd = IapProtocol::buildDataCommand(chunk);

    if(!sendCommand(dataCmd))
    {
      log(tr("Error: Failed to send data at offset %1").arg(bytesSent));
      return false;
    }

    bytesSent += chunkSize;
    bufferOffset += chunkSize;

    // When 1KB buffer is full, wait for ACK from device
    if(bufferOffset >= BUFFER_SIZE)
    {
      log(tr("Waiting for device ACK after %1 bytes...").arg(bytesSent));
      response = m_hidDevice->read(5000);  // 5 second timeout for flash write

      if(response.isEmpty())
      {
        log(tr("Error: No response from device after sending %1 bytes").arg(bytesSent));
        return false;
      }

      if(!IapProtocol::parseResponse(response, cmd, result))
      {
        log(tr("Error: Invalid response format"));
        return false;
      }

      if(result == IapProtocol::NACK)
      {
        log(tr("Error: Device rejected data at offset %1").arg(bytesSent));
        return false;
      }

      if(result == IapProtocol::ACK)
      {
        log(tr("Device ACK received, %1 bytes written to flash").arg(bytesSent));
      }

      // Move to next sector
      currentAddr += BUFFER_SIZE;
      bufferOffset = 0;
    }

    // Handle remaining data at end of file (pad to 1KB)
    if(bytesSent >= totalBytes && bufferOffset > 0)
    {
      int paddingNeeded = BUFFER_SIZE - bufferOffset;
      log(tr("Padding %1 bytes with 0xFF to complete 1KB buffer").arg(paddingNeeded));

      // Send padding data in chunks
      while(paddingNeeded > 0)
      {
        int padChunkSize = qMin(CHUNK_SIZE, paddingNeeded);
        QByteArray padding(padChunkSize, static_cast<char>(0xFF));
        QByteArray dataCmd = IapProtocol::buildDataCommand(padding);

        if(!sendCommand(dataCmd))
        {
          log(tr("Error: Failed to send padding data"));
          return false;
        }
        paddingNeeded -= padChunkSize;
      }

      log(tr("Waiting for device ACK for final block..."));
      response = m_hidDevice->read(5000);

      if(response.isEmpty())
      {
        log(tr("Error: No response from device for final block"));
        return false;
      }

      if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
      {
        log(tr("Error: Final block write failed"));
        return false;
      }

      log(tr("Final block written successfully"));
      bufferOffset = 0;
    }

    // Update progress
    int progress = (bytesSent * 100) / totalBytes;
    m_progressBar->setValue(progress);
    QApplication::processEvents();
  }

  // Step 4: Send FINISH command
  log(tr("Sending FINISH command..."));
  response = sendAndReceive(IapProtocol::buildFinishCommand());
  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    log(tr("Error: FINISH command failed"));
    return false;
  }

  log(tr("Firmware upgrade completed!"));

  // Step 5: Automatically jump to application
  log(tr("Sending JUMP command to start application..."));
  response = sendAndReceive(IapProtocol::buildJumpCommand(), 2000);

  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    log(tr("Warning: JUMP command failed, please manually reset the device"));
  }
  else
  {
    log(tr("Device will restart and run the new firmware..."));
    // Device will disconnect after jump
    QTimer::singleShot(500, [this]()
    {
      if(m_hidDevice->isOpen())
      {
        m_hidDevice->close();
        m_statusLabel->setText(tr("Disconnected"));
        m_statusIndicator->setStyleSheet("QLabel { background-color: #ef4444; border-radius: 6px; }");
        m_statusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; font-size: 14px; }");
      }
    });
  }

  return true;
}

bool MainWindow::sendAddress(uint32_t address)
{
  QByteArray response = sendAndReceive(IapProtocol::buildAddressCommand(address));

  IapProtocol::Command cmd;
  IapProtocol::Response result;

  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    return false;
  }

  return true;
}
void MainWindow::onDataReceived(const QByteArray & data)
{
  // Data received asynchronously - for future use
}

void MainWindow::onErrorOccurred(const QString & error)
{
  log(tr("Error: %1").arg(error));
}

void MainWindow::onDeviceConnected()
{
  updateStatus(tr("Connected"));
}

void MainWindow::onDeviceDisconnected()
{
  updateStatus(tr("Disconnected"));
}

void CustomTextEdit::contextMenuEvent(QContextMenuEvent * event)
{
  QMenu * menu = createStandardContextMenu();

  // Get standard menu actions and set Chinese text
  QList<QAction *> actions = menu->actions();
  for(QAction * action : actions)
  {
    if(action->text() == "&Undo")
    {
      action->setText(tr("Undo"));
    }
    else if(action->text() == "&Redo")
    {
      action->setText(tr("Redo"));
    }
    else if(action->text() == "Cu&t")
    {
      action->setText(tr("Cut"));
    }
    else if(action->text() == "&Copy")
    {
      action->setText(tr("Copy"));
    }
    else if(action->text() == "&Paste")
    {
      action->setText(tr("Paste"));
    }
    else if(action->text() == "Delete")
    {
      action->setText(tr("Delete"));
    }
    else if(action->text() == "Select All")
    {
      action->setText(tr("Select All"));
    }
  }

  menu->exec(event->globalPos());
  delete menu;
}

void CustomLineEdit::contextMenuEvent(QContextMenuEvent * event)
{
  QMenu * menu = createStandardContextMenu();

  // Get standard menu actions and set Chinese text
  QList<QAction *> actions = menu->actions();
  for(QAction * action : actions)
  {
    if(action->text() == "&Undo")
    {
      action->setText(tr("Undo"));
    }
    else if(action->text() == "&Redo")
    {
      action->setText(tr("Redo"));
    }
    else if(action->text() == "Cu&t")
    {
      action->setText(tr("Cut"));
    }
    else if(action->text() == "&Copy")
    {
      action->setText(tr("Copy"));
    }
    else if(action->text() == "&Paste")
    {
      action->setText(tr("Paste"));
    }
    else if(action->text() == "Delete")
    {
      action->setText(tr("Delete"));
    }
    else if(action->text() == "Select All")
    {
      action->setText(tr("Select All"));
    }
  }

  menu->exec(event->globalPos());
  delete menu;
}
