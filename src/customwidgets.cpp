#include "customwidgets.h"
#include <QAction>
#include <QObject>

void CustomTextEdit::contextMenuEvent(QContextMenuEvent * event)
{
  QMenu * menu = createStandardContextMenu();

  QList<QAction *> actions = menu->actions();
  for(QAction * action : actions)
  {
    if(action->text() == "&Undo")
      action->setText(QObject::tr("Undo"));
    else if(action->text() == "&Redo")
      action->setText(QObject::tr("Redo"));
    else if(action->text() == "Cu&t")
      action->setText(QObject::tr("Cut"));
    else if(action->text() == "&Copy")
      action->setText(QObject::tr("Copy"));
    else if(action->text() == "&Paste")
      action->setText(QObject::tr("Paste"));
    else if(action->text() == "Delete")
      action->setText(QObject::tr("Delete"));
    else if(action->text() == "Select All")
      action->setText(QObject::tr("Select All"));
  }

  menu->exec(event->globalPos());
  delete menu;
}

void CustomLineEdit::contextMenuEvent(QContextMenuEvent * event)
{
  QMenu * menu = createStandardContextMenu();

  QList<QAction *> actions = menu->actions();
  for(QAction * action : actions)
  {
    if(action->text() == "&Undo")
      action->setText(QObject::tr("Undo"));
    else if(action->text() == "&Redo")
      action->setText(QObject::tr("Redo"));
    else if(action->text() == "Cu&t")
      action->setText(QObject::tr("Cut"));
    else if(action->text() == "&Copy")
      action->setText(QObject::tr("Copy"));
    else if(action->text() == "&Paste")
      action->setText(QObject::tr("Paste"));
    else if(action->text() == "Delete")
      action->setText(QObject::tr("Delete"));
    else if(action->text() == "Select All")
      action->setText(QObject::tr("Select All"));
  }

  menu->exec(event->globalPos());
  delete menu;
}
