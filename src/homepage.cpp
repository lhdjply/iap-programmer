#include "homepage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QGraphicsDropShadowEffect>

HomePage::HomePage(QWidget * parent)
  : QWidget(parent)
{
  setupUi();
}

void HomePage::setupUi()
{
  QVBoxLayout * layout = new QVBoxLayout(this);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(30);

  QLabel * titleLabel = new QLabel("IAP Programmer", this);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setStyleSheet(R"(
    QLabel {
      font-size: 32px;
      font-weight: bold;
      color: #2c3e50;
    }
  )");
  layout->addWidget(titleLabel);

  QLabel * subtitleLabel = new QLabel(tr("In-Application Programming Tool"), this);
  subtitleLabel->setAlignment(Qt::AlignCenter);
  subtitleLabel->setStyleSheet(R"(
    QLabel {
      font-size: 16px;
      color: #64748b;
    }
  )");
  layout->addWidget(subtitleLabel);

  layout->addSpacing(20);

  // 按钮区域
  QHBoxLayout * buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(30);
  buttonRow->setAlignment(Qt::AlignCenter);

  const QString cardStyle = R"(
    QFrame {
      background-color: white;
      border-radius: 20px;
    }
  )";
  const QString btnStyle = R"(
    QPushButton {
      padding: 12px 24px;
      border: none;
      border-radius: 12px;
      font-size: 15px;
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
  )";

  // 匠芯创芯片下载
  QFrame * card1 = new QFrame(this);
  card1->setFixedSize(240, 120);
  card1->setStyleSheet(cardStyle);
  QGraphicsDropShadowEffect * shadow1 = new QGraphicsDropShadowEffect(this);
  shadow1->setBlurRadius(30);
  shadow1->setColor(QColor(0, 0, 0, 40));
  shadow1->setOffset(0, 8);
  card1->setGraphicsEffect(shadow1);

  QVBoxLayout * card1Layout = new QVBoxLayout(card1);
  card1Layout->setAlignment(Qt::AlignCenter);

  QPushButton * btnArtInChip = new QPushButton(tr("ArtInChip Download"), this);
  btnArtInChip->setFixedSize(180, 50);
  btnArtInChip->setCursor(Qt::PointingHandCursor);
  btnArtInChip->setStyleSheet(btnStyle);
  card1Layout->addWidget(btnArtInChip);

  buttonRow->addWidget(card1);

  // 当前 IAP 下载
  QFrame * card2 = new QFrame(this);
  card2->setFixedSize(240, 120);
  card2->setStyleSheet(cardStyle);
  QGraphicsDropShadowEffect * shadow2 = new QGraphicsDropShadowEffect(this);
  shadow2->setBlurRadius(30);
  shadow2->setColor(QColor(0, 0, 0, 40));
  shadow2->setOffset(0, 8);
  card2->setGraphicsEffect(shadow2);

  QVBoxLayout * card2Layout = new QVBoxLayout(card2);
  card2Layout->setAlignment(Qt::AlignCenter);

  QPushButton * btnIap = new QPushButton(tr("IAP Upgrade Tool"), this);
  btnIap->setFixedSize(180, 50);
  btnIap->setCursor(Qt::PointingHandCursor);
  btnIap->setStyleSheet(btnStyle);
  card2Layout->addWidget(btnIap);

  buttonRow->addWidget(card2);

  layout->addLayout(buttonRow);

  connect(btnArtInChip, &QPushButton::clicked, this, &HomePage::enterArtInChipClicked);
  connect(btnIap, &QPushButton::clicked, this, &HomePage::enterUpgradeClicked);
}
