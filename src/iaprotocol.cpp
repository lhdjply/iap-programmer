#include "iaprotocol.h"

IapProtocol::IapProtocol(QObject * parent)
  : QObject(parent)
{
}

QByteArray IapProtocol::buildIdleCommand()
{
  QByteArray packet;
  packet.resize(2);
  packet[0] = static_cast<char>((CMD_IDLE >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_IDLE & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildStartCommand()
{
  QByteArray packet;
  packet.resize(2);
  packet[0] = static_cast<char>((CMD_START >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_START & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildAddressCommand(uint32_t address)
{
  QByteArray packet;
  packet.resize(6);
  packet[0] = static_cast<char>((CMD_ADDR >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_ADDR & 0xFF);
  packet[2] = static_cast<char>((address >> 24) & 0xFF);
  packet[3] = static_cast<char>((address >> 16) & 0xFF);
  packet[4] = static_cast<char>((address >> 8) & 0xFF);
  packet[5] = static_cast<char>(address & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildDataCommand(const QByteArray & data)
{
  QByteArray packet;
  uint16_t dataLen = static_cast<uint16_t>(data.size());

  packet.resize(4 + dataLen);
  packet[0] = static_cast<char>((CMD_DATA >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_DATA & 0xFF);
  packet[2] = static_cast<char>((dataLen >> 8) & 0xFF);
  packet[3] = static_cast<char>(dataLen & 0xFF);

  for(int i = 0; i < data.size(); ++i)
  {
    packet[4 + i] = data[i];
  }

  return packet;
}

QByteArray IapProtocol::buildFinishCommand()
{
  QByteArray packet;
  packet.resize(2);
  packet[0] = static_cast<char>((CMD_FINISH >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_FINISH & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildCrcCommand(uint32_t address, uint16_t nkBytes)
{
  QByteArray packet;
  packet.resize(8);
  packet[0] = static_cast<char>((CMD_CRC >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_CRC & 0xFF);
  packet[2] = static_cast<char>((address >> 24) & 0xFF);
  packet[3] = static_cast<char>((address >> 16) & 0xFF);
  packet[4] = static_cast<char>((address >> 8) & 0xFF);
  packet[5] = static_cast<char>(address & 0xFF);
  packet[6] = static_cast<char>((nkBytes >> 8) & 0xFF);
  packet[7] = static_cast<char>(nkBytes & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildJumpCommand()
{
  QByteArray packet;
  packet.resize(2);
  packet[0] = static_cast<char>((CMD_JMP >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_JMP & 0xFF);
  return packet;
}

QByteArray IapProtocol::buildGetCommand()
{
  QByteArray packet;
  packet.resize(2);
  packet[0] = static_cast<char>((CMD_GET >> 8) & 0xFF);
  packet[1] = static_cast<char>(CMD_GET & 0xFF);
  return packet;
}

bool IapProtocol::parseResponse(const QByteArray & response, Command & cmd, Response & result)
{
  if(response.size() < 4)
  {
    return false;
  }

  cmd = static_cast<Command>((static_cast<uint8_t>(response[0]) << 8) |
                             static_cast<uint8_t>(response[1]));
  result = static_cast<Response>((static_cast<uint8_t>(response[2]) << 8) |
                                 static_cast<uint8_t>(response[3]));

  return true;
}

uint32_t IapProtocol::parseGetResponse(const QByteArray & response)
{
  if(response.size() < 8)
  {
    return 0;
  }

  return (static_cast<uint32_t>(static_cast<uint8_t>(response[4])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(response[5])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(response[6])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(response[7]));
}

uint32_t IapProtocol::parseCrcResponse(const QByteArray & response)
{
  if(response.size() < 8)
  {
    return 0;
  }

  return (static_cast<uint32_t>(static_cast<uint8_t>(response[4])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(response[5])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(response[6])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(response[7]));
}

uint32_t IapProtocol::calculateCrc32(const QByteArray & data)
{
  static const uint32_t crcTable[256] =
  {
    0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B,
    0x1A864DB2, 0x1E475005, 0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61,
    0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD, 0x4C11DB70, 0x48D0C6C7,
    0x4593E01E, 0x4152FDA9, 0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75,
    0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x675A1011, 0x791D4014, 0x7DDC5DA3,
    0x709F7B7A, 0x745E66CD, 0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039,
    0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5, 0xBE2B5B58, 0xBAEA46EF,
    0xB7A96036, 0xB3687D81, 0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
    0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49, 0xC7361B4C, 0xC3F706FB,
    0xCEB42022, 0xCA753D95, 0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1,
    0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D, 0x34867077, 0x30476DC0,
    0x3D044B19, 0x39C556AE, 0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072,
    0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16, 0x018A13FD, 0x054BF6D8,
    0x0808D07D, 0x0CC9CDCA, 0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE,
    0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02, 0x5E9F46BF, 0x5A5E5B08,
    0x571D7DD1, 0x53DC6066, 0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
    0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E, 0xBBA1F0CC, 0xBF60A65B,
    0xB823B79E, 0xBCE29829, 0x8A89C6D0, 0x8E487F67, 0x8306CDC9, 0x87C7C75E,
    0xD2A609A0, 0xD667E91D, 0xDB240BAD, 0xDFE50BD1, 0xCD1E12F0, 0xC9DDEE47,
    0xC49EC7F4, 0xC05F6A43, 0xF581B1DE, 0xF140B469, 0x6E2669AF, 0x6AE74058,
    0x67840BD9, 0x6345C5D1, 0x6E0669AF, 0x6AC74058, 0x7D40BD9, 0x6345C5D1,
    0x56B9C1CD, 0x5278F8D8, 0x5FBB0B73, 0x5B7AC5D1, 0x44D9B1CD, 0x4018F8D8,
    0x4DBB0B73, 0x497AC5D1, 0x32D9B1CD, 0x3618F8D8, 0x3BBB0B73, 0x3F7AC5D1,
    0x20D9B1CD, 0x2418F8D8, 0x29BB0B73, 0x2D7AC5D1, 0x96D9B1CD, 0x9218F8D8,
    0x9FBB0B73, 0x9B7AC5D1, 0x84D9B1CD, 0x8018F8D8, 0x8DBB0B73, 0x897AC5D1,
    0xA6D9B1CD, 0xA218F8D8, 0xAFBB0B73, 0xAB7AC5D1, 0xB4D9B1CD, 0xB018F8D8,
    0xBDBB0B73, 0xB97AC5D1, 0xC4D9B1CD, 0xC018F8D8, 0xCDBB0B73, 0xC97AC5D1,
    0xD6D9B1CD, 0xD218F8D8, 0xDFBB0B73, 0xDB7AC5D1, 0xE4D9B1CD, 0xE018F8D8,
    0xEDBB0B73, 0xE97AC5D1, 0xF4D9B1CD, 0xF018F8D8, 0xFDBB0B73, 0xF97AC5D1,
    0x06D9B1CD, 0x0218F8D8, 0x0FBB0B73, 0x0B7AC5D1, 0x14D9B1CD, 0x1018F8D8,
    0x1DBB0B73, 0x197AC5D1, 0x24D9B1CD, 0x2018F8D8, 0x2DBB0B73, 0x297AC5D1,
    0x34D9B1CD, 0x3018F8D8, 0x3DBB0B73, 0x397AC5D1, 0x44D9B1CD, 0x4018F8D8,
    0x4DBB0B73, 0x497AC5D1, 0x54D9B1CD, 0x5018F8D8, 0x5DBB0B73, 0x597AC5D1,
    0x64D9B1CD, 0x6018F8D8, 0x6DBB0B73, 0x697AC5D1, 0x74D9B1CD, 0x7018F8D8,
    0x7DBB0B73, 0x797AC5D1, 0x86D9B1CD, 0x8218F8D8, 0x8FBB0B73, 0x8B7AC5D1,
    0x94D9B1CD, 0x9018F8D8, 0x9DBB0B73, 0x997AC5D1, 0xA4D9B1CD, 0xA018F8D8,
    0xADBB0B73, 0xA97AC5D1, 0xB4D9B1CD, 0xB018F8D8, 0xBDBB0B73, 0xB97AC5D1,
    0xC4D9B1CD, 0xC018F8D8, 0xCDBB0B73, 0xC97AC5D1, 0xD4D9B1CD, 0xD018F8D8,
    0xDBBB0B73, 0xDB7AC5D1, 0xE4D9B1CD, 0xE018F8D8, 0xEDBB0B73, 0xE97AC5D1,
    0xF4D9B1CD, 0xF018F8D8, 0xFDBB0B73, 0xF97AC5D1
  };

  uint32_t crc = 0xFFFFFFFF;
  const uint8_t * dataPtr = reinterpret_cast<const uint8_t *>(data.constData());

  for(int i = 0; i < data.size(); ++i)
  {
    uint8_t index = static_cast<uint8_t>((crc >> 24) ^ dataPtr[i]);
    crc = (crc << 8) ^ crcTable[index];
  }

  return crc;
}
