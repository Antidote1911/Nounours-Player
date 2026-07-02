#ifndef JELLYFINRESULTSWIDGET_H
#define JELLYFINRESULTSWIDGET_H

#include <QWidget>
#include <QList>
#include <QHash>
#include <QPixmap>

#include "../jellyfinmanager.h"

class NounoursEngine;

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QTextEdit;
class QToolButton;

class JellyfinResultsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit JellyfinResultsWidget(NounoursEngine *nounours, QWidget *parent = nullptr);

    void SetResults(const QList<JellyfinItem> &items);

private:
    NounoursEngine  *nounours;
    JellyfinManager *jellyfin;

    QTreeWidget *resultsList;
    QWidget     *posterContainer;
    QLabel      *posterLabel;
    QToolButton *heartButton;
    QTextEdit   *synopsisEdit;
    QToolButton *playButton;

    QString currentImageId;
    QPixmap currentPixmap;

    QHash<QString, QTreeWidgetItem*> seriesNodes;
    QHash<QString, QTreeWidgetItem*> seasonNodes;

    void OnItemExpanded(QTreeWidgetItem *item);
    void DoPlay(int transcodeHeight = 0);
    void PositionHeart();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // JELLYFINRESULTSWIDGET_H
