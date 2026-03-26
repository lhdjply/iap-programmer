#include "hiddevice.h"
#include <hidapi.h>

QString HidDevice::s_lastError;
static bool s_hidInitialized = false;

HidDevice::HidDevice(QObject * parent)
  : QObject(parent)
  , m_device(nullptr)
{
  if(!s_hidInitialized)
  {
    hid_init();
    s_hidInitialized = true;
  }
}

HidDevice::~HidDevice()
{
  close();
}

bool HidDevice::open(unsigned short vendorId, unsigned short productId)
{
  close();

  m_device = hid_open(vendorId, productId, nullptr);
  if(!m_device)
  {
    s_lastError = QString::fromWCharArray(hid_error(nullptr));
    emit errorOccurred(tr("Failed to open HID device: %1").arg(s_lastError));
    return false;
  }

  // Set non-blocking mode for read operations
  hid_set_nonblocking(m_device, 0);

  // Flush any pending data by setting non-blocking temporarily
  unsigned char tempBuffer[64];
  hid_set_nonblocking(m_device, 1);
  while(hid_read(m_device, tempBuffer, sizeof(tempBuffer)) > 0)
  {
    // Discard any pending data
  }
  hid_set_nonblocking(m_device, 0);

  return true;
}

void HidDevice::close()
{
  if(m_device)
  {
    hid_close(m_device);
    m_device = nullptr;
  }
}

bool HidDevice::isOpen() const
{
  return m_device != nullptr;
}

int HidDevice::write(const QByteArray & data)
{
  if(!m_device)
  {
    emit errorOccurred(tr("Device not open"));
    return -1;
  }

  // HID report needs a report ID prefix (0x00 for no report ID)
  QByteArray reportData;
  reportData.append(static_cast<char>(0x00)); // Report ID
  reportData.append(data);

  // Pad to 65 bytes (report ID + 64 data bytes)
  while(reportData.size() < 65)
  {
    reportData.append(static_cast<char>(0x00));
  }

  int result = hid_write(m_device, reinterpret_cast<const unsigned char *>(reportData.constData()), reportData.size());
  if(result < 0)
  {
    s_lastError = QString::fromWCharArray(hid_error(m_device));
    emit errorOccurred(tr("Write failed: %1").arg(s_lastError));
  }

  return result;
}

QByteArray HidDevice::read(int timeoutMs)
{
  if(!m_device)
  {
    emit errorOccurred(tr("Device not open"));
    return QByteArray();
  }

  unsigned char buffer[64];

  int result = hid_read_timeout(m_device, buffer, sizeof(buffer), timeoutMs);

  if(result > 0)
  {
    QByteArray data(reinterpret_cast<char *>(buffer), result);
    emit dataReceived(data);
    return data;
  }
  else if(result < 0)
  {
    s_lastError = QString::fromWCharArray(hid_error(m_device));
    emit errorOccurred(tr("Read failed: %1").arg(s_lastError));
  }

  return QByteArray();
}

std::vector<HidDevice::DeviceInfo> HidDevice::enumerateDevices()
{
  std::vector<DeviceInfo> devices;

  hid_device_info * devs = hid_enumerate(0x00, 0x00);
  hid_device_info * current = devs;

  while(current)
  {
    DeviceInfo info;
    info.vendorId = current->vendor_id;
    info.productId = current->product_id;
    info.manufacturer = QString::fromWCharArray(current->manufacturer_string);
    info.product = QString::fromWCharArray(current->product_string);
    info.serialNumber = QString::fromWCharArray(current->serial_number);
    info.path = QString::fromUtf8(current->path);

    devices.push_back(info);
    current = current->next;
  }

  hid_free_enumeration(devs);
  return devices;
}

QString HidDevice::errorString()
{
  return s_lastError;
}
