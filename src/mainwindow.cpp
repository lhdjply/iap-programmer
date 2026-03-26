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
  QWidget * centralWidget = new QWidget(this);
  QVBoxLayout * mainLayout = new QVBoxLayout(centralWidget);

  // Status bar
  QHBoxLayout * statusLayout = new QHBoxLayout();
  m_statusLabel = new QLabel(tr("Disconnected"), this);
  m_statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
  statusLayout->addWidget(new QLabel(tr("Status:"), this));
  statusLayout->addWidget(m_statusLabel);
  statusLayout->addStretch();
  mainLayout->addLayout(statusLayout);

  // File selection
  QHBoxLayout * fileLayout = new QHBoxLayout();
  m_filePathEdit = new QLineEdit(this);
  m_filePathEdit->setReadOnly(true);
  m_filePathEdit->setPlaceholderText(tr("Select firmware file (.bin)"));
  m_selectFileBtn = new QPushButton(tr("Select File"), this);
  fileLayout->addWidget(m_filePathEdit);
  fileLayout->addWidget(m_selectFileBtn);
  mainLayout->addLayout(fileLayout);

  // VID/PID input
  QHBoxLayout * deviceIdLayout = new QHBoxLayout();
  deviceIdLayout->addWidget(new QLabel(tr("VID:"), this));
  m_vidEdit = new QLineEdit(this);
  m_vidEdit->setText("0x2E3C");
  m_vidEdit->setPlaceholderText(tr("Enter vendor ID (hex)"));
  m_vidEdit->setFixedWidth(100);
  deviceIdLayout->addWidget(m_vidEdit);

  deviceIdLayout->addSpacing(20);

  deviceIdLayout->addWidget(new QLabel(tr("PID:"), this));
  m_pidEdit = new QLineEdit(this);
  m_pidEdit->setText("0xAF01");
  m_pidEdit->setPlaceholderText(tr("Enter product ID (hex)"));
  m_pidEdit->setFixedWidth(100);
  deviceIdLayout->addWidget(m_pidEdit);

  deviceIdLayout->addStretch();
  mainLayout->addLayout(deviceIdLayout);

  // Flash address input
  QHBoxLayout * addressLayout = new QHBoxLayout();
  addressLayout->addWidget(new QLabel(tr("Flash Address:"), this));
  m_addressEdit = new QLineEdit(this);
  m_addressEdit->setText("0x08005000");
  m_addressEdit->setPlaceholderText(tr("Enter flash address (hex)"));
  m_addressEdit->setEnabled(false);
  addressLayout->addWidget(m_addressEdit);
  mainLayout->addLayout(addressLayout);

  // Progress bar
  m_progressBar = new QProgressBar(this);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFormat("%p%");
  m_progressBar->setValue(0);
  mainLayout->addWidget(m_progressBar);

  // Action buttons
  QHBoxLayout * actionLayout = new QHBoxLayout();
  m_startBtn = new QPushButton(tr("Start Upgrade"), this);
  m_startBtn->setMinimumWidth(150);
  m_startBtn->setEnabled(false);
  actionLayout->addWidget(m_startBtn);
  actionLayout->addStretch();
  mainLayout->addLayout(actionLayout);

  // Log area
  m_logEdit = new QTextEdit(this);
  m_logEdit->setReadOnly(true);
  m_logEdit->setMinimumHeight(200);
  mainLayout->addWidget(m_logEdit);

  setCentralWidget(centralWidget);
  setWindowTitle(tr(PROJECT_TARGET));
  setMinimumSize(500, 400);

  // Connect signals
  connect(m_selectFileBtn, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);
  connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartUpgradeClicked);
}

void MainWindow::log(const QString & message)
{
  QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
  m_logEdit->append(timestamp + message);
}

void MainWindow::updateStatus(const QString & status)
{
  m_statusLabel->setText(status);
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
    m_statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");

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
        m_statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
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
  m_statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
}

void MainWindow::onDeviceDisconnected()
{
  updateStatus(tr("Disconnected"));
  m_statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
}
