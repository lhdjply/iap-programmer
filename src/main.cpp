#include <QApplication>
#include <QStyleFactory>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QDebug>
#include "mainwindow.h"
#include "cli_programmer.h"
#include <QTranslator>

using namespace Qt::Literals::StringLiterals;

void printUsage()
{
  qInfo() << PROJECT_TARGET;
  qInfo() << "Usage:";
  qInfo() << "  iap_programmer                    - Launch GUI mode";
  qInfo() << "  iap_programmer -i <file>          - Program file (bin/hex)";
  qInfo() << "  iap_programmer -i <file> -d <addr> - Program bin file with address";
  qInfo() << "";
  qInfo() << "Options:";
  qInfo() << "  -i <file>     Input firmware file (.bin or .hex)";
  qInfo() << "  -d <address>  Flash address in hex (e.g., 0x08005000)";
  qInfo() << "                Required for .bin files, optional for .hex files";
  qInfo() << "  -vid <id>     Vendor ID in hex (e.g., 0x2E3C)";
  qInfo() << "  -pid <id>     Product ID in hex (e.g., 0xAF01)";
  qInfo() << "  -v, --version Show version information";
  qInfo() << "  -h, --help    Show this help message";
}

void printVersion()
{
  qInfo() << PROJECT_TARGET << PROJECT_VERSION;
}

int main(int argc, char * argv[])
{
  bool useGui = true;
  QString inputFile;
  uint32_t address = 0;
  bool addressProvided = false;
  unsigned short vid = 0x2E3C;
  unsigned short pid = 0xAF01;

  for(int i = 1; i < argc; i++)
  {
    QString arg = QString::fromLocal8Bit(argv[i]);

    if(arg == "-h" || arg == "--help")
    {
      printUsage();
      return 0;
    }
    else if(arg == "-v" || arg == "--version")
    {
      printVersion();
      return 0;
    }
    else if(arg == "-i" && i + 1 < argc)
    {
      useGui = false;
      inputFile = QString::fromLocal8Bit(argv[++i]);
    }
    else if(arg == "-d" && i + 1 < argc)
    {
      bool ok;
      QString addrStr = QString::fromLocal8Bit(argv[++i]);
      if(addrStr.startsWith("0x", Qt::CaseInsensitive))
      {
        address = addrStr.toUInt(&ok, 16);
      }
      else
      {
        address = addrStr.toUInt(&ok, 0);
      }
      if(!ok)
      {
        qCritical() << "Error: Invalid address format:" << addrStr;
        return 1;
      }
      addressProvided = true;
    }
    else if(arg == "-vid" && i + 1 < argc)
    {
      bool ok;
      QString vidStr = QString::fromLocal8Bit(argv[++i]);
      if(vidStr.startsWith("0x", Qt::CaseInsensitive))
      {
        vid = vidStr.toUShort(&ok, 16);
      }
      else
      {
        vid = vidStr.toUShort(&ok, 0);
      }
      if(!ok)
      {
        qCritical() << "Error: Invalid VID format:" << vidStr;
        return 1;
      }
    }
    else if(arg == "-pid" && i + 1 < argc)
    {
      bool ok;
      QString pidStr = QString::fromLocal8Bit(argv[++i]);
      if(pidStr.startsWith("0x", Qt::CaseInsensitive))
      {
        pid = pidStr.toUShort(&ok, 16);
      }
      else
      {
        pid = pidStr.toUShort(&ok, 0);
      }
      if(!ok)
      {
        qCritical() << "Error: Invalid PID format:" << pidStr;
        return 1;
      }
    }
    else if(arg.startsWith("-"))
    {
      qCritical() << "Error: Unknown option:" << arg;
      printUsage();
      return 1;
    }
  }

  QString version = PROJECT_VERSION;

  if(useGui)
  {
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
#ifdef Q_OS_WIN
    app.setWindowIcon(QIcon(":/icons/windows/com-master.ico"));
#else
    app.setWindowIcon(QIcon(":/icons/linux/hicolor/scalable/apps/iap-programmer.svg"));
#endif

    app.setApplicationName(PROJECT_TARGET);
    app.setApplicationVersion(version);

    // Translation setup
    QTranslator translator;
#if defined(TRANSLATION_RESOURCE_EMBEDDING)
    const QString qmDir = u":/i18n/"_s;
#elif defined(QM_FILE_INSTALL_ABSOLUTE_DIR)
    const QString qmDir = QT_STRINGIFY(QM_FILE_INSTALL_ABSOLUTE_DIR);
#else
    const QString qmDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("translations");
#endif
    if(translator.load(QLocale(), PROJECT_TARGET, u"_"_s, qmDir))
    {
      QCoreApplication::installTranslator(&translator);
    }

    MainWindow window;
    window.show();

    return app.exec();
  }
  else
  {
    if(inputFile.isEmpty())
    {
      qCritical() << "Error: No input file specified";
      printUsage();
      return 1;
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName(PROJECT_TARGET);
    app.setApplicationVersion(version);

    CliProgrammer programmer;
    bool success = programmer.program(inputFile, address, addressProvided, vid, pid);

    return success ? 0 : 1;
  }
}
