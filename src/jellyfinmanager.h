#ifndef JELLYFINMANAGER_H
#define JELLYFINMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPixmap>
#include <QWebSocket>
#include <QString>
#include <QList>
#include <QHash>
#include <QUrl>
#include <QMetaType>
#include <QTimer>
#include <QJsonObject>

class NounoursEngine;

struct JellyfinItem
{
    QString id, name, type, overview, seriesName;
    QString libraryName; // name of the Jellyfin library (media folder) this item belongs to
    int year = 0;
    int parentIndex = 0; // season number, for episodes
    int index = 0;       // season or episode number
    bool isFavorite = false;
};

Q_DECLARE_METATYPE(JellyfinItem)

class JellyfinManager : public QObject
{
friend class NounoursEngine;
    Q_OBJECT
public:
    explicit JellyfinManager(QObject *parent = 0);
    ~JellyfinManager();

    QString getServerUrl() { return serverUrl; }
    QString getUsername()  { return username; }
    QString getPassword()  { return password; }
    bool    isConnected()  { return !accessToken.isEmpty(); }

    QString GetStreamUrl(const QString &itemId) const
    {
        return QString("%0/Videos/%1/stream?static=true&api_key=%2").arg(serverUrl, itemId, accessToken);
    }

    QString GetTranscodeUrl(const QString &itemId) const
    {
        // HLS endpoint — always transcodes.
        // PlaySessionId must be generated before this URL is built; without it Jellyfin
        // cannot create the FFmpeg job and all segments return HTTP 500.
        // AllowVideoStreamCopy=false forces actual re-encoding (not just remuxing).
        // MaxWidth + MaxHeight together constrain to a 16:9 bounding box of the chosen
        // height, preserving source aspect ratio (e.g. 2.4:1 source at 720p → 1280×533).
        // VideoBitrate must be sized to the target resolution: Jellyfin uses it to pick
        // the output quality tier, so 4 Mbps forces 720p even when MaxHeight=1080.
        const int maxW        = transcodeHeight * 16 / 9;
        const int videoBitrate = transcodeHeight >= 1080 ? 8000000 : 4000000;
        return QString("%0/Videos/%1/master.m3u8"
                       "?DeviceId=%2&MediaSourceId=%1"
                       "&PlaySessionId=%3"
                       "&VideoCodec=h264&AudioCodec=aac"
                       "&AudioBitrate=320000&MaxAudioChannels=6"
                       "&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false"
                       "&api_key=%4")
               .arg(serverUrl, itemId, deviceId, transcodeSessionId, accessToken)
               + QString("&VideoBitrate=%1&MaxWidth=%2&MaxHeight=%3")
                 .arg(videoBitrate).arg(maxW).arg(transcodeHeight);
    }

    // Converts a Jellyfin HLS transcode URL to the equivalent direct-stream URL so
    // that recent-file entries are always replayable without a live transcode session.
    QString DirectUrlForRecent(const QString &url) const
    {
        static const QRegularExpression re(R"(/Videos/([^/?]+)/master\.m3u8)");
        const QRegularExpressionMatch m = re.match(url);
        return m.hasMatch() ? GetStreamUrl(m.captured(1)) : url;
    }

    QString getServerName() const { return serverName.isEmpty() ? QUrl(serverUrl).host() : serverName; }
    QString getNowPlayingTitle(const QString &currentUrl) const;

public slots:
    void ServerUrl(QString s) { serverUrl = s; }
    void Username(QString s)  { username = s; }
    void Password(QString s)  { password = s; }

    void Connect();
    void Connect(const QString &url, const QString &user, const QString &pass);
    void Search(const QString &term);
    void GetSeasons(const QString &seriesId);
    void GetEpisodes(const QString &seriesId, const QString &seasonId);
    void GetGenres();
    void GetItemsByGenre(const QString &genreId);
    void SetNowPlaying(QString url, QString title) { nowPlayingUrl = url; nowPlayingTitle = title; }

    void PlayMovie(const JellyfinItem &item, int transcodeHeight = 0);
    void PlayEpisodes(const QString &seriesId, const QList<JellyfinItem> &seasons, int seasonIndex,
                       const QList<JellyfinItem> &episodes, int episodeIndex, int transcodeHeight = 0);
    bool PlayNextSeason();

    void FetchImage(const QString &itemId, int maxHeight = 300);
    void GetFavorites();
    void ToggleFavorite(const QString &itemId, bool setFavorite);

    // reports the current playback state to the Jellyfin server so it shows up
    // in the server's "Now Playing" activity. playState matches Mpv::PlayState.
    void UpdatePlaybackState(const QString &url, int playState);

signals:
    void connectedSignal();
    void connectionFailedSignal(QString error);
    void searchResultsSignal(QList<JellyfinItem> items);
    void seasonsResultsSignal(QString seriesId, QList<JellyfinItem> items);
    void episodesResultsSignal(QString seasonId, QList<JellyfinItem> items);
    void genresResultsSignal(QList<JellyfinItem> items);
    void genreItemsResultsSignal(QString genreId, QList<JellyfinItem> items);
    void imageReadySignal(QString itemId, QPixmap pixmap);
    void favoritesResultsSignal(QList<JellyfinItem> items);
    void favoriteToggledSignal(QString itemId, bool isFavorite);
    void messageSignal(QString msg);

private:
    NounoursEngine *nounours;
    QNetworkAccessManager *manager;

    QString serverUrl, username, password;
    QString accessToken, userId, deviceId;
    QString serverName;
    QString nowPlayingUrl, nowPlayingTitle;
    QList<JellyfinItem> nowPlayingEpisodes;

    // currently playing episode/season context, used for season rollover
    QString nowPlayingSeriesId;
    QList<JellyfinItem> nowPlayingSeasons;
    int nowPlayingSeasonIndex = -1;
    int pendingNextSeasonIndex = -1;

    bool    transcodeMode      = false;
    int     transcodeHeight    = 0;       // target height in pixels (720 or 1080); 0 = no transcode
    QString transcodeSessionId;           // UUID generated per transcode session, used in HLS URL

    // Jellyfin "now playing" session reporting
    QString activePlaybackItemId, playSessionId;
    bool activePlaybackPaused = false;
    QTimer *playbackProgressTimer;

    // Jellyfin remote-control: WebSocket session used to receive
    // play/pause/stop/seek/volume/message commands sent from the server
    QWebSocket *socket;

    QString AuthHeader() const;
    static QNetworkRequest BuildRequest(const QUrl &url);
    void FetchServerName();
    void SearchAllLibraries(const QString &term, const QHash<QString, QString> &libraries);
    static QString ItemIdFromUrl(const QString &url);
    void PostSessionEvent(const QString &endpoint, QJsonObject body);
    void ReportPlaybackStart(const QString &itemId);
    void ReportPlaybackProgress();
    void ReportPlaybackStopped();

    void ReportCapabilities();
    void ConnectWebSocket();
    void HandleSocketMessage(const QString &message);
    void HandleGeneralCommand(const QJsonObject &data);
    void HandlePlaystateCommand(const QJsonObject &data);
    void HandlePlayCommand(const QJsonObject &data);
};

#endif // JELLYFINMANAGER_H
