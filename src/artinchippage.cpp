#include "artinchippage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QGraphicsDropShadowEffect>

extern "C" {
#include "upgcmd/upgcmd.h"
}

static void outputCallback(const char * msg, void * userdata)
{
  ArtInChipPage * page = static_cast<ArtInChipPage *>(userdata);
  QMetaObject::invokeMethod(page, "appendLog", Qt::QueuedConnection,
                            Q_ARG(QString, QString::fromUtf8(msg)));
}

static void progressCallback(int current, int total, void * userdata)
{
  ArtInChipPage * page = static_cast<ArtInChipPage *>(userdata);
  QMetaObject::invokeMethod(page, "updateProgress", Qt::QueuedConnection,
                            Q_ARG(int, current), Q_ARG(int, total));
}

ArtInChipPage::ArtInChipPage(QWidget * parent)
  : QWidget(parent)
  , m_workerThread(nullptr)
{
  libaicupg_init();
  setupUi();
}

ArtInChipPage::~ArtInChipPage()
{
  if(m_workerThread && m_workerThread->isRunning())
  {
    m_workerThread->quit();
    m_workerThread->wait(3000);
  }
  libaicupg_exit();
}

void ArtInChipPage::setupUi()
{
  setStyleSheet(R"(
    QLabel {
      color: #2c3e50;
      font-size: 13px;
    }
    QLineEdit {
      padding: 4px 10px;
      border: 2px solid #e1e8ed;
      border-radius: 8px;
      background-color: white;
      color: #2c3e50;
      font-size: 13px;
      min-height: 16px;
      selection-background-color: #3498db;
    }
    QLineEdit:focus {
      border-color: #3498db;
    }
    QLineEdit:disabled {
      background-color: #ecf0f1;
      color: #95a5a6;
    }
    QPushButton {
      padding: 10px 24px;
      border: none;
      border-radius: 10px;
      font-size: 13px;
      font-weight: bold;
      color: white;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #667eea, stop:1 #764ba2);
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #5a6fd6, stop:1 #6a4190);
    }
    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4e5fc4, stop:1 #5e3780);
    }
    QPushButton:disabled {
      background: #bdc3c7;
      color: #ecf0f1;
    }
    QProgressBar {
      border: none;
      border-radius: 8px;
      background-color: #e1e8ed;
      text-align: center;
      font-weight: bold;
      color: #2c3e50;
      height: 24px;
    }
    QProgressBar::chunk {
      border-radius: 8px;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #11998e, stop:1 #38ef7d);
    }
    QTextEdit {
      border: 2px solid #e1e8ed;
      border-radius: 12px;
      background-color: #1e293b;
      color: #e2e8f0;
      font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
      font-size: 12px;
      padding: 12px;
      selection-background-color: #3498db;
    }
    QScrollBar:vertical {
      border: none;
      background: #f1f5f9;
      width: 10px;
      border-radius: 5px;
    }
    QScrollBar::handle:vertical {
      background: #cbd5e1;
      border-radius: 5px;
      min-height: 30px;
    }
    QScrollBar::handle:vertical:hover {
      background: #94a3b8;
    }
  )");

  QVBoxLayout * mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(5);
  mainLayout->setContentsMargins(5, 5, 5, 5);

  QHBoxLayout * headerLayout = new QHBoxLayout();
  QPushButton * backBtn = new QPushButton(tr("< Back to Menu"), this);
  backBtn->setFixedWidth(150);
  backBtn->setFixedHeight(32);
  backBtn->setCursor(Qt::PointingHandCursor);
  backBtn->setStyleSheet(R"(
    QPushButton {
      padding: 6px 16px;
      border: none;
      border-radius: 8px;
      font-size: 12px;
      font-weight: bold;
      color: #64748b;
      background: #e2e8f0;
    }
    QPushButton:hover {
      background: #cbd5e1;
      color: #334155;
    }
  )");
  headerLayout->addWidget(backBtn);
  headerLayout->addStretch();

  m_statusLabel = new QLabel(tr("Ready"), this);
  m_statusLabel->setStyleSheet("QLabel { color: #22c55e; font-weight: bold; font-size: 14px; }");
  headerLayout->addWidget(m_statusLabel);

  mainLayout->addLayout(headerLayout);

  connect(backBtn, &QPushButton::clicked, this, &ArtInChipPage::backToHomeClicked);

  QFrame * fileCard = new QFrame(this);
  fileCard->setStyleSheet(R"(
    QFrame {
      background-color: #dbeafe;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * fileShadow = new QGraphicsDropShadowEffect(this);
  fileShadow->setBlurRadius(20);
  fileShadow->setColor(QColor(0, 0, 0, 30));
  fileShadow->setOffset(0, 4);
  fileCard->setGraphicsEffect(fileShadow);

  QVBoxLayout * fileCardLayout = new QVBoxLayout(fileCard);
  fileCardLayout->setContentsMargins(12, 12, 12, 12);
  fileCardLayout->setSpacing(8);

  QLabel * fileTitle = new QLabel(tr("Firmware Image"), this);
  fileTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  fileCardLayout->addWidget(fileTitle);

  QHBoxLayout * fileLayout = new QHBoxLayout();
  fileLayout->setSpacing(12);
  m_filePathEdit = new CustomLineEdit(this);
  m_filePathEdit->setReadOnly(true);
  m_filePathEdit->setPlaceholderText(tr("Select firmware image file (.img)"));
  fileLayout->addWidget(m_filePathEdit);

  m_selectFileBtn = new QPushButton(tr("Browse"), this);
  m_selectFileBtn->setFixedWidth(120);
  m_selectFileBtn->setFixedHeight(36);
  fileLayout->addWidget(m_selectFileBtn);
  fileCardLayout->addLayout(fileLayout);

  mainLayout->addWidget(fileCard);

  QFrame * progressCard = new QFrame(this);
  progressCard->setStyleSheet(R"(
    QFrame {
      background-color: #fef3c7;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * progressShadow = new QGraphicsDropShadowEffect(this);
  progressShadow->setBlurRadius(20);
  progressShadow->setColor(QColor(0, 0, 0, 30));
  progressShadow->setOffset(0, 4);
  progressCard->setGraphicsEffect(progressShadow);

  QVBoxLayout * progressCardLayout = new QVBoxLayout(progressCard);
  progressCardLayout->setContentsMargins(12, 12, 12, 12);
  progressCardLayout->setSpacing(8);

  QLabel * progressTitle = new QLabel(tr("Download Progress"), this);
  progressTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  progressCardLayout->addWidget(progressTitle);

  QHBoxLayout * progressRowLayout = new QHBoxLayout();
  progressRowLayout->setSpacing(12);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFormat("%p%");
  m_progressBar->setValue(0);
  m_progressBar->setFixedHeight(28);
  progressRowLayout->addWidget(m_progressBar);

  m_startBtn = new QPushButton(tr("Start Download"), this);
  m_startBtn->setFixedWidth(140);
  m_startBtn->setFixedHeight(36);
  m_startBtn->setEnabled(false);
  m_startBtn->setStyleSheet(R"(
    QPushButton {
      padding: 8px 20px;
      border: none;
      border-radius: 10px;
      font-size: 13px;
      font-weight: bold;
      color: white;
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #11998e, stop:1 #38ef7d);
    }
    QPushButton:hover {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0d8579, stop:1 #2dd46a);
    }
    QPushButton:pressed {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0a7166, stop:1 #25b85a);
    }
    QPushButton:disabled {
      background: #bdc3c7;
      color: #ecf0f1;
    }
  )");
  progressRowLayout->addWidget(m_startBtn);

  progressCardLayout->addLayout(progressRowLayout);

  mainLayout->addWidget(progressCard);

  QFrame * logCard = new QFrame(this);
  logCard->setStyleSheet(R"(
    QFrame {
      background-color: #ffffff;
      border-radius: 16px;
    }
  )");
  QGraphicsDropShadowEffect * logShadow = new QGraphicsDropShadowEffect(this);
  logShadow->setBlurRadius(20);
  logShadow->setColor(QColor(0, 0, 0, 30));
  logShadow->setOffset(0, 4);
  logCard->setGraphicsEffect(logShadow);

  QVBoxLayout * logCardLayout = new QVBoxLayout(logCard);
  logCardLayout->setContentsMargins(12, 12, 12, 12);
  logCardLayout->setSpacing(8);

  QLabel * logTitle = new QLabel(tr("Activity Log"), this);
  logTitle->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #64748b; }");
  logCardLayout->addWidget(logTitle);

  m_logEdit = new CustomTextEdit(this);
  m_logEdit->setReadOnly(true);
  m_logEdit->setMinimumHeight(100);
  logCardLayout->addWidget(m_logEdit, 1);

  mainLayout->addWidget(logCard);

  connect(m_selectFileBtn, &QPushButton::clicked, this, &ArtInChipPage::onSelectFileClicked);
  connect(m_startBtn, &QPushButton::clicked, this, &ArtInChipPage::onStartDownloadClicked);
}

void ArtInChipPage::log(const QString & message)
{
  QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss]");
  QString color;

  if(message.contains("Error", Qt::CaseInsensitive) || message.contains("Failed", Qt::CaseInsensitive) ||
     message.contains("fail", Qt::CaseInsensitive))
  {
    color = "#f87171";
  }
  else if(message.contains("Success", Qt::CaseInsensitive) || message.contains("completed", Qt::CaseInsensitive) ||
          message.contains("successfully", Qt::CaseInsensitive) || message.contains("Burn", Qt::CaseInsensitive))
  {
    color = "#4ade80";
  }
  else if(message.contains("Warning", Qt::CaseInsensitive))
  {
    color = "#fbbf24";
  }
  else
  {
    color = "#94a3b8";
  }

  m_logEdit->append(QString("<span style='color:#64748b;'>%1</span> <span style='color:%2;'>%3</span>")
                    .arg(timestamp)
                    .arg(color)
                    .arg(message.toHtmlEscaped()));
}

void ArtInChipPage::appendLog(const QString & message)
{
  QStringList lines = message.split('\n', Qt::SkipEmptyParts);
  for(const QString & line : lines)
  {
    QString trimmed = line.trimmed();
    if(!trimmed.isEmpty())
    {
      log(trimmed);
    }
  }
}

void ArtInChipPage::updateProgress(int current, int total)
{
  if(total > 0)
  {
    int pct = current * 100 / total;
    m_progressBar->setValue(pct);
  }
}

void ArtInChipPage::onSelectFileClicked()
{
  QString filePath = QFileDialog::getOpenFileName(this, tr("Select Firmware Image"),
                                                  m_lastDirPath, tr("Image Files (*.img);;All Files (*)"));

  if(!filePath.isEmpty())
  {
    m_lastDirPath = QFileInfo(filePath).path();
    m_firmwarePath = filePath;
    m_filePathEdit->setText(filePath);
    m_filePathEdit->setStyleSheet("QLineEdit { padding: 10px 14px; border: 2px solid #11998e; border-radius: 10px; background-color: white; color: #2c3e50; font-size: 13px; }");
    log(tr("Selected image: %1").arg(filePath));

    m_startBtn->setEnabled(true);
  }
}

void ArtInChipPage::onStartDownloadClicked()
{
  if(m_firmwarePath.isEmpty())
  {
    log(tr("Error: No image file selected"));
    return;
  }

  m_startBtn->setEnabled(false);
  m_selectFileBtn->setEnabled(false);
  m_progressBar->setValue(0);

  m_statusLabel->setText(tr("Downloading..."));
  m_statusLabel->setStyleSheet("QLabel { color: #f59e0b; font-weight: bold; font-size: 14px; }");

  log(tr("Starting ArtInChip firmware download..."));
  log(tr("Image: %1").arg(m_firmwarePath));

  startDownload();
}

void ArtInChipPage::resetUi()
{
  m_startBtn->setEnabled(true);
  m_selectFileBtn->setEnabled(true);
}

static int runUpgcmdImage(void * userdata)
{
  ArtInChipPage * page = static_cast<ArtInChipPage *>(userdata);
  QString firmwarePath = page->property("firmwarePath").toString();

  upgcmd_set_output_callback(outputCallback, page);
  upgcmd_set_progress_callback(progressCallback, page);
  g_config.show_progress = 1;

  upg_device_t ** dev_list = NULL;
  int dev_count = 0;

  if(upg_usb_dev_get_list(&dev_list, &dev_count) < 0 || dev_count == 0)
  {
    aicupg_output("No usbupg device is found.");
    upgcmd_set_output_callback(NULL, NULL);
    return 1;
  }

  upg_device_t * dev = dev_list[0];
  if(upg_usb_dev_open(dev) < 0)
  {
    aicupg_output("Open upg device failed: %s", upg_usb_dev_get_last_error());
    upg_usb_dev_free_list(dev_list, dev_count);
    upgcmd_set_output_callback(NULL, NULL);
    return 1;
  }

  aicupg_trans_init(dev);

  QByteArray pathBytes = firmwarePath.toLocal8Bit();
  int ret = image_do_upgrade(dev, pathBytes.constData());

  aicupg_trans_exit(dev);
  upg_usb_dev_close(dev);
  upg_usb_dev_free_list(dev_list, dev_count);

  upgcmd_set_output_callback(NULL, NULL);
  upgcmd_set_progress_callback(NULL, NULL);
  return ret;
}

static int runUpgcmdReset(void * userdata)
{
  ArtInChipPage * page = static_cast<ArtInChipPage *>(userdata);

  upgcmd_set_output_callback(outputCallback, page);
  upgcmd_set_progress_callback(progressCallback, page);

  upg_device_t ** dev_list = NULL;
  int dev_count = 0;

  if(upg_usb_dev_get_list(&dev_list, &dev_count) < 0 || dev_count == 0)
  {
    aicupg_output("No usbupg device is found.");
    upgcmd_set_output_callback(NULL, NULL);
    return 1;
  }

  upg_device_t * dev = dev_list[0];
  if(upg_usb_dev_open(dev) < 0)
  {
    aicupg_output("Open upg device failed: %s", upg_usb_dev_get_last_error());
    upg_usb_dev_free_list(dev_list, dev_count);
    upgcmd_set_output_callback(NULL, NULL);
    return 1;
  }

  aicupg_trans_init(dev);

  int ret = aicupg_cmd_run_shell_str(dev, "reset");

  aicupg_trans_exit(dev);
  upg_usb_dev_close(dev);
  upg_usb_dev_free_list(dev_list, dev_count);

  upgcmd_set_output_callback(NULL, NULL);
  upgcmd_set_progress_callback(NULL, NULL);
  return ret;
}

void ArtInChipPage::startDownload()
{
  setProperty("firmwarePath", m_firmwarePath);

  int * result = new int(-1);

  m_workerThread = QThread::create([this, result]()
  {
    *result = runUpgcmdImage(this);
  });

  connect(m_workerThread, &QThread::finished, this, [this, result]()
  {
    int exitCode = *result;
    delete result;
    onDownloadFinished(exitCode);
  });

  m_workerThread->start();
}

void ArtInChipPage::startReset()
{
  int * result = new int(-1);

  m_workerThread = QThread::create([this, result]()
  {
    *result = runUpgcmdReset(this);
  });

  connect(m_workerThread, &QThread::finished, this, [this, result]()
  {
    m_progressBar->setValue(100);
    int exitCode = *result;
    delete result;
    if(exitCode == 0)
    {
      log(tr("Device reset successfully"));
    }
    log(tr("Download completed!"));
    m_statusLabel->setText(tr("Completed"));
    m_statusLabel->setStyleSheet("QLabel { color: #22c55e; font-weight: bold; font-size: 14px; }");
    resetUi();
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
  });

  m_workerThread->start();
}

void ArtInChipPage::onDownloadFinished(int exitCode)
{
  if(m_workerThread)
  {
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
  }

  if(exitCode == 0)
  {
    m_progressBar->setValue(80);
    log(tr("Image download completed, resetting device..."));

    startReset();
  }
  else
  {
    log(tr("Error: Image download failed (exit code: %1)").arg(exitCode));
    m_statusLabel->setText(tr("Failed"));
    m_statusLabel->setStyleSheet("QLabel { color: #ef4444; font-weight: bold; font-size: 14px; }");
    resetUi();
  }
}
