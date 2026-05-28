#include <QApplication>
#include <QStyleFactory>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include "mainwindow.h"
#include <QTranslator>

using namespace Qt::Literals::StringLiterals;

int main(int argc, char * argv[])
{
  QApplication app(argc, argv);
  app.setStyle(QStyleFactory::create("Fusion"));
#ifdef Q_OS_WIN
  app.setWindowIcon(QIcon(":/icons/windows/com-master.ico"));
#else
  app.setWindowIcon(QIcon(":/icons/linux/hicolor/scalable/apps/iap-programmer.svg"));
#endif

  app.setApplicationName(PROJECT_TARGET);
  app.setApplicationVersion(PROJECT_VERSION);

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
