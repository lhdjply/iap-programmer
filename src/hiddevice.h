#ifndef HIDDEVICE_H
#define HIDDEVICE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <vector>

struct hid_device_;
typedef struct hid_device_ hid_device;

class HidDevice : public QObject
{
    Q_OBJECT

  public:
    explicit HidDevice(QObject * parent = nullptr);
    ~HidDevice();

    struct DeviceInfo
    {
      unsigned short vendorId;
      unsigned short productId;
      QString manufacturer;
      QString product;
      QString serialNumber;
      QString path;
    };

    bool open(unsigned short vendorId, unsigned short productId);
    void close();
    bool isOpen() const;

    int write(const QByteArray & data);
    QByteArray read(int timeoutMs = 1000);

    static std::vector<DeviceInfo> enumerateDevices();
    static QString errorString();

  signals:
    void dataReceived(const QByteArray & data);
    void errorOccurred(const QString & error);

  private:
    hid_device * m_device;
    static QString s_lastError;
};

#endif // HIDDEVICE_H
