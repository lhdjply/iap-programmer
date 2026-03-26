#ifndef IAPROTOCOL_H
#define IAPROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <cstdint>

class IapProtocol : public QObject
{
    Q_OBJECT

  public:
    // IAP Commands
    enum Command : uint16_t
    {
      CMD_IDLE   = 0x5AA0,
      CMD_START  = 0x5AA1,
      CMD_ADDR   = 0x5AA2,
      CMD_DATA   = 0x5AA3,
      CMD_FINISH = 0x5AA4,
      CMD_CRC    = 0x5AA5,
      CMD_JMP    = 0x5AA6,
      CMD_GET    = 0x5AA7
    };

    // Response codes
    enum Response : uint16_t
    {
      ACK  = 0xFF00,
      NACK = 0x00FF
    };

    explicit IapProtocol(QObject * parent = nullptr);

    // Build command packets
    static QByteArray buildIdleCommand();
    static QByteArray buildStartCommand();
    static QByteArray buildAddressCommand(uint32_t address);
    static QByteArray buildDataCommand(const QByteArray & data);
    static QByteArray buildFinishCommand();
    static QByteArray buildCrcCommand(uint32_t address, uint16_t nkBytes);
    static QByteArray buildJumpCommand();
    static QByteArray buildGetCommand();

    // Parse response
    static bool parseResponse(const QByteArray & response, Command & cmd, Response & result);
    static uint32_t parseGetResponse(const QByteArray & response);
    static uint32_t parseCrcResponse(const QByteArray & response);

    // Flash parameters
    static constexpr uint32_t FLASH_APP_ADDRESS = 0x08005000;
    static constexpr uint32_t FLASH_BASE = 0x08000000;
    static constexpr uint16_t MAX_PACKET_SIZE = 64;
    static constexpr uint16_t DATA_PAYLOAD_SIZE = 60;  // 64 - 4 bytes header

    // Calculate CRC32
    static uint32_t calculateCrc32(const QByteArray & data);
};

#endif // IAPROTOCOL_H
