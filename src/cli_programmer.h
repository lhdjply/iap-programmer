#ifndef CLI_PROGRAMMER_H
#define CLI_PROGRAMMER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include "hiddevice.h"
#include "iaprotocol.h"

class CliProgrammer : public QObject
{
    Q_OBJECT

  public:
    explicit CliProgrammer(QObject * parent = nullptr);
    ~CliProgrammer();

    bool program(const QString & filePath, uint32_t address, bool addressProvided, unsigned short vid = 0x2E3C,
                 unsigned short pid = 0xAF01);

  private:
    bool loadFirmwareFile(const QString & filePath, uint32_t & outAddress, bool & outAddressFromHex);
    bool sendCommand(const QByteArray & cmd);
    QByteArray sendAndReceive(const QByteArray & cmd, int timeoutMs = 2000);
    bool startIap(uint32_t address);
    bool sendAddress(uint32_t address);

    HidDevice * m_hidDevice;
    QByteArray m_firmwareData;

    unsigned short m_vendorId;
    unsigned short m_productId;
};

#endif // CLI_PROGRAMMER_H
