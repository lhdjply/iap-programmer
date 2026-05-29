#ifndef ARTINCHIPPAGE_H
#define ARTINCHIPPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QThread>
#include "customwidgets.h"

class ArtInChipPage : public QWidget
{
    Q_OBJECT

  public:
    explicit ArtInChipPage(QWidget * parent = nullptr);
    ~ArtInChipPage();

  signals:
    void backToHomeClicked();

  public slots:
    void appendLog(const QString & message);
    void updateProgress(int current, int total);

  private slots:
    void onSelectFileClicked();
    void onStartDownloadClicked();
    void onDownloadFinished(int exitCode);

  private:
    void setupUi();
    void log(const QString & message);
    void startDownload();
    void startReset();
    void resetUi();

    QLabel * m_statusLabel;
    QPushButton * m_selectFileBtn;
    QPushButton * m_startBtn;
    QLineEdit * m_filePathEdit;
    QProgressBar * m_progressBar;
    QTextEdit * m_logEdit;

    QString m_firmwarePath;
    QString m_lastDirPath;
    QThread * m_workerThread;
};

#endif // ARTINCHIPPAGE_H
