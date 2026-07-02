#include "jellyfinsearchdialog.h"
#include "jellyfinresultswidget.h"

#include "../nounoursengine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QSplitter>
#include <QTabWidget>
#include <QDialogButtonBox>

JellyfinSearchDialog::JellyfinSearchDialog(NounoursEngine *nounours, QWidget *parent)
    : QDialog(parent), nounours(nounours), jellyfin(nounours->jellyfin)
{
    setWindowTitle(tr("Jellyfin Search"));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    resize(800, 550);

    // Search tab
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(tr("Search for a movie or series..."));
    searchButton = new QPushButton(tr("Search"), this);

    auto *searchLayout = new QHBoxLayout;
    searchLayout->addWidget(searchEdit);
    searchLayout->addWidget(searchButton);

    searchResults = new JellyfinResultsWidget(nounours, this);

    auto *searchTab = new QWidget(this);
    auto *searchTabLayout = new QVBoxLayout(searchTab);
    searchTabLayout->addLayout(searchLayout);
    searchTabLayout->addWidget(searchResults);

    // Genres tab
    genresList = new QListWidget(this);
    genreResults = new JellyfinResultsWidget(nounours, this);

    auto *genresSplitter = new QSplitter(Qt::Horizontal, this);
    genresSplitter->addWidget(genresList);
    genresSplitter->addWidget(genreResults);
    genresSplitter->setStretchFactor(0, 1);
    genresSplitter->setStretchFactor(1, 2);

    auto *genresTab = new QWidget(this);
    auto *genresTabLayout = new QVBoxLayout(genresTab);
    genresTabLayout->setContentsMargins(0, 0, 0, 0);
    genresTabLayout->addWidget(genresSplitter);

    // Favorites tab
    favoritesResults = new JellyfinResultsWidget(nounours, this);

    auto *favRefreshButton = new QPushButton(tr("Refresh"), this);
    auto *favButtonLayout = new QHBoxLayout;
    favButtonLayout->addStretch();
    favButtonLayout->addWidget(favRefreshButton);

    auto *favoritesTab = new QWidget(this);
    auto *favoritesTabLayout = new QVBoxLayout(favoritesTab);
    favoritesTabLayout->setContentsMargins(0, 4, 0, 0);
    favoritesTabLayout->addLayout(favButtonLayout);
    favoritesTabLayout->addWidget(favoritesResults);

    tabWidget = new QTabWidget(this);
    tabWidget->addTab(searchTab, tr("Search"));
    tabWidget->addTab(genresTab, tr("Genres"));
    tabWidget->addTab(favoritesTab, tr("Favorites"));

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabWidget);
    root->addWidget(statusLabel);
    root->addWidget(buttons);

    bool connected = jellyfin->isConnected();
    searchEdit->setEnabled(connected);
    searchButton->setEnabled(connected);
    genresList->setEnabled(connected);
    if(!connected)
    {
        statusLabel->setText(tr("Connecting to Jellyfin server..."));
        jellyfin->Connect();
    }
    else
        jellyfin->GetGenres();

    connect(jellyfin, &JellyfinManager::connectedSignal, this, [=]
    {
        searchEdit->setEnabled(true);
        searchButton->setEnabled(true);
        genresList->setEnabled(true);
        statusLabel->clear();
        jellyfin->GetGenres();
    });

    connect(jellyfin, &JellyfinManager::connectionFailedSignal, this, [=](QString error)
    {
        searchEdit->setEnabled(false);
        searchButton->setEnabled(false);
        genresList->setEnabled(false);
        statusLabel->setText(tr("Connection failed: %0\nCheck the Jellyfin settings in Preferences.").arg(error));
    });

    connect(jellyfin, &JellyfinManager::searchResultsSignal, this, [=](QList<JellyfinItem> items)
    {
        searchResults->SetResults(items);
        statusLabel->setText(items.isEmpty() ? tr("No results found.") : tr("%0 result(s).").arg(items.size()));
    });

    connect(jellyfin, &JellyfinManager::genresResultsSignal, this, [=](QList<JellyfinItem> items)
    {
        genresList->clear();
        for(const auto &genre : items)
        {
            auto *item = new QListWidgetItem(genre.name, genresList);
            item->setData(Qt::UserRole, genre.id);
        }
    });

    connect(jellyfin, &JellyfinManager::genreItemsResultsSignal, this, [=](QString genreId, QList<JellyfinItem> items)
    {
        QListWidgetItem *current = genresList->currentItem();
        if(!current || current->data(Qt::UserRole).toString() != genreId)
            return;

        genreResults->SetResults(items);
        statusLabel->setText(items.isEmpty() ? tr("No results found.") : tr("%0 result(s).").arg(items.size()));
    });

    connect(jellyfin, &JellyfinManager::favoritesResultsSignal, this, [=](QList<JellyfinItem> items)
    {
        favoritesResults->SetResults(items);
        statusLabel->setText(items.isEmpty() ? tr("No favorites found.") : tr("%0 favorite(s).").arg(items.size()));
    });

    connect(favRefreshButton, &QPushButton::clicked, this, [=]
    {
        statusLabel->setText(tr("Loading..."));
        jellyfin->GetFavorites();
    });

    connect(tabWidget, &QTabWidget::currentChanged, this, [=](int index)
    {
        if(index == 2 && jellyfin->isConnected())
        {
            statusLabel->setText(tr("Loading..."));
            jellyfin->GetFavorites();
        }
    });

    // When a favorite is toggled while the favorites tab is active, refresh the list
    connect(jellyfin, &JellyfinManager::favoriteToggledSignal, this, [=](QString, bool)
    {
        if(tabWidget->currentIndex() == 2)
        {
            statusLabel->setText(tr("Loading..."));
            jellyfin->GetFavorites();
        }
    });

    connect(searchButton, &QPushButton::clicked, this, &JellyfinSearchDialog::DoSearch);
    connect(searchEdit, &QLineEdit::returnPressed, this, &JellyfinSearchDialog::DoSearch);

    connect(genresList, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *current, QListWidgetItem *)
    {
        if(!current)
            return;

        statusLabel->setText(tr("Loading..."));
        jellyfin->GetItemsByGenre(current->data(Qt::UserRole).toString());
    });
}

void JellyfinSearchDialog::DoSearch()
{
    statusLabel->setText(tr("Searching..."));
    jellyfin->Search(searchEdit->text());
}
