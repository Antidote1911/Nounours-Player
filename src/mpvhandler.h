#ifndef MPVHANDLER_H
#define MPVHANDLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>

#include <mpv/client.h>

#include "mpvtypes.h"

class MediaInfoHelper;

#define MPV_REPLY_COMMAND 1
#define MPV_REPLY_PROPERTY 2

class NounoursEngine;

class MpvHandler : public QObject
{
friend class NounoursEngine;
    Q_OBJECT
public:
    explicit MpvHandler(int64_t wid, QObject *parent = 0);
    ~MpvHandler();

    void Initialize();
    const Mpv::FileInfo &getFileInfo()      { return fileInfo; }
    Mpv::PlayState getPlayState()           { return playState; }
    QString getFile()                       { return file; }
    QString getPath()                       { return path; }
    QString getScreenshotFormat()           { return screenshotFormat; }
    QString getScreenshotTemplate()         { return screenshotTemplate; }
    QString getScreenshotDir()              { return screenshotDir; }
    QString getVo()                         { return vo; }
    QString getMsgLevel()                   { return msgLevel; }
    QString getHwdec()                      { return hwdec; }
    QString getFramedrop()                  { return framedrop; }
    QString getSkipLoopFilter()             { return skipLoopFilter; }
    int getVdLavcThreads()                  { return vdLavcThreads; }
    double getSpeed()                       { return speed; }
    int getTime()                           { return time; }
    int getVolume()                         { return volume; }
    int getAid()                            { return aid; }
    int getSid()                            { return sid; }
    bool getSubtitleVisibility()            { return subtitleVisibility; }
    bool getMute()                          { return mute; }

    int getBrightness()                     { return brightness; }
    int getContrast()                       { return contrast; }
    int getSaturation()                     { return saturation; }
    int getGamma()                          { return gamma_; }
    int getHue()                            { return hue; }
    bool getEqEnabled()                     { return eqEnabled; }

    QString getMediaInfo();
    QString getMediaInfoFull();
    QString getMpvVersion();
    const MediaInfoHelper *getMediaInfoHelper() const { return miHelper.get(); }

protected:
    virtual bool event(QEvent*);

public slots:
    void LoadFile(QString);
    QString LoadPlaylist(QString);
    bool PlayFile(QString, const QString &perFileOptions = {});
    void LoadUrlPlaylist(const QStringList &urls, const QStringList &labels, int startIndex,
                         const QString &perFileOptions = {});

    void AddOverlay(int id, int x, int y, QString file, int offset, int w, int h);
    void RemoveOverlay(int id);

    void Play();
    void Pause();
    void Stop();
    void PlayPause(QString fileIfStopped);
    void Restart();
    void Rewind();
    void Mute(bool);

    void Seek(int pos, bool relative = false, bool osd = false);
    int Relative(int pos);

    void Volume(int, bool osd = false);
    void Speed(double);
    void Aid(int);
    void Sid(int);

    void Brightness(int);
    void Contrast(int);
    void Saturation(int);
    void Gamma(int);
    void Hue(int);
    void EqEnabled(bool);

    void Screenshot(bool withSubs = false);

    void ScreenshotFormat(QString);
    void ScreenshotTemplate(QString);
    void ScreenshotDirectory(QString);

    void AddSubtitleTrack(QString);
    void AddAudioTrack(QString);
    void ShowSubtitles(bool);

    void Deinterlace(bool);
    void Interpolate(bool);
    void Vo(QString);

    void MsgLevel(QString level);

    void Hwdec(QString);
    void SuppressHwdec();  // force hwdec=no without altering the user's stored setting
    void ResumeHwdec();    // restore hwdec to the user's stored setting
    void Framedrop(QString);
    void SkipLoopFilter(QString);
    void VdLavcThreads(int);

    void ShowText(QString text, int duration = 4000);
    void ShowMessage(QString text, int duration = 15000);

    void LoadTracks();
    void LoadChapters();
    void LoadVideoParams();
    void LoadAudioParams();
    void LoadMetadata();

    void Command(const QStringList &strlist);
    void SetOption(QString key, QString val);

protected slots:
    void OpenFile(QString, const QString &perFileOptions = {});
    QString PopulatePlaylist();
    void LoadFileInfo();
    void SetProperties();

    void AsyncCommand(const char *args[]);
    void Command(const char *args[]);
    void HandleErrorCode(int);

private slots:
    void setPlaylist(const QStringList& l)  { emit playlistChanged(l, QStringList()); }
    void setPlaylist(const QStringList& l, const QStringList& labels) { emit playlistChanged(l, labels); }
    void setFileInfo()                      { emit fileInfoChanged(fileInfo); }
    void setPlayState(Mpv::PlayState s)     { emit playStateChanged(playState = s); }
    void setFile(QString s)                 { emit fileChanged(file = s); }
    void setPath(QString s)                 { emit pathChanged(path = s); }
    void setScreenshotFormat(QString s)     { emit screenshotFormatChanged(screenshotFormat = s); }
    void setScreenshotTemplate(QString s)   { emit screenshotTemplateChanged(screenshotTemplate = s); }
    void setScreenshotDir(QString s)        { emit screenshotDirChanged(screenshotDir = s); }
    void setVo(QString s)                   { emit voChanged(vo = s); }
    void setMsgLevel(QString s)             { emit msgLevelChanged(msgLevel = s); }
    void setHwdec(QString s)                { emit hwdecChanged(hwdec = s); }
    void setFramedrop(QString s)            { emit framedropChanged(framedrop = s); }
    void setSkipLoopFilter(QString s)       { emit skipLoopFilterChanged(skipLoopFilter = s); }
    void setVdLavcThreads(int i)            { emit vdLavcThreadsChanged(vdLavcThreads = i); }
    void setSpeed(double d)                 { emit speedChanged(speed = d); }
    void setTime(int i)                     { emit timeChanged(time = i); }
    void setVolume(int i)                   { emit volumeChanged(volume = i); }
    void setAid(int i)                      { emit aidChanged(aid = i); }
    void setSid(int i)                      { emit sidChanged(sid = i); }
    void setSubtitleVisibility(bool b)      { emit subtitleVisibilityChanged(subtitleVisibility = b); }
    void setMute(bool b)                    { if(mute != b) emit muteChanged(mute = b); }
    void setEqEnabled(bool b)               { emit eqEnabledChanged(eqEnabled = b); }

signals:
    void playlistChanged(const QStringList&, const QStringList&);
    void fileInfoChanged(const Mpv::FileInfo&);
    void trackListChanged(const QList<Mpv::Track>&);
    void chaptersChanged(const QList<Mpv::Chapter>&);
    void videoParamsChanged(const Mpv::VideoParams&);
    void audioParamsChanged(const Mpv::AudioParams&);
    void playStateChanged(Mpv::PlayState);
    void fileChanging(int, int);
    void fileChanged(QString);
    void pathChanged(QString);
    void screenshotFormatChanged(QString);
    void screenshotTemplateChanged(QString);
    void screenshotDirChanged(QString);
    void voChanged(QString);
    void msgLevelChanged(QString);
    void hwdecChanged(QString);
    void framedropChanged(QString);
    void skipLoopFilterChanged(QString);
    void vdLavcThreadsChanged(int);
    void speedChanged(double);
    void timeChanged(int);
    void volumeChanged(int);
    void aidChanged(int);
    void sidChanged(int);
    void debugChanged(bool);
    void subtitleVisibilityChanged(bool);
    void muteChanged(bool);
    void eqEnabledChanged(bool);

    void messageSignal(QString m);

private:
    NounoursEngine *nounours;
    mpv_handle *mpv = nullptr;

    // variables
    Mpv::PlayState playState = Mpv::Idle;
    Mpv::FileInfo fileInfo;
    QString     file,
                path,
                screenshotFormat,
                screenshotTemplate,
                screenshotDir,
                suffix,
                vo,
                msgLevel,
                hwdec,
                framedrop,
                skipLoopFilter;
    double      speed = 1;
    int         time = 0,
                lastTime = 0,
                volume = 100,
                aid,
                sid,
                brightness = 0,
                contrast = 0,
                saturation = 0,
                gamma_ = 0,
                hue = 0,
                vdLavcThreads = 0;
    std::unique_ptr<MediaInfoHelper> miHelper;
    QTimer     *bufferTimer = nullptr;
    bool        init = false,
                playlistVisible = false,
                subtitleVisibility = true,
                mute = false,
                eqEnabled = true;
};

#endif // MPVHANDLER_H
