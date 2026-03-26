#include "cli_programmer.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>

static QTextStream _qStdoutStream(stdout);
#define qStdout() _qStdoutStream

CliProgrammer::CliProgrammer(QObject * parent)
  : QObject(parent)
  , m_hidDevice(new HidDevice(this))
  , m_vendorId(0x2E3C)
  , m_productId(0xAF01)
{
}

CliProgrammer::~CliProgrammer()
{
  if(m_hidDevice->isOpen())
  {
    m_hidDevice->close();
  }
}

bool CliProgrammer::program(const QString & filePath, uint32_t address, bool addressProvided, unsigned short vid,
                            unsigned short pid)
{
  m_vendorId = vid;
  m_productId = pid;
  uint32_t fileAddress = 0;
  bool addressFromHex = false;

  if(!loadFirmwareFile(filePath, fileAddress, addressFromHex))
  {
    qCritical() << "Failed to load firmware file:" << filePath;
    return false;
  }

  if(m_firmwareData.isEmpty())
  {
    qCritical() << "Firmware file is empty";
    return false;
  }

  uint32_t finalAddress;
  if(filePath.endsWith(".hex", Qt::CaseInsensitive))
  {
    if(addressProvided)
    {
      qWarning() << "Hex file contains address information, ignoring -d parameter";
    }
    finalAddress = fileAddress;
    qInfo() << "Using address from hex file: 0x" << QString("%1").arg(finalAddress, 8, 16, QChar('0'));
  }
  else
  {
    if(addressProvided)
    {
      finalAddress = address;
    }
    else
    {
      finalAddress = IapProtocol::FLASH_APP_ADDRESS;
      qInfo() << "Using default address: 0x" << QString("%1").arg(finalAddress, 8, 16, QChar('0'));
    }
  }

  qInfo() << "Connecting to device (VID: 0x" << QString("%1").arg(m_vendorId, 4, 16, QChar('0'))
          << ", PID: 0x" << QString("%1").arg(m_productId, 4, 16, QChar('0')) << ")...";

  if(!m_hidDevice->open(m_vendorId, m_productId))
  {
    qCritical() << "Failed to connect:" << HidDevice::errorString();
    qCritical() << "Please ensure:";
    qCritical() << "  1. The device is connected via USB";
    qCritical() << "  2. You have proper USB permissions";
    return false;
  }

  qInfo() << "Device connected successfully";

  // Try to get device info and check if in IAP mode
  QByteArray response = sendAndReceive(IapProtocol::buildGetCommand(), 1000);
  if(!response.isEmpty())
  {
    IapProtocol::Command cmd;
    IapProtocol::Response result;
    if(IapProtocol::parseResponse(response, cmd, result) && result == IapProtocol::ACK)
    {
      uint32_t appAddr = IapProtocol::parseGetResponse(response);
      if(appAddr == 0)
      {
        qInfo() << "Application address: 0x00000000 (no app programmed or using default)";
      }
      else
      {
        qInfo() << "Application address: 0x" << QString("%1").arg(appAddr, 8, 16, QChar('0'));
      }

      response = sendAndReceive(IapProtocol::buildIdleCommand(), 1000);
      if(IapProtocol::parseResponse(response, cmd, result) && result == IapProtocol::ACK)
      {
        qInfo() << "Device is in IAP mode (bootloader), ready for upgrade";
      }
      else
      {
        qInfo() << "Device may be in application mode, will trigger IAP mode on upgrade";
      }
    }
  }

  QThread::msleep(1000);
  m_hidDevice->close();
  QThread::msleep(500);
  if(!m_hidDevice->open(m_vendorId, m_productId))
  {
    qCritical() << "Failed to reopen device:" << HidDevice::errorString();
    return false;
  }
  qInfo() << "Device reconnected";

  bool success = startIap(finalAddress);

  if(m_hidDevice->isOpen())
  {
    m_hidDevice->close();
  }

  return success;
}

bool CliProgrammer::loadFirmwareFile(const QString & filePath, uint32_t & outAddress, bool & outAddressFromHex)
{
  QFile file(filePath);
  if(!file.open(QIODevice::ReadOnly))
  {
    qCritical() << "Failed to open file:" << file.errorString();
    return false;
  }

  outAddressFromHex = false;

  if(filePath.endsWith(".hex", Qt::CaseInsensitive))
  {
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

      bool ok;
      int byteCount = line.mid(1, 2).toInt(&ok, 16);
      if(!ok || byteCount == 0) continue;

      int address = line.mid(3, 4).toInt(&ok, 16);
      if(!ok) continue;

      int recordType = line.mid(7, 2).toInt(&ok, 16);
      if(!ok) continue;

      switch(recordType)
      {
        case 0x00:
          {
            uint32_t absoluteAddress = baseAddress + address;
            if(startAddress == 0xFFFFFFFF)
            {
              startAddress = absoluteAddress;
            }

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
        case 0x04:
          {
            if(byteCount == 2)
            {
              QString addressStr = line.mid(9, 4);
              baseAddress = addressStr.toInt(&ok, 16) << 16;
            }
            break;
          }
        case 0x01:
          goto endOfFile;
      }
    }

endOfFile:
    file.close();

    if(startAddress != 0xFFFFFFFF)
    {
      outAddress = startAddress;
      outAddressFromHex = true;
      qInfo() << "Loaded hex firmware:" << m_firmwareData.size() << "bytes, start address: 0x"
              << QString("%1").arg(startAddress, 8, 16, QChar('0'));
      return true;
    }
    else
    {
      qCritical() << "Failed to parse hex file";
      return false;
    }
  }
  else
  {
    m_firmwareData = file.readAll();
    file.close();
    qInfo() << "Loaded binary firmware:" << m_firmwareData.size() << "bytes";
    return true;
  }
}

bool CliProgrammer::sendCommand(const QByteArray & cmd)
{
  if(!m_hidDevice->isOpen())
  {
    qCritical() << "Device not connected";
    return false;
  }

  int result = m_hidDevice->write(cmd);
  if(result < 0)
  {
    qWarning() << "Write failed, attempting to reconnect...";
    m_hidDevice->close();
    if(m_hidDevice->open(m_vendorId, m_productId))
    {
      result = m_hidDevice->write(cmd);
    }
  }
  return result > 0;
}

QByteArray CliProgrammer::sendAndReceive(const QByteArray & cmd, int timeoutMs)
{
  if(!sendCommand(cmd))
  {
    return QByteArray();
  }
  return m_hidDevice->read(timeoutMs);
}

bool CliProgrammer::startIap(uint32_t address)
{
  qInfo() << "Starting IAP upgrade process...";

  IapProtocol::Command cmd;
  IapProtocol::Response result;

  qInfo() << "Step 1: Sending IDLE command...";
  QByteArray response = sendAndReceive(IapProtocol::buildIdleCommand(), 2000);
  if(response.isEmpty())
  {
    qCritical() << "Error: No response to IDLE command";
    qCritical() << "Tip: Make sure device is connected and in IAP mode";
    return false;
  }

  if(!IapProtocol::parseResponse(response, cmd, result))
  {
    qCritical() << "Error: Cannot parse IDLE response";
    return false;
  }

  if(result != IapProtocol::ACK)
  {
    qCritical() << "Error: IDLE command NACK";
    return false;
  }

  qInfo() << "IDLE command acknowledged";

  qInfo() << "Step 2: Sending START command...";
  response = sendAndReceive(IapProtocol::buildStartCommand(), 2000);

  if(response.isEmpty())
  {
    qCritical() << "Error: No response to START command";
    return false;
  }

  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    qCritical() << "Error: START command failed";
    return false;
  }

  qInfo() << "START command acknowledged";

  qInfo() << "Sending firmware data (" << m_firmwareData.size() << " bytes)...";

  const int BUFFER_SIZE = 1024;
  const int CHUNK_SIZE = 60;

  int totalBytes = m_firmwareData.size();
  uint32_t currentAddr = address;
  int bytesSent = 0;
  int bufferOffset = 0;

  while(bytesSent < totalBytes)
  {
    if(bufferOffset == 0)
    {
      if(!sendAddress(currentAddr))
      {
        qCritical() << "Error: Failed to send address command";
        return false;
      }
    }

    int remainingInBuffer = BUFFER_SIZE - bufferOffset;
    int remainingInFile = totalBytes - bytesSent;
    int chunkSize = qMin(CHUNK_SIZE, qMin(remainingInBuffer, remainingInFile));

    QByteArray chunk = m_firmwareData.mid(bytesSent, chunkSize);
    QByteArray dataCmd = IapProtocol::buildDataCommand(chunk);

    if(!sendCommand(dataCmd))
    {
      qCritical() << "Error: Failed to send data at offset" << bytesSent;
      return false;
    }

    bytesSent += chunkSize;
    bufferOffset += chunkSize;

    if(bufferOffset >= BUFFER_SIZE)
    {
      response = m_hidDevice->read(5000);

      if(response.isEmpty())
      {
        qCritical() << "Error: No response from device after sending" << bytesSent << "bytes";
        return false;
      }

      if(!IapProtocol::parseResponse(response, cmd, result))
      {
        qCritical() << "Error: Invalid response format";
        return false;
      }

      if(result == IapProtocol::NACK)
      {
        qCritical() << "Error: Device rejected data at offset" << bytesSent;
        return false;
      }

      if(result == IapProtocol::ACK)
      {
        int progress = (bytesSent * 100) / totalBytes;
        qStdout() << "\rProgress: " << progress << "%";
        qStdout().flush();
      }

      currentAddr += BUFFER_SIZE;
      bufferOffset = 0;
    }

    if(bytesSent >= totalBytes && bufferOffset > 0)
    {
      int paddingNeeded = BUFFER_SIZE - bufferOffset;

      while(paddingNeeded > 0)
      {
        int padChunkSize = qMin(CHUNK_SIZE, paddingNeeded);
        QByteArray padding(padChunkSize, static_cast<char>(0xFF));
        QByteArray dataCmd = IapProtocol::buildDataCommand(padding);

        if(!sendCommand(dataCmd))
        {
          qCritical() << "Error: Failed to send padding data";
          return false;
        }
        paddingNeeded -= padChunkSize;
      }

      response = m_hidDevice->read(5000);

      if(response.isEmpty())
      {
        qCritical() << "Error: No response from device for final block";
        return false;
      }

      if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
      {
        qCritical() << "Error: Final block write failed";
        return false;
      }

      bufferOffset = 0;
    }
  }

  // Print final progress at 100%
  qStdout() << "\rProgress: 100%" << Qt::endl;
  qStdout().flush();

  qInfo() << "Sending FINISH command...";
  response = sendAndReceive(IapProtocol::buildFinishCommand());
  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    qCritical() << "Error: FINISH command failed";
    return false;
  }

  qInfo() << "Firmware upgrade completed!";

  qInfo() << "Sending JUMP command to start application...";
  response = sendAndReceive(IapProtocol::buildJumpCommand(), 2000);

  if(!IapProtocol::parseResponse(response, cmd, result) || result != IapProtocol::ACK)
  {
    qWarning() << "Warning: JUMP command failed, please manually reset the device";
  }
  else
  {
    qInfo() << "Device will restart and run to new firmware...";
  }

  return true;
}

bool CliProgrammer::sendAddress(uint32_t address)
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
