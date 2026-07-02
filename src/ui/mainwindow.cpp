#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <algorithm>
#include <QtMath>
#include <QLibraryInfo>
#include <QMimeData>
#include <QToolButton>
#include <QPointingDevice>
#include <QPainter>

#include "nounoursengine.h"
#include "mpvhandler.h"
#include "gesturehandler.h"
#include "overlayhandler.h"
#include "util.h"
#include "widgets/dimdialog.h"
#include "inputdialog.h"
#include "screenshotdialog.h"
#include "videoequalizer.h"
#include "jellyfinsearchdialog.h"
#include "../mediainfohelper.h"

// Generate a round icon with the playlist count painted inside (same circle style as the SVG).
static QIcon playlistCountIcon(int count, const QSize &iconSize)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap px(iconSize * dpr);
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0xF5, 0xF5, 0xF5)); // whitesmoke — same as the SVG circle
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(QPointF(0, 0), QSizeF(iconSize)));
    p.setPen(Qt::black);
    QFont f;
    f.setBold(true);
    f.setPixelSize(qRound(iconSize.height() * (count < 100 ? 0.44 : 0.34)));
    p.setFont(f);
    p.drawText(QRectF(QPointF(0, 0), QSizeF(iconSize)), Qt::AlignCenter,
               count < 100 ? QString::number(count) : "99+");
    return QIcon(px);
}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
#if defined(Q_OS_UNIX) || defined(Q_OS_LINUX)
    // update streaming support disabled on unix platforms
    ui->actionUpdate_Streaming_Support->setEnabled(false);
#endif
    ShowPlaylist(false);
    addActions(ui->menubar->actions()); // makes menubar shortcuts work even when menubar is hidden

    // initialize managers/handlers
    nounours = new NounoursEngine(this);
    mpv = nounours->mpv;

    ui->playlistWidget->AttachEngine(nounours);
    ui->mpvFrame->installEventFilter(this); // capture events on mpvFrame in the eventFilter function
    ui->mpvFrame->setMouseTracking(true);
    autohide = new QTimer(this);

    logoLabel = new QLabel(ui->mpvFrame);
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->show();
    ui->menubar->removeAction(ui->menuSubtitle_Track->menuAction());
    ui->menubar->removeAction(ui->menuAudio_Tracks->menuAction());
    ui->menubar->removeAction(ui->menu_Help->menuAction());
    helpBtn = new QToolButton(ui->menubar);
    helpBtn->setText(ui->menu_Help->title());
    helpBtn->setMenu(ui->menu_Help);
    helpBtn->setPopupMode(QToolButton::InstantPopup);
    helpBtn->setLayoutDirection(Qt::RightToLeft);
    helpBtn->setStyleSheet("QToolButton { background: transparent; color: #cccccc; padding: 4px 8px; border: none; }"
                           "QToolButton:hover, QToolButton:pressed { background: #3a3a3a; color: white; }");
    ui->menubar->setCornerWidget(helpBtn, Qt::TopRightCorner);

    // command action mappings (action (right) performs command (left))
    commandActionMap = {
        {"mpv add chapter +1", ui->action_Next_Chapter},
        {"mpv add chapter -1", ui->action_Previous_Chapter},
        {"mpv set sub-scale 1", ui->action_Reset_Size},
        {"mpv add sub-scale +0.1", ui->action_Size},
        {"mpv add sub-scale -0.1", ui->actionS_ize},
        {"mpv set video-aspect -1", ui->action_Auto_Detect}, // todo: make these nounours-commands so we can output messages when they change
        {"mpv set video-aspect 16:9", ui->actionForce_16_9},
        {"mpv set video-aspect 2.35:1", ui->actionForce_2_35_1},
        {"mpv set video-aspect 4:3", ui->actionForce_4_3},
        {"mpv cycle sub-visibility", ui->actionShow_Subtitles},
        {"mpv set time-pos 0", ui->action_Restart},
        {"mpv frame-step", ui->action_Frame_Step},
        {"mpv frame-back-step", ui->actionFrame_Back_Step},
        {"deinterlace", ui->action_Deinterlace},
        {"interpolate", ui->action_Motion_Interpolation},
        {"mute", ui->action_Mute},
        {"screenshot subtitles", ui->actionWith_Subtitles},
        {"screenshot", ui->actionWithout_Subtitles},
        {"add_subtitles", ui->action_Add_Subtitle_File},
        {"add_audio", ui->action_Add_Audio_File},
        {"fitwindow", ui->action_To_Current_Size},
        {"fitwindow 50", ui->action50},
        {"fitwindow 75", ui->action75},
        {"fitwindow 100", ui->action100},
        {"fitwindow 150", ui->action150},
        {"fitwindow 200", ui->action200},
        {"fullscreen", ui->action_Full_Screen},
        {"hide_all_controls", ui->actionHide_All_Controls},
        {"jump", ui->action_Jump_to_Time},
        {"media_info", ui->actionMedia_Info},
        {"mediainfo_full", ui->actionMediaInfoFull},
        {"new", ui->action_New_Player},
        {"open", ui->action_Open_File},
        {"open_clipboard", ui->actionOpen_Path_from_Clipboard},
        {"open_location", ui->actionOpen_URL},
        {"playlist play +1", ui->actionPlay_Next_File},
        {"playlist play -1", ui->actionPlay_Previous_File},
        {"playlist repeat off", ui->action_Off},
        {"playlist repeat playlist", ui->action_Playlist},
        {"playlist repeat this", ui->action_This_File},
        {"playlist shuffle", ui->actionSh_uffle},
        {"playlist toggle", ui->action_Show_Playlist},
        {"playlist full", ui->action_Hide_Album_Art},
        {"dim", ui->action_Dim_Lights},
        {"play_pause", ui->action_Play},
        {"quit", ui->actionE_xit},
        {"show_in_folder", ui->actionShow_in_Folder},
        {"stop", ui->action_Stop},
        {"volume +5", ui->action_Increase_Volume},
        {"volume -5", ui->action_Decrease_Volume},
        {"speed +0.1", ui->action_Increase},
        {"speed -0.1", ui->action_Decrease},
        {"speed 1.0", ui->action_Reset},
        {"output", ui->actionShow_D_ebug_Output},
        {"preferences", ui->action_Preferences},
        {"online_help", ui->actionOnline_Help},
        {"update", ui->action_Check_for_Updates},
        {"update youtube-dl", ui->actionUpdate_Streaming_Support},
        {"about qt", ui->actionAbout_Qt},
        {"about", ui->actionAbout_Nounours_Player}
    };

    // map actions to commands
    for(auto action = commandActionMap.begin(); action != commandActionMap.end(); ++action)
    {
        const QString cmd = action.key();
        connect(*action, &QAction::triggered,
                [=] {
                    if(cmd == "stop") userStopped = true;
                    nounours->Command(cmd);
                });
    }

    // setup signals & slots

    // mainwindow
    connect(this, &MainWindow::langChanged,
            [=](QString lang)
            {
                if(lang == "auto") // fetch lang from locale
                    lang = QLocale::system().name();

                if(lang != "en")
                {
                    QTranslator *tmp;

                    // load the system translations provided by Qt
                    tmp = nounours->qtTranslator;
                    nounours->qtTranslator = new QTranslator();
                    static_cast<void>(nounours->qtTranslator->load(QString("qt_%0").arg(lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)));
                    qApp->installTranslator(nounours->qtTranslator);
                    if(tmp != nullptr)
                        delete tmp;

                    // load the application translations
                    tmp = nounours->translator;
                    nounours->translator = new QTranslator();
                    static_cast<void>(nounours->translator->load(QString("nounours-player_%0").arg(lang), NOUNOURS_PLAYER_LANG_PATH));
                    qApp->installTranslator(nounours->translator);
                    if(tmp != nullptr)
                        delete tmp;
                }
                else
                {
                    if(nounours->qtTranslator != nullptr)
                        qApp->removeTranslator(nounours->qtTranslator);
                    if(nounours->translator != nullptr)
                        qApp->removeTranslator(nounours->translator);
                }

                // save strings we want to keep
                QString title = windowTitle(),
                        duration = ui->durationLabel->text(),
                        remaining = ui->remainingLabel->text(),
                        index = ui->indexLabel->text();

                ui->retranslateUi(this);
                helpBtn->setText(ui->menu_Help->title());

                // reload strings we kept
                setWindowTitle(title);
                ui->durationLabel->setText(duration);
                ui->remainingLabel->setText(remaining);
                ui->indexLabel->setText(index);
            });

    connect(this, &MainWindow::onTopChanged,
            [=](QString onTop)
            {
                if(onTop == "never")
                    Util::SetAlwaysOnTop(winId(), false);
                else if(onTop == "always")
                    Util::SetAlwaysOnTop(winId(), true);
                else if(onTop == "playing" && mpv->getPlayState() > 0)
                    Util::SetAlwaysOnTop(winId(), true);
            });

    connect(this, &MainWindow::remainingChanged,
            [=]
            {
                SetRemainingLabels(mpv->getTime());
            });

    connect(this, &MainWindow::debugChanged,
            [=](bool b)
            {
                ui->actionShow_D_ebug_Output->setChecked(b);
                ui->verticalWidget->setVisible(b);
                mouseMoveEvent(new QMouseEvent(QMouseEvent::MouseMove,
                                               QCursor::pos(), QCursor::pos(),
                                               Qt::NoButton,Qt::NoButton,Qt::NoModifier,
                                               QPointingDevice::primaryPointingDevice()));
                if(b)
                    ui->inputLineEdit->setFocus();
            });

    connect(this, &MainWindow::hideAllControlsChanged,
            [=](bool b)
            {
                HideAllControls(b);
                blockSignals(true);
                ui->actionHide_All_Controls->setChecked(b);
                blockSignals(false);
            });

    connect(this, &MainWindow::leftClickPlayPauseChanged,
            [=](bool b)
            {
                if(b)
                    setContextMenuPolicy(Qt::ActionsContextMenu);
                else if(!isFullScreen())
                    setContextMenuPolicy(Qt::NoContextMenu);
            });

    connect(nounours->sysTrayIcon, &QSystemTrayIcon::activated,
            [=](QSystemTrayIcon::ActivationReason reason)
            {
                if(reason == QSystemTrayIcon::Trigger)
                {
                    if(!hidePopup)
                    {
                        if(mpv->getPlayState() == Mpv::Playing)
                            nounours->sysTrayIcon->showMessage("Nounours Player", tr("Playing"), QSystemTrayIcon::NoIcon, 4000);
                        else if(mpv->getPlayState() == Mpv::Paused)
                            nounours->sysTrayIcon->showMessage("Nounours Player", tr("Paused"), QSystemTrayIcon::NoIcon, 4000);
                    }
                    nounours->PlayPause();
                }

            });

    connect(autohide, &QTimer::timeout, // cursor autohide
            [=]
            {
                if(ui->mpvFrame->geometry().contains(ui->mpvFrame->mapFromGlobal(cursor().pos())))
                    setCursor(QCursor(Qt::BlankCursor));
                if(autohide)
                    autohide->stop();
            });

    // dimDialog
    connect(nounours->dimDialog, &DimDialog::visbilityChanged,
            [=](bool dim)
            {
                ui->action_Dim_Lights->setChecked(dim);
                if(dim)
                    Util::SetAlwaysOnTop(winId(), true);
                else if(onTop == "never" || (onTop == "playing" && mpv->getPlayState() > 0))
                    Util::SetAlwaysOnTop(winId(), false);
            });

    // mpv

    connect(mpv, &MpvHandler::playlistChanged,
            [=](const QStringList &list)
            {
                if(list.length() > 1)
                {
                    ui->actionSh_uffle->setEnabled(true);
                    ui->actionStop_after_Current->setEnabled(true);
                    if(showPlaylistOnLoad) {
                        ShowPlaylist(true);
                        showPlaylistOnLoad = false;
                    }
                }
                else
                {
                    ui->actionSh_uffle->setEnabled(false);
                    ui->actionStop_after_Current->setEnabled(false);
                }

                if(list.length() > 0)
                    ui->menuR_epeat->setEnabled(true);
                else
                    ui->menuR_epeat->setEnabled(false);

                if(list.length() > 1)
                    ui->playlistButton->setIcon(playlistCountIcon(list.length(), ui->playlistButton->iconSize()));
                else
                    ui->playlistButton->setIcon(QIcon(":/img/default_playlist.svg"));
                setMinimumWidth(ui->playbackLayoutWidget->sizeHint().width());
            });

    connect(mpv, &MpvHandler::fileInfoChanged,
            [=](const Mpv::FileInfo &fileInfo)
            {
                if(mpv->getPlayState() > 0)
                {
                    if(fileInfo.media_title == "")
                        setWindowTitle("Nounours Player");
                    else if(fileInfo.media_title == "-")
                        setWindowTitle("Nounours Player: stdin"); // todo: disable playlist?
                    // normal case: title already set with HDR info in trackListChanged

                    QString f = mpv->getFile(), file = mpv->getPath()+f;
                    // Transcode URLs contain an expiring PlaySessionId and cannot be
                    // replayed later; store the equivalent direct-stream URL instead.
                    QString storeFile = nounours->jellyfin->DirectUrlForRecent(file);
                    if(f != QString() && maxRecent > 0)
                    {
                        auto it = std::find_if(recent.begin(), recent.end(),
                            [&storeFile](const Recent &r){ return r.path == storeFile; });
                        if(it != recent.end())
                        {
                            int t = it->time;
                            if(t > 0 && resume)
                                mpv->Seek(t);
                            recent.erase(it);
                        }
                        if(recent.isEmpty() || recent.front().path != storeFile)
                        {
                            UpdateRecentFiles(); // update after initialization and only if the current file is different from the first recent
                            while(recent.length() > maxRecent-1)
                                recent.removeLast();
                            QString jellyfinTitle = nounours->jellyfin->getNowPlayingTitle(file);
                            recent.push_front(
                                Recent(storeFile,
                                       !jellyfinTitle.isEmpty() ? jellyfinTitle :
                                       (mpv->getPath() == QString() || !Util::IsValidFile(storeFile)) ?
                                           fileInfo.media_title : QString()));
                            current = &recent.front();
                        }
                    }

                    // reset speed if length isn't known and we have a streaming video
                    // todo: don't save this reset, put their speed back when a normal video comes on
                    // todo: disable speed alteration during streaming media
                    if(fileInfo.length == 0)
                        if(mpv->getSpeed() != 1)
                            mpv->Speed(1);

                    ui->seekBar->setTracking(fileInfo.length);

                    if(ui->actionMedia_Info->isChecked())
                        nounours->MediaInfo(true);

                    SetRemainingLabels(fileInfo.length);
                }
            });

    connect(mpv, &MpvHandler::trackListChanged,
            [=](const QList<Mpv::Track> &trackList)
            {
                if(mpv->getPlayState() > 0)
                {
                    QAction *action;
                    bool video = false,
                         albumArt = false;

                    ui->menuSubtitle_Track->clear();
                    ui->menuSubtitle_Track->addAction(ui->action_Add_Subtitle_File);
                    ui->menuAudio_Tracks->clear();
                    ui->menuAudio_Tracks->addAction(ui->action_Add_Audio_File);
                    const MediaInfoHelper *miHelper = mpv->getMediaInfoHelper();

                    // update window title with HDR tag
                    {
                        const QString &mediaTitle = mpv->getFileInfo().media_title;
                        if(mediaTitle != "" && mediaTitle != "-") {
                            QString jellyfinTitle = nounours->jellyfin->getNowPlayingTitle(mpv->getFile());
                            QString title = !jellyfinTitle.isEmpty() ?
                                        jellyfinTitle :
                                        QFileInfo(mpv->getFile()).completeBaseName();
                            QStringList tags;
                            if(miHelper && miHelper->isValid()) {
                                auto vi = miHelper->videoTrack();
                                if(!vi.codec.isEmpty())     tags << vi.codec;
                                if(!vi.hdrFormat.isEmpty()) tags << vi.hdrFormat;
                            }
                            if(!tags.isEmpty()) title += " [" + tags.join(" | ") + "]";
                            setWindowTitle(title);
                        }
                    }

                    int audioIdx = 0;
                    for(auto &track : trackList)
                    {
                        if(track.type == "sub")
                        {
                            QString subCodec = track.codec;
                            if(subCodec == "hdmv_pgs_subtitle") subCodec = "PGS";
                            else if(subCodec == "subrip")        subCodec = "SRT";
                            QString subLabel = QString("%0:").arg(track.id);
                            if(!track.lang.isEmpty()) subLabel += " " + track.lang.toUpper();
                            if(!subCodec.isEmpty())   subLabel += " " + subCodec;
                            if(track.forced)          subLabel += " (forced)";
                            if(track.external)        subLabel += " *";
                            if(!track.title.isEmpty()) subLabel += " (" + track.title + ")";
                            action = ui->menuSubtitle_Track->addAction(subLabel.simplified().replace("&", "&&"));
                            connect(action, &QAction::triggered,
                                    [=]
                                    {
                                        // basically, if you uncheck the selected subtitle id, we hide subtitles
                                        // when you check a subtitle id, we make sure subtitles are showing and set it
                                        if(mpv->getSid() == track.id)
                                        {
                                            if(mpv->getSubtitleVisibility())
                                            {
                                                mpv->ShowSubtitles(false);
                                                return;
                                            }
                                            else
                                                mpv->ShowSubtitles(true);
                                        }
                                        else if(!mpv->getSubtitleVisibility())
                                            mpv->ShowSubtitles(true);
                                        mpv->Sid(track.id);
                                        mpv->ShowText(QString("%0 %1: %2 (%3)").arg(tr("Sub"), QString::number(track.id), track.title, track.lang + (track.external ? "*" : "")));
                                    });
                        }
                        else if(track.type == "audio")
                        {
                            {
                                auto info = (miHelper && miHelper->isValid())
                                    ? miHelper->audioTrack(audioIdx)
                                    : MediaInfoHelper::fromMpvFallback(track.demux_channels, track.codec);
                                audioIdx++;
                                auto parts = MediaInfoHelper::formatAudioLabelParts(track.id, track.lang, track.title, info);
                                QString label = parts.sub.isEmpty()
                                    ? parts.main
                                    : parts.main + " (" + parts.sub + ")";
                                action = ui->menuAudio_Tracks->addAction(label.replace("&", "&&"));
                            }
                            connect(action, &QAction::triggered,
                                    [=]
                                    {
                                        if(mpv->getAid() != track.id) // don't allow selection of the same track
                                        {
                                            mpv->Aid(track.id);
                                            mpv->ShowText(QString("%0 %1: %2 (%3)").arg(tr("Audio"), QString::number(track.id), track.title, track.lang));
                                        }
                                        else
                                            action->setChecked(true); // recheck the track
                                    });
                        }
                        else if(track.type == "video") // video track
                        {
                            if(!track.albumart) // isn't album art
                                video = true;
                            else
                                albumArt = true;
                        }
                    }
                    if(video)
                    {
                        // if we were hiding album art, show it--we've gone to a video
                        if(ui->mpvFrame->styleSheet() != QString()) // remove filler album art
                            ui->mpvFrame->setStyleSheet("");
                        if(ui->action_Hide_Album_Art->isChecked())
                            HideAlbumArt(false);
                        ui->action_Hide_Album_Art->setEnabled(false);
                        ui->menuSubtitle_Track->setEnabled(true);
                        if(ui->menuSubtitle_Track->actions().count() > 1)
                        {
                            ui->menuFont_Si_ze->setEnabled(true);
                            ui->actionShow_Subtitles->setEnabled(true);
                            ui->actionShow_Subtitles->setChecked(mpv->getSubtitleVisibility());
                        }
                        else
                        {
                            ui->menuFont_Si_ze->setEnabled(false);
                            ui->actionShow_Subtitles->setEnabled(false);
                            ui->actionShow_Subtitles->setChecked(false);
                        }
                        ui->menuTake_Screenshot->setEnabled(true);
                        ui->menuFit_Window->setEnabled(true);
                        ui->menuAspect_Ratio->setEnabled(true);
                        ui->action_Frame_Step->setEnabled(true);
                        ui->actionFrame_Back_Step->setEnabled(true);
                        ui->action_Deinterlace->setEnabled(true);
                        ui->action_Motion_Interpolation->setEnabled(true);
                    }
                    else
                    {
                        if(!albumArt)
                        {
                            // put in filler albumArt
                            if(ui->mpvFrame->styleSheet() == QString())
                                ui->mpvFrame->setStyleSheet("background-image:url(:/img/album-art.png);background-repeat:no-repeat;background-position:center;");
                        }
                        ui->action_Hide_Album_Art->setEnabled(true);
                        ui->menuSubtitle_Track->setEnabled(false);
                        ui->menuFont_Si_ze->setEnabled(false);
                        ui->actionShow_Subtitles->setEnabled(false);
                        ui->actionShow_Subtitles->setChecked(false);
                        ui->menuTake_Screenshot->setEnabled(false);
                        ui->menuFit_Window->setEnabled(false);
                        ui->menuAspect_Ratio->setEnabled(false);
                        ui->action_Frame_Step->setEnabled(false);
                        ui->actionFrame_Back_Step->setEnabled(false);
                        ui->action_Deinterlace->setEnabled(false);
                        ui->action_Motion_Interpolation->setEnabled(false);

                        if(nounours->sysTrayIcon->isVisible() && !hidePopup)
                        {
                            // todo: use {artist} - {title}
                            nounours->sysTrayIcon->showMessage("Nounours Player", mpv->getFileInfo().media_title, QSystemTrayIcon::NoIcon, 4000);
                        }
                    }

                    if(ui->menuAudio_Tracks->actions().count() == 1)
                        ui->menuAudio_Tracks->actions().first()->setEnabled(false);

                    if(pathChanged && autoFit)
                    {
                        nounours->FitWindow(autoFit, false);
                        pathChanged = false;
                    }
                }
            });

    connect(mpv, &MpvHandler::chaptersChanged,
            [=](const QList<Mpv::Chapter> &chapters)
            {
                if(mpv->getPlayState() > 0)
                {
                    QAction *action;
                    QList<int> ticks;
                    int n = 1,
                        N = chapters.length();
                    ui->menu_Chapters->clear();
                    for(auto &ch : chapters)
                    {
                        action = ui->menu_Chapters->addAction(QString("%0: %1").arg(Util::FormatNumberWithAmpersand(n, N), ch.title));
                        if(n <= 9)
                            action->setShortcut(QKeySequence("Ctrl+"+QString::number(n)));
                        connect(action, &QAction::triggered,
                                [=]
                                {
                                    mpv->Seek(ch.time);
                                });
                        ticks.push_back(ch.time);
                        n++;
                    }
                    if(ui->menu_Chapters->actions().count() == 0)
                    {
                        ui->menu_Chapters->setEnabled(false);
                        ui->action_Next_Chapter->setEnabled(false);
                        ui->action_Previous_Chapter->setEnabled(false);
                    }
                    else
                    {
                        ui->menu_Chapters->setEnabled(true);
                        ui->action_Next_Chapter->setEnabled(true);
                        ui->action_Previous_Chapter->setEnabled(true);
                    }

                    ui->seekBar->setTicks(ticks);
                }
            });

    connect(mpv, &MpvHandler::playStateChanged,
            [=](Mpv::PlayState playState)
            {
                nounours->jellyfin->UpdatePlaybackState(mpv->getFile(), playState);

                switch(playState)
                {
                case Mpv::Loaded:
                    break;

                case Mpv::Started:
                    if(!init) // will only happen the first time a file is loaded.
                    {
                        ui->action_Play->setEnabled(true);
                        ui->playButton->setEnabled(true);
                        ui->playlistButton->setEnabled(true);
                        ui->action_Show_Playlist->setEnabled(true);
                        ui->menuAudio_Tracks->setEnabled(true);
                        init = true;
                    }
                    SetPlaybackControls(true);
                    mpv->Play();
                    nounours->overlay->showStatusText(QString(), 0);
                case Mpv::Playing:
                    SetPlayButtonIcon(false);
                    if(onTop == "playing")
                        Util::SetAlwaysOnTop(winId(), true);
                    break;

                case Mpv::Paused:
                    SetPlayButtonIcon(true);
                    if(onTop == "playing")
                        Util::SetAlwaysOnTop(winId(), false);
                    break;

                case Mpv::Stopped:
                    SetPlayButtonIcon(true);
                    logoLabel->setVisible(true);
                    nounours->overlay->showStatusText(QString(), 0);
                    if(onTop == "playing")
                        Util::SetAlwaysOnTop(winId(), false);
                    break;

                case Mpv::Idle:
                    if(init)
                    {
                        if(ui->action_This_File->isChecked()) // repeat this file
                            ui->playlistWidget->PlayIndex(0, true); // restart file
                        else if(ui->actionStop_after_Current->isChecked() ||  // stop after playing this file
                                ui->playlistWidget->CurrentIndex() >= ui->playlistWidget->count()-1) // end of the playlist
                        {
                            if(!ui->actionStop_after_Current->isChecked() && // not supposed to stop after current
                                ui->action_Playlist->isChecked() && // we're supposed to restart the playlist
                                ui->playlistWidget->count() > 0) // playlist isn't empty
                            {
                                ui->playlistWidget->PlayIndex(0); // restart playlist
                            }
                            else if(!ui->actionStop_after_Current->isChecked() && // not supposed to stop after current
                                    nounours->jellyfin->PlayNextSeason()) // continue into the next season, if any
                            {
                                // playback continues asynchronously once the next season's episodes are fetched
                            }
                            else // stop
                            {
                                setWindowTitle("Nounours Player");
                                SetPlaybackControls(false);
                                ui->seekBar->setTracking(0);
                                ui->actionStop_after_Current->setChecked(false);
                                if(ui->mpvFrame->styleSheet() != QString()) // remove filler album art
                                    ui->mpvFrame->setStyleSheet("");
                            }
                        }
                        else if(userStopped)
                        {
                            userStopped = false;
                            setWindowTitle("Nounours Player");
                            SetPlaybackControls(false);
                            ui->seekBar->setTracking(0);
                            if(ui->mpvFrame->styleSheet() != QString())
                                ui->mpvFrame->setStyleSheet("");
                        }
                        else
                            ui->playlistWidget->PlayIndex(1, true);
                    }
                    break;
                }
            });

    connect(mpv, &MpvHandler::pathChanged,
            [=]()
            {
                pathChanged = true;
            });

      connect(mpv, &MpvHandler::fileChanging,
              [=](int t, int l)
              {
                  if(current != nullptr)
                  {
                      if(t > 0.05*l && t < 0.95*l) // only save if within the middle 90%
                          current->time = t;
                      else
                          current->time = 0;
                  }
              });

    connect(mpv, &MpvHandler::timeChanged,
            [=](int i)
            {
                const Mpv::FileInfo &fi = mpv->getFileInfo();
                // set the seekBar's location with NoSignal function so that it doesn't trigger a seek
                // the formula is a simple ratio seekBar's max * time/totalTime
                ui->seekBar->setValueNoSignal(ui->seekBar->maximum()*((double)i/fi.length));

                SetRemainingLabels(i);

                // set next/previous chapter's enabled state
                if(fi.chapters.length() > 0)
                {
                    ui->action_Next_Chapter->setEnabled(i < fi.chapters.last().time);
                    ui->action_Previous_Chapter->setEnabled(i > fi.chapters.first().time);
                }
            });

    connect(mpv, &MpvHandler::volumeChanged,
            [=](int volume)
            {
                ui->volumeSlider->setValueNoSignal(volume);
            });

    connect(mpv, &MpvHandler::speedChanged,
            [=](double speed)
            {
                static double last = 1;
                if(last != speed)
                {
                    if(init)
                        mpv->ShowText(tr("Speed: %0x").arg(QString::number(speed)));
                    if(speed <= 0.25)
                        ui->action_Decrease->setEnabled(false);
                    else
                        ui->action_Decrease->setEnabled(true);
                    last = speed;
                }
            });

    connect(mpv, &MpvHandler::sidChanged,
            [=](int sid)
            {
                QList<QAction*> actions = ui->menuSubtitle_Track->actions();
                for(auto &action : actions)
                {
                    if(action->text().startsWith(QString::number(sid)))
                    {
                        action->setCheckable(true);
                        action->setChecked(true);
                    }
                    else
                        action->setChecked(false);
                }
            });

    connect(mpv, &MpvHandler::aidChanged,
            [=](int aid)
            {
                QList<QAction*> actions = ui->menuAudio_Tracks->actions();
                for(auto &action : actions)
                {
                    if(action->text().startsWith(QString::number(aid)))
                    {
                        action->setCheckable(true);
                        action->setChecked(true);
                    }
                    else
                        action->setChecked(false);
                }
            });

    connect(mpv, &MpvHandler::subtitleVisibilityChanged,
            [=](bool b)
            {
                if(ui->actionShow_Subtitles->isEnabled())
                    ui->actionShow_Subtitles->setChecked(b);
                if(init)
                    mpv->ShowText(b ? tr("Subtitles visible") : tr("Subtitles hidden"));
            });

    connect(mpv, &MpvHandler::muteChanged,
            [=](bool b)
            {
                if(b)
                    ui->muteButton->setIcon(QIcon(":/img/default_mute.svg"));
                else
                    ui->muteButton->setIcon(QIcon(":/img/default_unmute.svg"));
                mpv->ShowText(b ? tr("Muted") : tr("Unmuted"));
            });

    connect(mpv, &MpvHandler::voChanged,
            [=](QString vo)
            {
                ui->action_Motion_Interpolation->setChecked(vo.contains("interpolation"));
            });
    // ui

    connect(ui->seekBar, &SeekBar::valueChanged,                        // Playback: Seekbar clicked
            [=](int i)
            {
                mpv->Seek(mpv->Relative(((double)i/ui->seekBar->maximum())*mpv->getFileInfo().length), true);
            });


    connect(ui->openButton, &OpenButton::LeftClick,                     // Playback: Open button (left click)
            [=]
            {
                nounours->Open();
            });

    connect(ui->openButton, &OpenButton::MiddleClick,                   // Playback: Open button (middle click)
            [=]
            {
                nounours->Jump();
            });

    connect(ui->openButton, &OpenButton::RightClick,                    // Playback: Open button (right click)
            [=]
            {
                nounours->OpenLocation();
            });

    connect(ui->rewindButton, &QPushButton::clicked,                    // Playback: Rewind button
            [=]
            {
                mpv->Rewind();
            });

    connect(ui->previousButton, &IndexButton::clicked,                  // Playback: Previous button
            [=]
            {
                ui->playlistWidget->PlayIndex(-1, true);
            });

    connect(ui->playButton, &QPushButton::clicked,                      // Playback: Play/pause button
            [=]
            {
                nounours->PlayPause();
            });

    connect(ui->stopButton, &QPushButton::clicked,                      // Playback: Stop button
            [=]
            {
                userStopped = true;
                nounours->Command("stop");
            });

    connect(ui->nextButton, &IndexButton::clicked,                      // Playback: Next button
            [=]
            {
                ui->playlistWidget->PlayIndex(1, true);
            });

    connect(ui->muteButton, &QPushButton::clicked,
            [=]
            {
                mpv->Mute(!mpv->getMute());
            });

    connect(ui->volumeSlider, &CustomSlider::valueChanged,              // Playback: Volume slider adjusted
            [=](int i)
            {
                mpv->Volume(i, true);
            });

    connect(ui->playlistButton, &QPushButton::clicked,                  // Playback: Clicked the playlist button
            [=]
            {
                TogglePlaylist();
            });

    connect(ui->splitter, &CustomSplitter::positionChanged,             // Splitter position changed
            [=](int i)
            {
                blockSignals(true);
                if(i == 0) // right-most, playlist is hidden
                {
                    ui->action_Show_Playlist->setChecked(false);
                    ui->action_Hide_Album_Art->setChecked(false);
                    ui->playlistLayoutWidget->setVisible(false);
                }
                else if(i == ui->splitter->max()) // left-most, album art is hidden, playlist is visible
                {
                    ui->action_Show_Playlist->setChecked(true);
                    ui->action_Hide_Album_Art->setChecked(true);
                }
                else // in the middle, album art is visible, playlist is visible
                {
                    ui->action_Show_Playlist->setChecked(true);
                    ui->action_Hide_Album_Art->setChecked(false);
                }
                ui->playlistLayoutWidget->setVisible(ui->action_Show_Playlist->isChecked());
                blockSignals(false);
                if(ui->actionMedia_Info->isChecked())
                    nounours->overlay->showInfoText();
            });

    connect(ui->searchBox, &QLineEdit::textChanged,                     // Playlist: Search box
            [=](QString s)
            {
                ui->playlistWidget->Search(s);
            });

    connect(ui->indexLabel, &CustomLabel::clicked,                      // Playlist: Clicked the indexLabel
            [=]
            {
                QString res = InputDialog::getInput(tr("Enter the file number you want to play:\nNote: Value must be from %0 - %1").arg("1", QString::number(ui->playlistWidget->count())),
                                                    tr("Enter File Number"),
                                                    [this](QString input)
                                                    {
                                                        int in = input.toInt();
                                                        if(in >= 1 && in <= ui->playlistWidget->count())
                                                            return true;
                                                        return false;
                                                    },
                                                    this);
                if(res != "")
                    ui->playlistWidget->PlayIndex(res.toInt()-1); // user index will be 1 greater than actual
            });

    connect(ui->playlistWidget, &PlaylistWidget::currentRowChanged,     // Playlist: Playlist selection changed
            [=](int)
            {
                SetIndexLabels(true);
            });

    connect(ui->currentFileButton, &QPushButton::clicked,               // Playlist: Select current file button
            [=]
            {
                ui->playlistWidget->SelectIndex(ui->playlistWidget->CurrentIndex());
            });

    connect(ui->refreshButton, &QPushButton::clicked,                   // Playlist: Refresh playlist button
            [=]
            {
                ui->playlistWidget->RefreshPlaylist();
            });

    connect(ui->savePlaylistButton, &QPushButton::clicked,              // Playlist: Save playlist button
            [=]
            {
                ui->playlistWidget->SavePlaylist();
            });

    connect(ui->shufflePlaylistButton, &QPushButton::toggled,
            [=](bool checked)
            {
                if(checked)
                    ui->playlistWidget->Shuffle();
                else
                    ui->playlistWidget->Populate();
            });

    connect(ui->inputLineEdit, &CustomLineEdit::submitted,
            [=](QString s)
            {
                nounours->Command(s);
                ui->inputLineEdit->setText("");
            });

    // Video Equalizer action
    actionVideoEqualizer = new QAction(tr("Video &Equalizer"), this);
    actionVideoEqualizer->setEnabled(false);
    ui->menu_View->insertAction(ui->actionMedia_Info, actionVideoEqualizer);
    ui->menu_View->insertSeparator(ui->actionMedia_Info);
    connect(actionVideoEqualizer, &QAction::triggered, [=] {
        VideoEqualizerDialog dlg(mpv, this);
        dlg.exec();
    });

    connect(ui->actionJellyfin_Search, &QAction::triggered, [=] {
        JellyfinSearchDialog dlg(nounours, this);
        dlg.exec();
    });

    ui->actionJellyfin_Search->setVisible(nounours->jellyfin->isConnected());
    connect(nounours->jellyfin, &JellyfinManager::connectedSignal, [=] {
        ui->actionJellyfin_Search->setVisible(true);
    });
    connect(nounours->jellyfin, &JellyfinManager::connectionFailedSignal, [=] {
        ui->actionJellyfin_Search->setVisible(false);
    });

    // add multimedia shortcuts
    ui->action_Play->setShortcuts({ui->action_Play->shortcut(), QKeySequence(Qt::Key_MediaPlay)});
    ui->action_Stop->setShortcuts({ui->action_Stop->shortcut(), QKeySequence(Qt::Key_MediaStop)});
    ui->actionPlay_Next_File->setShortcuts({ui->actionPlay_Next_File->shortcut(), QKeySequence(Qt::Key_MediaNext)});
    ui->actionPlay_Previous_File->setShortcuts({ui->actionPlay_Previous_File->shortcut(), QKeySequence(Qt::Key_MediaPrevious)});
}

MainWindow::~MainWindow()
{
    if(current != nullptr)
    {
        int t = mpv->getTime(),
            l = mpv->getFileInfo().length;
        if(t > 0.05*l && t < 0.95*l) // only save if within the middle 90%
            current->time = t;
        else
            current->time = 0;
    }
    nounours->SaveSettings();

    // Note: child objects _should_ not need to be deleted because
    // all children should get deleted when mainwindow is deleted
    // see: http://qt-project.org/doc/qt-4.8/objecttrees.html

    // but apparently they don't (https://github.com/u8sand/Nounours-Player/issues/47)
    delete nounours;
    delete ui;
}

void MainWindow::Load(QString file)
{
    // load the settings here--the constructor has already been called
    // this solves some issues with setting things before the constructor has ended
    menuVisible = ui->menubar->isVisible(); // does the OS use a menubar? (appmenu doesn't)
    nounours->LoadSettings();
    mpv->Initialize();
    mpv->LoadFile(file);
}

void MainWindow::MapShortcuts()
{
    auto tmp = commandActionMap;
    // map shortcuts to actions
    for(auto input_iter = nounours->input.begin(); input_iter != nounours->input.end(); ++input_iter)
    {
        auto commandAction = tmp.find(input_iter->first);
        if(commandAction != tmp.end())
        {
            (*commandAction)->setShortcut(QKeySequence(input_iter.key()));
            tmp.erase(commandAction);
        }
    }
    // clear the rest
    for(auto iter = tmp.begin(); iter != tmp.end(); ++iter)
        (*iter)->setShortcut(QKeySequence());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if(event->mimeData()->hasUrls() || event->mimeData()->hasText()) // url / text
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if(mimeData->hasUrls()) // urls
    {
        for(QUrl &url : mimeData->urls())
        {
            if(url.isLocalFile())
                mpv->LoadFile(url.toLocalFile());
            else
                mpv->LoadFile(url.url());
        }
    }
    else if(mimeData->hasText()) // text
        mpv->LoadFile(mimeData->text());
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        if(gestures)
        {
            if(ui->mpvFrame->geometry().contains(event->pos())) // mouse is in the mpvFrame
                nounours->gesture->Begin(GestureHandler::HSEEK_VVOLUME, event->globalPosition().toPoint(), pos());
            else if(!isFullScreen()) // not fullscreen
                nounours->gesture->Begin(GestureHandler::MOVE, event->globalPosition().toPoint(), pos());
        }
        else if(!isFullScreen()) // not fullscreen
            nounours->gesture->Begin(GestureHandler::MOVE, event->globalPosition().toPoint(), pos());

        if(ui->remainingLabel->rect().contains(ui->remainingLabel->mapFrom(this, event->pos()))) // clicked timeLayoutWidget
            setRemaining(!remaining); // todo: use a nounourscommand
    }
    else if(event->button() == Qt::RightButton &&
            !isFullScreenMode() &&  // not fullscreen mode
            mpv->getPlayState() > 0 &&  // playing
            ui->mpvFrame->geometry().contains(event->pos()) && // mouse is in the mpvFrame
            !leftClickPlayPause)
    {
        mpv->PlayPause(ui->playlistWidget->CurrentItem());
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    bool gestureCaptured = nounours->gesture->End();
    if (!gestureCaptured && leftClickPlayPause && event->button() == Qt::LeftButton) {
        mpv->PlayPause(ui->playlistWidget->CurrentItem());
    }
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if(nounours->gesture->Process(event->globalPosition().toPoint()))
        event->accept();
    else if(isFullScreenMode())
    {
        setCursor(QCursor(Qt::ArrowCursor)); // show the cursor
        autohide->stop();

        QRect playbackRect = geometry();
        playbackRect.setTop(playbackRect.bottom() - 60);
        bool showPlayback = playbackRect.contains(event->globalPosition().toPoint());
        ui->playbackLayoutWidget->setVisible(showPlayback || ui->outputTextEdit->isVisible());
        ui->seekBar->setVisible(showPlayback || ui->outputTextEdit->isVisible());

        QRect playlistRect = geometry();
        playlistRect.setLeft(playlistRect.right() - qCeil(playlistRect.width()/7.0));
        bool showPlaylist = playlistRect.contains(event->globalPosition().toPoint());
        ShowPlaylist(showPlaylist);

        if(!(showPlayback || showPlaylist) && autohide)
            autohide->start(500);
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::leaveEvent(QEvent *event)
{
    mouseMoveEvent(new QMouseEvent(QMouseEvent::MouseMove,
                                   QCursor::pos(), QCursor::pos(),
                                   Qt::NoButton,Qt::NoButton,Qt::NoModifier,
                                   QPointingDevice::primaryPointingDevice()));
    QMainWindow::leaveEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->mpvFrame)
    {
        if(isFullScreenMode() && event->type() == QEvent::MouseMove)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            mouseMoveEvent(mouseEvent);
        }
        else if(event->type() == QEvent::Resize)
        {
            logoLabel->setGeometry(ui->mpvFrame->rect());
            int maxW = qMin(ui->mpvFrame->width()  - 60, 420);
            int maxH = qMin(ui->mpvFrame->height() - 40, maxW * 710 / 512);
            logoLabel->setPixmap(QPixmap(":/img/logo_memorial.svg")
                                 .scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    else if(event->type() == 6) // QEvent::KeyPress = 6  (but using QEvent::KeyPress causes compile errors, not sure why)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        keyPressEvent(keyEvent);
    }
    return false;
}

void MainWindow::wheelEvent(QWheelEvent *event)
{
    if(event->angleDelta().y() > 0)
        mpv->Volume(mpv->getVolume()+5, true);
    else
        mpv->Volume(mpv->getVolume()-5, true);
    QMainWindow::wheelEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // keyboard shortcuts
    if(!nounours->input.empty())
    {
        QString key = QKeySequence(event->modifiers()|event->key()).toString();

        // TODO: Add more protection/find a better way to protect edit boxes from executing commands
        if(focusWidget() == ui->inputLineEdit &&
           key == "Return")
            return;

        // Escape exits fullscreen
        if(isFullScreen() &&
           key == "Esc") {
            FullScreen(false);
            return;
        }

        // find shortcut in input hash table
        auto iter = nounours->input.find(key);
        if(iter != nounours->input.end())
            nounours->Command(iter->first); // execute command
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if(ui->actionMedia_Info->isChecked())
        nounours->overlay->showInfoText();
    logoLabel->setGeometry(ui->mpvFrame->rect());
    QMainWindow::resizeEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton && ui->mpvFrame->geometry().contains(event->pos())) // if mouse is in the mpvFrame
    {
        if(!isFullScreen() && ui->action_Full_Screen->isEnabled()) // don't allow people to go full screen if they shouldn't be able to
            FullScreen(true);
        else // they can leave fullscreen even if it's disabled (eg. video ends while in full screen)
            FullScreen(false);
        event->accept();
    }
    QMainWindow::mouseDoubleClickEvent(event);
}

void MainWindow::SetIndexLabels(bool enable)
{
    int i = ui->playlistWidget->currentRow(),
        index = ui->playlistWidget->CurrentIndex();

    // next file
    if(enable && index+1 < ui->playlistWidget->count()) // not the last entry
    {
        SetNextButtonEnabled(true);
        ui->nextButton->setIndex(index+2); // starting at 1 instead of at 0 like actual index

    }
    else
        SetNextButtonEnabled(false);

    // previous file
    if(enable && index-1 >= 0) // not the first entry
    {
        SetPreviousButtonEnabled(true);
        ui->previousButton->setIndex(-index); // we use a negative index value for the left button
    }
    else
        SetPreviousButtonEnabled(false);

    if(i == -1) // no selection
    {
        ui->indexLabel->setText(tr("No selection"));
        ui->indexLabel->setEnabled(false);
    }
    else
    {
        ui->indexLabel->setEnabled(true);
        ui->indexLabel->setText(tr("%0 / %1").arg(QString::number(i+1), QString::number(ui->playlistWidget->count())));
    }
}

void MainWindow::SetPlaybackControls(bool enable)
{
    logoLabel->setVisible(!enable);

    // playback controls
    ui->seekBar->setEnabled(enable);
    ui->rewindButton->setEnabled(enable);

    SetIndexLabels(enable);

    // menubar
    ui->stopButton->setEnabled(enable);
    ui->action_Stop->setEnabled(enable);
    ui->action_Restart->setEnabled(enable);
    ui->menuS_peed->setEnabled(enable);
    ui->action_Jump_to_Time->setEnabled(enable);
    ui->actionMedia_Info->setEnabled(enable);
    ui->actionMediaInfoFull->setEnabled(enable);
    actionVideoEqualizer->setEnabled(enable);
    ui->actionShow_in_Folder->setEnabled(enable && nounours->mpv->getPath() != QString());
    ui->action_Full_Screen->setEnabled(enable);
    if(enable) {
        ui->menubar->addAction(ui->menuSubtitle_Track->menuAction());
        ui->menubar->addAction(ui->menuAudio_Tracks->menuAction());
    } else {
        ui->menubar->removeAction(ui->menuSubtitle_Track->menuAction());
        ui->menubar->removeAction(ui->menuAudio_Tracks->menuAction());
    }
    if(!enable)
    {
        ui->action_Hide_Album_Art->setEnabled(false);
        ui->menuFont_Si_ze->setEnabled(false);
    }
}

void MainWindow::HideAllControls(bool w, bool s)
{
    if(s)
    {
        hideAllControls = w;
        if(isFullScreen())
            return;
    }
    if(w)
    {
        if(s || !hideAllControls)
            playlistState = ui->playlistLayoutWidget->isVisible();
        ui->menubar->setVisible(false);
        setContextMenuPolicy(Qt::ActionsContextMenu);
        mouseMoveEvent(new QMouseEvent(QMouseEvent::MouseMove,
                                       QCursor::pos(), QCursor::pos(),
                                       Qt::NoButton,Qt::NoButton,Qt::NoModifier,
                                       QPointingDevice::primaryPointingDevice()));
    }
    else
    {
        if(menuVisible)
            ui->menubar->setVisible(true);
        ui->seekBar->setVisible(true);
        ui->playbackLayoutWidget->setVisible(true);
        if(!leftClickPlayPause) {
            setContextMenuPolicy(Qt::NoContextMenu);
        }
        setCursor(QCursor(Qt::ArrowCursor)); // show cursor
        autohide->stop();
        ShowPlaylist(playlistState);
    }
}

void MainWindow::FullScreen(bool fs)
{
    if(fs)
    {
        if(nounours->dimDialog && nounours->dimDialog->isVisible())
            nounours->Dim(false);
        setWindowState(windowState() | Qt::WindowFullScreen);
        if(!hideAllControls)
            HideAllControls(true, false);
    }
    else
    {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
        if(!hideAllControls)
            HideAllControls(false, false);
    }
}

bool MainWindow::isPlaylistVisible()
{
    // if the position is 0, playlist is hidden
    return (ui->splitter->position() != 0);
}

void MainWindow::TogglePlaylist()
{
    ShowPlaylist(!isPlaylistVisible());
}

void MainWindow::ShowPlaylist(bool visible)
{
    if(ui->splitter->position() != 0 && visible) // ignore showing if it's already visible as it resets original position
        return;

    if(visible)
        ui->splitter->setPosition(ui->splitter->normalPosition()); // bring splitter position to normal
    else
    {
        if(ui->splitter->position() != ui->splitter->max() && ui->splitter->position() != 0)
            ui->splitter->setNormalPosition(ui->splitter->position()); // save current splitter position as the normal position
        ui->splitter->setPosition(0); // set splitter position to right-most
        setFocus();
    }
}

void MainWindow::HideAlbumArt(bool hide)
{
    if(hide)
    {
        if(ui->splitter->position() != ui->splitter->max() && ui->splitter->position() != 0)
            ui->splitter->setNormalPosition(ui->splitter->position()); // save splitter position as the normal position
        ui->splitter->setPosition(ui->splitter->max()); // bring the splitter position to the left-most
    }
    else
        ui->splitter->setPosition(ui->splitter->normalPosition()); // bring the splitter to normal position
}

void MainWindow::UpdateRecentFiles()
{
    ui->menu_Recently_Opened->clear();
    QAction *action;
    int n = 1,
        N = recent.length();
    for(auto &f : recent)
    {
        action = ui->menu_Recently_Opened->addAction(QString("%0. %1").arg(Util::FormatNumberWithAmpersand(n, N), Util::ShortenPathToParent(f).replace("&","&&")));
        if(n++ == 1)
            action->setShortcut(QKeySequence("Ctrl+Z"));
        connect(action, &QAction::triggered,
                [=]
                {
                    // restore the saved title so the window title and playlist
                    // show it instead of the raw stream URL
                    if(!f.title.isEmpty() && f.path.startsWith("http"))
                        nounours->jellyfin->SetNowPlaying(f.path, f.title);
                    mpv->LoadFile(f);
                });
    }
}

void MainWindow::SetNextButtonEnabled(bool enable)
{
    ui->nextButton->setEnabled(enable);
    ui->actionPlay_Next_File->setEnabled(enable);
}

void MainWindow::SetPreviousButtonEnabled(bool enable)
{
    ui->previousButton->setEnabled(enable);
    ui->actionPlay_Previous_File->setEnabled(enable);
}

void MainWindow::SetPlayButtonIcon(bool play)
{
    if(play)
    {
        ui->playButton->setIcon(QIcon(":/img/default_play.svg"));
        ui->action_Play->setText(tr("&Play"));
    }
    else // pause icon
    {
        ui->playButton->setIcon(QIcon(":/img/default_pause.svg"));
        ui->action_Play->setText(tr("&Pause"));
    }
}

void MainWindow::SetRemainingLabels(int time)
{
    // todo: move setVisible functions outside of this function which gets called every second and somewhere at the start of a video
    const Mpv::FileInfo &fi = mpv->getFileInfo();
    if(fi.length == 0)
    {
        ui->durationLabel->setText(Util::FormatTime(time, time));
        ui->remainingLabel->setVisible(false);
        ui->seperatorLabel->setVisible(false);
    }
    else
    {
        ui->remainingLabel->setVisible(true);
        ui->seperatorLabel->setVisible(true);

        ui->durationLabel->setText(Util::FormatTime(time, fi.length));
        if(remaining)
        {
            int remainingTime = fi.length - time;
            QString text = "-" + Util::FormatTime(remainingTime, fi.length);
            if(mpv->getSpeed() != 1)
            {
                double speed = mpv->getSpeed();
                text += QString("  (-%0)").arg(Util::FormatTime(int(remainingTime/speed), int(fi.length/speed)));
            }
            ui->remainingLabel->setText(text);
        }
        else
        {
            QString text = Util::FormatTime(fi.length, fi.length);
            if(mpv->getSpeed() != 1)
            {
                double speed = mpv->getSpeed();
                text += QString("  (%0)").arg(Util::FormatTime(int(fi.length/speed), int(fi.length/speed)));
            }
            ui->remainingLabel->setText(text);
        }
    }
}
