#include "jellyfinresultswidget.h"

#include "../nounoursengine.h"
#include "../mpvhandler.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QTreeWidget>
#include <QLabel>
#include <QTextEdit>
#include <QToolButton>
#include <QSplitter>
#include <QResizeEvent>

JellyfinResultsWidget::JellyfinResultsWidget(NounoursEngine *nounours, QWidget *parent)
    : QWidget(parent), nounours(nounours), jellyfin(nounours->jellyfin)
{
    resultsList = new QTreeWidget(this);
    resultsList->setHeaderHidden(true);
    resultsList->setColumnCount(1);

    // Poster area: container with absolute-positioned label + heart overlay
    posterContainer = new QWidget(this);
    posterContainer->setMinimumHeight(80);
    posterContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    posterContainer->setStyleSheet("background: #111;");
    posterContainer->installEventFilter(this);

    posterLabel = new QLabel(posterContainer);
    posterLabel->setAlignment(Qt::AlignCenter);
    posterLabel->setGeometry(posterContainer->rect());

    heartButton = new QToolButton(posterContainer);
    heartButton->setCheckable(true);
    heartButton->setText(QStringLiteral("♥")); // ♥
    heartButton->setFixedSize(30, 30);
    heartButton->setVisible(false);
    heartButton->setStyleSheet(
        "QToolButton { color: rgba(210,210,210,170); background: rgba(0,0,0,120); "
        "border: none; border-radius: 15px; font-size: 16px; }"
        "QToolButton:hover { background: rgba(0,0,0,190); color: rgba(255,180,180,230); }"
        "QToolButton:checked { color: #e53935; background: rgba(0,0,0,150); }"
    );

    synopsisEdit = new QTextEdit(this);
    synopsisEdit->setReadOnly(true);

    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);
    rightLayout->addWidget(posterContainer, 2);
    rightLayout->addWidget(synopsisEdit, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(resultsList);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto *playMenu        = new QMenu(this);
    auto *actPlay         = playMenu->addAction(tr("Play"));
    auto *actTranscode720  = playMenu->addAction(tr("Transcode to 720p (1280×720)"));
    auto *actTranscode1080 = playMenu->addAction(tr("Transcode to 1080p (1920×1080)"));

    playButton = new QToolButton(this);
    playButton->setText(tr("Play"));
    playButton->setEnabled(false);
    playButton->setPopupMode(QToolButton::MenuButtonPopup);
    playButton->setMenu(playMenu);
    playButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(playButton);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(splitter);
    root->addLayout(buttonLayout);

    connect(jellyfin, &JellyfinManager::seasonsResultsSignal, this, [=](QString seriesId, QList<JellyfinItem> items)
    {
        QTreeWidgetItem *seriesItem = seriesNodes.value(seriesId);
        if(!seriesItem)
            return;

        qDeleteAll(seriesItem->takeChildren());

        for(const auto &season : items)
        {
            auto *node = new QTreeWidgetItem(seriesItem);
            node->setText(0, season.name.isEmpty() ? tr("Season %0").arg(season.index) : season.name);
            node->setData(0, Qt::UserRole, QVariant::fromValue(season));

            auto *placeholder = new QTreeWidgetItem(node);
            placeholder->setText(0, tr("Loading..."));
            seasonNodes[season.id] = node;
        }
    });

    connect(jellyfin, &JellyfinManager::episodesResultsSignal, this, [=](QString seasonId, QList<JellyfinItem> items)
    {
        QTreeWidgetItem *seasonItem = seasonNodes.value(seasonId);
        if(!seasonItem)
            return;

        qDeleteAll(seasonItem->takeChildren());

        for(const auto &episode : items)
        {
            auto *node = new QTreeWidgetItem(seasonItem);
            node->setText(0, QString("%0. %1").arg(episode.index).arg(episode.name));
            node->setData(0, Qt::UserRole, QVariant::fromValue(episode));
        }
    });

    connect(resultsList, &QTreeWidget::itemExpanded, this, &JellyfinResultsWidget::OnItemExpanded);

    connect(resultsList, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem *current, QTreeWidgetItem *)
    {
        QVariant data = current ? current->data(0, Qt::UserRole) : QVariant();
        if(data.isNull())
        {
            playButton->setEnabled(false);
            synopsisEdit->clear();
            currentImageId.clear();
            currentPixmap = QPixmap();
            posterLabel->clear();
            heartButton->setVisible(false);
            return;
        }

        JellyfinItem item = data.value<JellyfinItem>();
        synopsisEdit->setPlainText(item.overview.isEmpty() ? tr("No synopsis available.") : item.overview);
        playButton->setEnabled(item.type == "Movie" || item.type == "Video" || item.type == "Episode");

        currentImageId = item.id;
        currentPixmap = QPixmap();
        posterLabel->clear();
        jellyfin->FetchImage(item.id);

        heartButton->blockSignals(true);
        heartButton->setChecked(item.isFavorite);
        heartButton->blockSignals(false);
        heartButton->setVisible(true);
        PositionHeart();
    });

    connect(jellyfin, &JellyfinManager::imageReadySignal, this, [=](QString itemId, QPixmap pixmap)
    {
        if(itemId != currentImageId)
            return;
        currentPixmap = pixmap;
        if(pixmap.isNull())
        {
            posterLabel->clear();
            return;
        }
        QSize sz = posterContainer->size();
        posterLabel->setPixmap(pixmap.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    connect(heartButton, &QToolButton::toggled, this, [=](bool checked)
    {
        if(currentImageId.isEmpty())
            return;
        // optimistic update: persist new state in the tree item data
        QTreeWidgetItem *current = resultsList->currentItem();
        if(current)
        {
            JellyfinItem item = current->data(0, Qt::UserRole).value<JellyfinItem>();
            item.isFavorite = checked;
            current->setData(0, Qt::UserRole, QVariant::fromValue(item));
        }
        jellyfin->ToggleFavorite(currentImageId, checked);
    });

    connect(playButton,     &QToolButton::clicked, this, [=]{ DoPlay(0);    });
    connect(actPlay,        &QAction::triggered,   this, [=]{ DoPlay(0);    });
    connect(actTranscode720, &QAction::triggered,  this, [=]{ DoPlay(720);  });
    connect(actTranscode1080,&QAction::triggered,  this, [=]{ DoPlay(1080); });
    connect(resultsList, &QTreeWidget::itemDoubleClicked, this, [=]{ DoPlay(0); });
}

bool JellyfinResultsWidget::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == posterContainer && event->type() == QEvent::Resize)
    {
        QSize sz = static_cast<QResizeEvent*>(event)->size();
        posterLabel->setGeometry(0, 0, sz.width(), sz.height());
        if(!currentPixmap.isNull())
            posterLabel->setPixmap(currentPixmap.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if(heartButton->isVisible())
            heartButton->move(sz.width() - heartButton->width() - 6,
                              sz.height() - heartButton->height() - 6);
    }
    return QWidget::eventFilter(obj, event);
}

void JellyfinResultsWidget::PositionHeart()
{
    QSize sz = posterContainer->size();
    heartButton->move(sz.width() - heartButton->width() - 6,
                      sz.height() - heartButton->height() - 6);
    heartButton->raise();
}

void JellyfinResultsWidget::SetResults(const QList<JellyfinItem> &items)
{
    resultsList->clear();
    synopsisEdit->clear();
    currentImageId.clear();
    currentPixmap = QPixmap();
    posterLabel->clear();
    heartButton->setVisible(false);
    seriesNodes.clear();
    seasonNodes.clear();

    for(const auto &item : items)
    {
        QString category = item.libraryName;
        if(category.isEmpty())
            category = item.type == "Series" ? tr("Series") : tr("Movie");
        QString label = QString("[%0] %1").arg(category, item.name);
        if(item.year > 0)
            label += QString(" (%0)").arg(item.year);

        auto *node = new QTreeWidgetItem(resultsList);
        node->setText(0, label);
        node->setData(0, Qt::UserRole, QVariant::fromValue(item));

        if(item.type == "Series")
        {
            auto *placeholder = new QTreeWidgetItem(node);
            placeholder->setText(0, tr("Loading..."));
            seriesNodes[item.id] = node;
        }
    }
}

void JellyfinResultsWidget::OnItemExpanded(QTreeWidgetItem *item)
{
    // not yet loaded if the only child is the "Loading..." placeholder (no item data)
    if(item->childCount() != 1 || !item->child(0)->data(0, Qt::UserRole).isNull())
        return;

    JellyfinItem data = item->data(0, Qt::UserRole).value<JellyfinItem>();
    if(data.type == "Series")
        jellyfin->GetSeasons(data.id);
    else if(data.type == "Season")
    {
        JellyfinItem seriesData = item->parent()->data(0, Qt::UserRole).value<JellyfinItem>();
        jellyfin->GetEpisodes(seriesData.id, data.id);
    }
}

void JellyfinResultsWidget::DoPlay(int transcodeHeight)
{
    QTreeWidgetItem *current = resultsList->currentItem();
    if(!current)
        return;

    QVariant data = current->data(0, Qt::UserRole);
    if(data.isNull())
        return;

    JellyfinItem item = data.value<JellyfinItem>();
    if(item.type != "Movie" && item.type != "Video" && item.type != "Episode")
        return;

    if(item.type == "Episode")
    {
        QTreeWidgetItem *seasonItem = current->parent();
        QTreeWidgetItem *seriesItem = seasonItem ? seasonItem->parent() : nullptr;
        if(!seasonItem || !seriesItem)
            return;

        QList<JellyfinItem> episodes;
        int episodeIndex = -1;
        for(int i = 0; i < seasonItem->childCount(); i++)
        {
            QVariant d = seasonItem->child(i)->data(0, Qt::UserRole);
            if(d.isNull())
                continue;
            JellyfinItem episode = d.value<JellyfinItem>();
            if(episode.id == item.id)
                episodeIndex = episodes.size();
            episodes.append(episode);
        }

        QList<JellyfinItem> seasons;
        int seasonIndex = -1;
        JellyfinItem seasonData = seasonItem->data(0, Qt::UserRole).value<JellyfinItem>();
        for(int i = 0; i < seriesItem->childCount(); i++)
        {
            QVariant d = seriesItem->child(i)->data(0, Qt::UserRole);
            if(d.isNull())
                continue;
            JellyfinItem season = d.value<JellyfinItem>();
            if(season.id == seasonData.id)
                seasonIndex = seasons.size();
            seasons.append(season);
        }

        JellyfinItem seriesData = seriesItem->data(0, Qt::UserRole).value<JellyfinItem>();
        jellyfin->PlayEpisodes(seriesData.id, seasons, seasonIndex, episodes, episodeIndex, transcodeHeight);
    }
    else
        jellyfin->PlayMovie(item, transcodeHeight);

    window()->close();
}
