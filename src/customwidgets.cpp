#include "customwidgets.h"
#include <QAction>
#include <QObject>

void CustomTextEdit::contextMenuEvent(QContextMenuEvent * event)
{
  QMenu * menu = createStandardContextMenu();

  QList<QAction *> actions = menu->actions();
  for(QAction * action : actions)
  {
    QString text = action->text();
    int tabPos = text.indexOf('\t');
    if(tabPos != -1)
    {
      text = text.left(tabPos);
    }

    if(text == "&Undo")
    {
      action->setText(tr("Undo") + "\t Ctrl+Z");
    }
    else if(text == "&Redo")
    {
      action->setText(tr("Redo") + "\t Ctrl+Y");
    }
    else if(text == "Cu&t")
    {
      action->setText(tr("Cut") + "\t Ctrl+X");
    }
    else if(text == "&Copy")
    {
      action->setText(tr("Copy") + "\t Ctrl+C");
    }
    else if(text == "&Paste")
    {
      action->setText(tr("Paste") + "\t Ctrl+V");
    }
    else if(text == "Delete")
    {
      action->setText(tr("Delete"));
    }
    else if(text == "Select All")
    {
      action->setText(tr("Select All") + "\t Ctrl+A");
    }
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
    QString text = action->text();
    int tabPos = text.indexOf('\t');
    if(tabPos != -1)
    {
      text = text.left(tabPos);
    }

    if(text == "&Undo")
    {
      action->setText(tr("Undo") + "\t Ctrl+Z");
    }
    else if(text == "&Redo")
    {
      action->setText(tr("Redo") + "\t Ctrl+Y");
    }
    else if(text == "Cu&t")
    {
      action->setText(tr("Cut") + "\t Ctrl+X");
    }
    else if(text == "&Copy")
    {
      action->setText(tr("Copy") + "\t Ctrl+C");
    }
    else if(text == "&Paste")
    {
      action->setText(tr("Paste") + "\t Ctrl+V");
    }
    else if(text == "Delete")
    {
      action->setText(tr("Delete"));
    }
    else if(text == "Select All")
    {
      action->setText(tr("Select All") + "\t Ctrl+A");
    }
  }

  menu->exec(event->globalPos());
  delete menu;
}
