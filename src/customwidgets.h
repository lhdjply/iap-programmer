#ifndef CUSTOMWIDGETS_H
#define CUSTOMWIDGETS_H

#include <QTextEdit>
#include <QLineEdit>
#include <QMenu>
#include <QContextMenuEvent>

class CustomTextEdit : public QTextEdit
{
    Q_OBJECT
  public:
    explicit CustomTextEdit(QWidget * parent = nullptr) : QTextEdit(parent) {}

  protected:
    void contextMenuEvent(QContextMenuEvent * event) override;
};

class CustomLineEdit : public QLineEdit
{
    Q_OBJECT
  public:
    explicit CustomLineEdit(QWidget * parent = nullptr) : QLineEdit(parent) {}

  protected:
    void contextMenuEvent(QContextMenuEvent * event) override;
};

#endif // CUSTOMWIDGETS_H
