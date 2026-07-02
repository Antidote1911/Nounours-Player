#include "jellyfinmanager.h"

#include "nounoursengine.h"
#include "mpvhandler.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QSharedPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QHostInfo>
#include <QSysInfo>

// Per-file options for mpv's loadfile command when playing Jellyfin transcode streams.
// Forces OpenGL VO (no Vulkan DRI3) and software decode for this specific file.
static const QString kTranscodeFileOpts = QStringLiteral("hwdec=no");

JellyfinManager::JellyfinManager(QObject *parent):
    QObject(parent),
    nounours(static_cast<NounoursEngine*>(parent)),
    manager(new QNetworkAccessManager(this)),
    playbackProgressTimer(new QTimer(this)),
    socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
{
    // periodically report the playback position while a Jellyfin item is playing
    playbackProgressTimer->setInterval(10000);
    connect(playbackProgressTimer, &QTimer::timeout, this, &JellyfinManager::ReportPlaybackProgress);

    // receive remote-control commands (play/pause/seek/volume/messages) pushed
    // by the Jellyfin server, and keep retrying the connection while logged in
    connect(socket, &QWebSocket::textMessageReceived, this, &JellyfinManager::HandleSocketMessage);
    connect(socket, &QWebSocket::disconnected, this, [=]
    {
        if(!accessToken.isEmpty())
            QTimer::singleShot(5000, this, &JellyfinManager::ConnectWebSocket);
    });

    // continue playback into the next season once its episodes have been fetched
    connect(this, &JellyfinManager::episodesResultsSignal, this, [=](QString seasonId, QList<JellyfinItem> items)
    {
        if(pendingNextSeasonIndex < 0 || seasonId != nowPlayingSeasons[pendingNextSeasonIndex].id)
            return;

        nowPlayingSeasonIndex = pendingNextSeasonIndex;
        pendingNextSeasonIndex = -1;
        nowPlayingEpisodes = items;

        if(transcodeMode)
            nounours->mpv->SuppressHwdec();

        QStringList urls, labels;
        for(const auto &episode : items)
        {
            urls << (transcodeMode ? GetTranscodeUrl(episode.id) : GetStreamUrl(episode.id));
            labels << QString("%0. %1").arg(episode.index).arg(episode.name);
        }

        nounours->mpv->LoadUrlPlaylist(urls, labels, 0,
                                        transcodeMode ? kTranscodeFileOpts : QString{});
    });
}

JellyfinManager::~JellyfinManager()
{
    accessToken.clear(); // prevent the disconnected signal from scheduling a reconnect
    socket->abort();     // force-close the WebSocket without waiting for the closing handshake
}

QString JellyfinManager::AuthHeader() const
{
    return QString("MediaBrowser Client=\"Nounours Player\", Device=\"%2 %3\", DeviceId=\"%0\", Version=\"%1\"")
            .arg(deviceId, NOUNOURS_PLAYER_VERSION, QHostInfo::localHostName(), QSysInfo::prettyProductName());
}

QNetworkRequest JellyfinManager::BuildRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    // identify as the official-ish Nounours Player to reverse proxies that
    // block requests with a generic/script User-Agent (e.g. Qt's default)
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NounoursPlayer/" NOUNOURS_PLAYER_VERSION));
    return request;
}

void JellyfinManager::Connect(const QString &url, const QString &user, const QString &pass)
{
    serverUrl = url;
    username = user;
    password = pass;
    Connect();
}

void JellyfinManager::Connect()
{
    socket->close();

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QNetworkRequest request = BuildRequest(QUrl(url + "/Users/AuthenticateByName"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Emby-Authorization", AuthHeader().toUtf8());

    QJsonObject body;
    body["Username"] = username;
    body["Pw"] = password;

    accessToken.clear();
    userId.clear();

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        accessToken = response["AccessToken"].toString();
        userId = response["User"].toObject()["Id"].toString();

        if(accessToken.isEmpty() || userId.isEmpty())
            emit connectionFailedSignal(tr("Invalid response from server."));
        else
        {
            FetchServerName();
            ReportCapabilities();
            ConnectWebSocket();
            emit connectedSignal();
        }

        reply->deleteLater();
    });
}

void JellyfinManager::FetchServerName()
{
    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QNetworkReply *reply = manager->get(BuildRequest(QUrl(url + "/System/Info/Public")));
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() == QNetworkReply::NoError)
            serverName = QJsonDocument::fromJson(reply->readAll()).object()["ServerName"].toString();
        reply->deleteLater();
    });
}

void JellyfinManager::Search(const QString &term)
{
    if(accessToken.isEmpty())
    {
        emit connectionFailedSignal(tr("Not connected."));
        return;
    }

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    // first list the user's libraries (media folders) so each result can be
    // tagged with the library it belongs to (Movies, Series, Animes, ...)
    QUrl viewsUrl(url + "/Users/" + userId + "/Views");
    QNetworkRequest viewsRequest = BuildRequest(viewsUrl);
    viewsRequest.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *viewsReply = manager->get(viewsRequest);
    connect(viewsReply, &QNetworkReply::finished, this, [=]
    {
        QHash<QString, QString> libraries;
        if(viewsReply->error() == QNetworkReply::NoError)
        {
            static const QSet<QString> excludedTypes = {"music", "musicvideos", "books", "photos", "livetv", "playlists", "boxsets"};

            QJsonObject response = QJsonDocument::fromJson(viewsReply->readAll()).object();
            for(auto entry : response["Items"].toArray())
            {
                QJsonObject obj = entry.toObject();
                if(excludedTypes.contains(obj["CollectionType"].toString()))
                    continue;

                libraries[obj["Id"].toString()] = obj["Name"].toString();
            }
        }
        viewsReply->deleteLater();

        SearchAllLibraries(term, libraries);
    });
}

void JellyfinManager::SearchAllLibraries(const QString &term, const QHash<QString, QString> &libraries)
{
    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    auto buildRequest = [=](const QString &parentId, bool withFields)
    {
        QUrlQuery query;
        query.addQueryItem("searchTerm", term);
        // "Video" covers items from "Home Videos" libraries (e.g. "Full Bluray"),
        // which Jellyfin does not classify as "Movie"
        query.addQueryItem("IncludeItemTypes", "Movie,Series,Video");
        query.addQueryItem("Recursive", "true");
        query.addQueryItem("Limit", "50");
        if(withFields)
            query.addQueryItem("Fields", "Overview,ProductionYear,UserData");
        if(!parentId.isEmpty())
            query.addQueryItem("ParentId", parentId);

        QUrl requestUrl(url + "/Users/" + userId + "/Items");
        requestUrl.setQuery(query);

        QNetworkRequest request = BuildRequest(requestUrl);
        request.setRawHeader("X-Emby-Token", accessToken.toUtf8());
        return request;
    };

    auto allItems = QSharedPointer<QList<JellyfinItem>>::create();
    auto libraryByItem = QSharedPointer<QHash<QString, QString>>::create();
    auto pending = QSharedPointer<int>::create(1 + libraries.size());

    auto finish = [=]
    {
        if(--(*pending) != 0)
            return;

        QList<JellyfinItem> items = *allItems;
        for(auto &item : items)
            item.libraryName = libraryByItem->value(item.id);

        emit searchResultsSignal(items);
    };

    // global, unscoped search: source of truth for which items match,
    // regardless of whether their library is exposed via /Users/{userId}/Views
    QNetworkReply *globalReply = manager->get(buildRequest(QString(), true));
    connect(globalReply, &QNetworkReply::finished, this, [=]
    {
        if(globalReply->error() == QNetworkReply::NoError)
        {
            QJsonObject response = QJsonDocument::fromJson(globalReply->readAll()).object();
            for(auto entry : response["Items"].toArray())
            {
                QJsonObject obj = entry.toObject();
                JellyfinItem item;
                item.id = obj["Id"].toString();
                item.name = obj["Name"].toString();
                item.type = obj["Type"].toString();
                item.overview = obj["Overview"].toString();
                item.year = obj["ProductionYear"].toInt();
                item.isFavorite = obj["UserData"].toObject()["IsFavorite"].toBool();
                allItems->append(item);
            }
        }
        else
            emit connectionFailedSignal(globalReply->errorString());

        globalReply->deleteLater();
        finish();
    });

    // per-library searches: best-effort lookup used only to label results
    // with the name of the library they belong to
    for(auto it = libraries.constBegin(); it != libraries.constEnd(); ++it)
    {
        QString libraryName = it.value();
        QNetworkReply *reply = manager->get(buildRequest(it.key(), false));
        connect(reply, &QNetworkReply::finished, this, [=]
        {
            if(reply->error() == QNetworkReply::NoError)
            {
                QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
                for(auto entry : response["Items"].toArray())
                    (*libraryByItem)[entry.toObject()["Id"].toString()] = libraryName;
            }
            reply->deleteLater();
            finish();
        });
    }
}

void JellyfinManager::GetSeasons(const QString &seriesId)
{
    if(accessToken.isEmpty())
    {
        emit connectionFailedSignal(tr("Not connected."));
        return;
    }

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("userId", userId);
    query.addQueryItem("Fields", "Overview");

    QUrl requestUrl(url + "/Shows/" + seriesId + "/Seasons");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        QList<JellyfinItem> items;
        for(auto entry : response["Items"].toArray())
        {
            QJsonObject obj = entry.toObject();
            JellyfinItem item;
            item.id = obj["Id"].toString();
            item.name = obj["Name"].toString();
            item.type = obj["Type"].toString();
            item.overview = obj["Overview"].toString();
            item.index = obj["IndexNumber"].toInt();
            items.append(item);
        }

        emit seasonsResultsSignal(seriesId, items);
        reply->deleteLater();
    });
}

void JellyfinManager::GetEpisodes(const QString &seriesId, const QString &seasonId)
{
    if(accessToken.isEmpty())
    {
        emit connectionFailedSignal(tr("Not connected."));
        return;
    }

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("seasonId", seasonId);
    query.addQueryItem("userId", userId);
    query.addQueryItem("Fields", "Overview");

    QUrl requestUrl(url + "/Shows/" + seriesId + "/Episodes");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        QList<JellyfinItem> items;
        for(auto entry : response["Items"].toArray())
        {
            QJsonObject obj = entry.toObject();
            JellyfinItem item;
            item.id = obj["Id"].toString();
            item.name = obj["Name"].toString();
            item.type = obj["Type"].toString();
            item.overview = obj["Overview"].toString();
            item.seriesName = obj["SeriesName"].toString();
            item.parentIndex = obj["ParentIndexNumber"].toInt();
            item.index = obj["IndexNumber"].toInt();
            items.append(item);
        }

        emit episodesResultsSignal(seasonId, items);
        reply->deleteLater();
    });
}

void JellyfinManager::GetGenres()
{
    if(accessToken.isEmpty())
    {
        emit connectionFailedSignal(tr("Not connected."));
        return;
    }

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("userId", userId);
    query.addQueryItem("IncludeItemTypes", "Movie,Series");
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("SortBy", "SortName");

    QUrl requestUrl(url + "/Genres");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        QList<JellyfinItem> items;
        for(auto entry : response["Items"].toArray())
        {
            QJsonObject obj = entry.toObject();
            JellyfinItem item;
            item.id = obj["Id"].toString();
            item.name = obj["Name"].toString();
            item.type = obj["Type"].toString();
            items.append(item);
        }

        emit genresResultsSignal(items);
        reply->deleteLater();
    });
}

void JellyfinManager::GetItemsByGenre(const QString &genreId)
{
    if(accessToken.isEmpty())
    {
        emit connectionFailedSignal(tr("Not connected."));
        return;
    }

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("GenreIds", genreId);
    query.addQueryItem("IncludeItemTypes", "Movie,Series");
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("Fields", "Overview,ProductionYear");
    query.addQueryItem("SortBy", "SortName");

    QUrl requestUrl(url + "/Users/" + userId + "/Items");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        QList<JellyfinItem> items;
        for(auto entry : response["Items"].toArray())
        {
            QJsonObject obj = entry.toObject();
            JellyfinItem item;
            item.id = obj["Id"].toString();
            item.name = obj["Name"].toString();
            item.type = obj["Type"].toString();
            item.overview = obj["Overview"].toString();
            item.year = obj["ProductionYear"].toInt();
            items.append(item);
        }

        emit genreItemsResultsSignal(genreId, items);
        reply->deleteLater();
    });
}

QString JellyfinManager::getNowPlayingTitle(const QString &currentUrl) const
{
    if(currentUrl == nowPlayingUrl)
        return nowPlayingTitle;

    for(const auto &episode : nowPlayingEpisodes)
    {
        if(GetStreamUrl(episode.id) == currentUrl)
        {
            return QString("%0: %1 - S%2E%3 - %4")
                    .arg(getServerName(), episode.seriesName)
                    .arg(episode.parentIndex, 2, 10, QChar('0'))
                    .arg(episode.index, 2, 10, QChar('0'))
                    .arg(episode.name);
        }
    }

    return QString();
}

void JellyfinManager::PlayMovie(const JellyfinItem &item, int transcodeHeight)
{
    transcodeMode        = transcodeHeight > 0;
    this->transcodeHeight = transcodeHeight;
    if(transcodeMode)
    {
        transcodeSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        nounours->mpv->SuppressHwdec();

        // Register keepalive session BEFORE loading the HLS URL.
        // If we wait for UpdatePlaybackState (fired by mpv's file-loaded event),
        // ReportPlaybackStart arrives while FFmpeg is already initialising and
        // causes Jellyfin to reset the transcoding job, failing even segment 0.
        if(!activePlaybackItemId.isEmpty())
            ReportPlaybackStopped();
        activePlaybackItemId = item.id;
        playSessionId        = transcodeSessionId;
        activePlaybackPaused = false;
        playbackProgressTimer->stop();
        ReportPlaybackStart(item.id);
        playbackProgressTimer->start();
    }
    else
    {
        nounours->mpv->ResumeHwdec();
    }

    QString url = transcodeMode ? GetTranscodeUrl(item.id) : GetStreamUrl(item.id);
    QString label = item.name;
    if(item.year > 0)
        label += QString(" (%0)").arg(item.year);

    SetNowPlaying(url, QString("%0: %1").arg(getServerName(), label));
    nowPlayingEpisodes.clear();

    nounours->mpv->LoadUrlPlaylist({url}, {label}, 0, transcodeMode ? kTranscodeFileOpts : QString{});
}

void JellyfinManager::PlayEpisodes(const QString &seriesId, const QList<JellyfinItem> &seasons, int seasonIndex,
                                    const QList<JellyfinItem> &episodes, int episodeIndex, int transcodeHeight)
{
    transcodeMode        = transcodeHeight > 0;
    this->transcodeHeight = transcodeHeight;
    if(transcodeMode)
    {
        transcodeSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        nounours->mpv->SuppressHwdec();

        if(!activePlaybackItemId.isEmpty())
            ReportPlaybackStopped();
        activePlaybackItemId = episodes.isEmpty() ? QString() : episodes.first().id;
        playSessionId        = transcodeSessionId;
        activePlaybackPaused = false;
        playbackProgressTimer->stop();
        if(!activePlaybackItemId.isEmpty())
            ReportPlaybackStart(activePlaybackItemId);
        playbackProgressTimer->start();
    }
    else
    {
        nounours->mpv->ResumeHwdec();
    }

    nowPlayingSeriesId = seriesId;
    nowPlayingSeasons = seasons;
    nowPlayingSeasonIndex = seasonIndex;
    nowPlayingEpisodes = episodes;
    pendingNextSeasonIndex = -1;

    QStringList urls, labels;
    for(const auto &episode : episodes)
    {
        urls << (transcodeMode ? GetTranscodeUrl(episode.id) : GetStreamUrl(episode.id));
        labels << QString("%0. %1").arg(episode.index).arg(episode.name);
    }

    nounours->mpv->LoadUrlPlaylist(urls, labels, episodeIndex, transcodeMode ? kTranscodeFileOpts : QString{});
}

bool JellyfinManager::PlayNextSeason()
{
    if(nowPlayingSeasonIndex < 0 || nowPlayingSeasonIndex + 1 >= nowPlayingSeasons.size())
        return false;

    pendingNextSeasonIndex = nowPlayingSeasonIndex + 1;
    GetEpisodes(nowPlayingSeriesId, nowPlayingSeasons[pendingNextSeasonIndex].id);
    return true;
}

QString JellyfinManager::ItemIdFromUrl(const QString &url)
{
    static const QRegularExpression re("/Videos/([^/]+)/stream");
    const QRegularExpressionMatch match = re.match(url);
    return match.hasMatch() ? match.captured(1) : QString();
}

void JellyfinManager::PostSessionEvent(const QString &endpoint, QJsonObject body)
{
    if(accessToken.isEmpty())
        return;

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QNetworkRequest request = BuildRequest(QUrl(url + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    body["PlaySessionId"] = playSessionId;
    body["IsPaused"] = activePlaybackPaused;
    body["IsMuted"] = nounours->mpv->getMute();
    body["VolumeLevel"] = nounours->mpv->getVolume();
    body["PositionTicks"] = qint64(nounours->mpv->getTime()) * 10000000LL;
    body["CanSeek"] = true;
    body["PlayMethod"] = transcodeMode ? "Transcode" : "DirectStream";

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void JellyfinManager::ReportPlaybackStart(const QString &itemId)
{
    PostSessionEvent("/Sessions/Playing", {{"ItemId", itemId}});
}

void JellyfinManager::ReportPlaybackProgress()
{
    if(activePlaybackItemId.isEmpty())
        return;

    PostSessionEvent("/Sessions/Playing/Progress", {{"ItemId", activePlaybackItemId}});
}

void JellyfinManager::ReportPlaybackStopped()
{
    PostSessionEvent("/Sessions/Playing/Stopped", {{"ItemId", activePlaybackItemId}});
}

void JellyfinManager::UpdatePlaybackState(const QString &url, int playState)
{
    QString itemId = ItemIdFromUrl(url);

    // In transcode mode mpv reports .m3u8 / HLS segment URLs that don't match the
    // /stream pattern, so itemId is empty even though we have an active session.
    // Ignore these non-Jellyfin URLs to avoid tearing down the keepalive we set up
    // in PlayMovie/PlayEpisodes before the HLS URL was loaded.
    if(itemId.isEmpty() && !url.isEmpty() && transcodeMode && !activePlaybackItemId.isEmpty())
        return;

    if(itemId != activePlaybackItemId)
    {
        if(!activePlaybackItemId.isEmpty())
            ReportPlaybackStopped();

        activePlaybackItemId = itemId;
        playbackProgressTimer->stop();

        if(activePlaybackItemId.isEmpty())
            return;

        // For transcode sessions the PlaySessionId must match the one in the HLS URL;
        // otherwise Jellyfin discards progress reports and kills the transcoder.
        playSessionId = (transcodeMode && !transcodeSessionId.isEmpty())
                        ? transcodeSessionId
                        : QUuid::createUuid().toString(QUuid::WithoutBraces);
        activePlaybackPaused = (playState == Mpv::Paused);
        ReportPlaybackStart(activePlaybackItemId);
        playbackProgressTimer->start();
        return;
    }

    if(activePlaybackItemId.isEmpty())
        return;

    if(playState == Mpv::Stopped || playState == Mpv::Idle)
    {
        ReportPlaybackStopped();
        activePlaybackItemId.clear();
        playbackProgressTimer->stop();
        return;
    }

    bool paused = (playState == Mpv::Paused);
    if(paused != activePlaybackPaused)
    {
        activePlaybackPaused = paused;
        ReportPlaybackProgress();
    }
}

void JellyfinManager::ReportCapabilities()
{
    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QNetworkRequest request = BuildRequest(QUrl(url + "/Sessions/Capabilities/Full"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QJsonObject body;
    body["PlayableMediaTypes"] = QJsonArray{"Video", "Audio"};
    body["SupportedCommands"] = QJsonArray{
        "VolumeUp", "VolumeDown", "SetVolume", "Mute", "Unmute", "ToggleMute", "DisplayMessage"
    };
    body["SupportsMediaControl"] = true;

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void JellyfinManager::ConnectWebSocket()
{
    if(accessToken.isEmpty())
        return;

    QUrl url(serverUrl);
    url.setScheme(url.scheme() == "https" ? "wss" : "ws");

    QString path = url.path();
    if(path.endsWith('/'))
        path.chop(1);
    url.setPath(path + "/socket");

    QUrlQuery query;
    query.addQueryItem("api_key", accessToken);
    query.addQueryItem("deviceId", deviceId);
    url.setQuery(query);

    socket->open(BuildRequest(url));
}

void JellyfinManager::HandleSocketMessage(const QString &message)
{
    QJsonObject json = QJsonDocument::fromJson(message.toUtf8()).object();
    QString type = json["MessageType"].toString();

    if(type == "ForceKeepAlive")
        socket->sendTextMessage(QStringLiteral("{\"MessageType\":\"KeepAlive\"}"));
    else if(type == "GeneralCommand")
        HandleGeneralCommand(json["Data"].toObject());
    else if(type == "Playstate")
        HandlePlaystateCommand(json["Data"].toObject());
    else if(type == "Play")
        HandlePlayCommand(json["Data"].toObject());
}

void JellyfinManager::HandleGeneralCommand(const QJsonObject &data)
{
    QString name = data["Name"].toString();
    QJsonObject args = data["Arguments"].toObject();

    if(name == "DisplayMessage")
    {
        QString header = args["Header"].toString();
        QString text = args["Text"].toString();
        int timeoutMs = args["TimeoutMs"].toString().toInt();

        // Jellyfin's "Send Message" dialog defaults TimeoutMs to 5000, which
        // feels too short here, so enforce a longer minimum
        nounours->mpv->ShowMessage(header.isEmpty() ? text : QString("%0: %1").arg(header, text),
                                    qMax(timeoutMs, 15000));
    }
    else if(name == "SetVolume")
        nounours->mpv->Volume(args["Volume"].toString().toInt(), true);
    else if(name == "VolumeUp")
        nounours->mpv->Volume(qMin(nounours->mpv->getVolume() + 5, 100), true);
    else if(name == "VolumeDown")
        nounours->mpv->Volume(qMax(nounours->mpv->getVolume() - 5, 0), true);
    else if(name == "Mute")
        nounours->mpv->Mute(true);
    else if(name == "Unmute")
        nounours->mpv->Mute(false);
    else if(name == "ToggleMute")
        nounours->mpv->Mute(!nounours->mpv->getMute());
}

void JellyfinManager::HandlePlaystateCommand(const QJsonObject &data)
{
    QString command = data["Command"].toString();

    if(command == "PlayPause")
        nounours->mpv->PlayPause(QString());
    else if(command == "Pause")
        nounours->mpv->Pause();
    else if(command == "Unpause")
        nounours->mpv->Play();
    else if(command == "Stop")
        nounours->mpv->Stop();
    else if(command == "Seek")
        nounours->mpv->Seek(int(data["SeekPositionTicks"].toDouble() / 10000000.0), false, true);
}

void JellyfinManager::GetFavorites()
{
    if(accessToken.isEmpty())
        return;

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("Filters", "IsFavorite");
    query.addQueryItem("IncludeItemTypes", "Movie,Series,Video");
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("Fields", "Overview,ProductionYear");
    query.addQueryItem("SortBy", "SortName");

    QUrl requestUrl(url + "/Users/" + userId + "/Items");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() != QNetworkReply::NoError)
        {
            emit connectionFailedSignal(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        QList<JellyfinItem> items;
        for(auto entry : response["Items"].toArray())
        {
            QJsonObject obj = entry.toObject();
            JellyfinItem item;
            item.id = obj["Id"].toString();
            item.name = obj["Name"].toString();
            item.type = obj["Type"].toString();
            item.overview = obj["Overview"].toString();
            item.year = obj["ProductionYear"].toInt();
            item.isFavorite = true;
            items.append(item);
        }

        emit favoritesResultsSignal(items);
        reply->deleteLater();
    });
}

void JellyfinManager::ToggleFavorite(const QString &itemId, bool setFavorite)
{
    if(accessToken.isEmpty() || itemId.isEmpty())
        return;

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QNetworkRequest request = BuildRequest(QUrl(url + "/Users/" + userId + "/FavoriteItems/" + itemId));
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = setFavorite
        ? manager->post(request, QByteArray())
        : manager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() == QNetworkReply::NoError)
            emit favoriteToggledSignal(itemId, setFavorite);
        reply->deleteLater();
    });
}

void JellyfinManager::FetchImage(const QString &itemId, int maxHeight)
{
    if(accessToken.isEmpty() || itemId.isEmpty())
        return;

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrl imgUrl(QString("%0/Items/%1/Images/Primary?fillHeight=%2&quality=90&api_key=%3")
                .arg(url, itemId, QString::number(maxHeight), accessToken));

    QNetworkReply *reply = manager->get(BuildRequest(imgUrl));
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        QPixmap pixmap;
        if(reply->error() == QNetworkReply::NoError)
            pixmap.loadFromData(reply->readAll());
        emit imageReadySignal(itemId, pixmap);
        reply->deleteLater();
    });
}

void JellyfinManager::HandlePlayCommand(const QJsonObject &data)
{
    QStringList ids;
    for(const auto &id : data["ItemIds"].toArray())
        ids << id.toString();

    if(ids.isEmpty())
        return;

    QString url = serverUrl;
    if(url.endsWith('/'))
        url.chop(1);

    QUrlQuery query;
    query.addQueryItem("Ids", ids.join(','));
    query.addQueryItem("Fields", "ProductionYear");

    QUrl requestUrl(url + "/Users/" + userId + "/Items");
    requestUrl.setQuery(query);

    QNetworkRequest request = BuildRequest(requestUrl);
    request.setRawHeader("X-Emby-Token", accessToken.toUtf8());

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]
    {
        if(reply->error() == QNetworkReply::NoError)
        {
            QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
            QStringList urls, labels;
            for(auto entry : response["Items"].toArray())
            {
                QJsonObject obj = entry.toObject();
                QString label = obj["Name"].toString();
                int year = obj["ProductionYear"].toInt();
                if(year > 0)
                    label += QString(" (%0)").arg(year);

                urls << GetStreamUrl(obj["Id"].toString());
                labels << label;
            }

            if(!urls.isEmpty())
            {
                nowPlayingEpisodes.clear();
                SetNowPlaying(urls.first(), QString("%0: %1").arg(getServerName(), labels.first()));
                nounours->mpv->LoadUrlPlaylist(urls, labels, 0);
            }
        }
        reply->deleteLater();
    });
}
