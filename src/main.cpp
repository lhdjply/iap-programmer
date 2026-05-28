#include <QApplication>
#include <QStyleFactory>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QMainWindow>
#include <QStackedWidget>
#include "homepage.h"
#include "upgradepage.h"
#include "artinchippage.h"
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

  QMainWindow window;
  window.setStyleSheet("QMainWindow { background-color: #f5f7fa; }");
  window.setWindowTitle(QObject::tr("IAP Programmer"));
  window.setMinimumSize(700, 550);
  window.resize(900, 650);

  QStackedWidget * stack = new QStackedWidget(&window);

  HomePage * homePage = new HomePage(&window);
  UpgradePage * upgradePage = new UpgradePage(&window);
  ArtInChipPage * artInChipPage = new ArtInChipPage(&window);

  stack->addWidget(homePage);     // 0
  stack->addWidget(upgradePage);  // 1
  stack->addWidget(artInChipPage); // 2
  stack->setCurrentIndex(0);

  QObject::connect(homePage, &HomePage::enterArtInChipClicked, [stack]()
  {
    stack->setCurrentIndex(2);
  });
  QObject::connect(artInChipPage, &ArtInChipPage::backToHomeClicked, [stack]()
  {
    stack->setCurrentIndex(0);
  });
  QObject::connect(homePage, &HomePage::enterUpgradeClicked, [stack]()
  {
    stack->setCurrentIndex(1);
  });
  QObject::connect(upgradePage, &UpgradePage::backToHomeClicked, [stack]()
  {
    stack->setCurrentIndex(0);
  });

  window.setCentralWidget(stack);
  window.show();

  return app.exec();
}
