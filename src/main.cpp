#include "ui/mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QString>

#include <locale.h>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    FreeConsole();
#endif
#if defined(Q_OS_LINUX)
    // mpv is embedded through its "wid" option, which only works on X11: on a
    // native Wayland session mpv ignores it and opens its own detached window
    // (typical in VMs, where the default session is Wayland). Run through
    // XWayland instead, and hide WAYLAND_DISPLAY so mpv doesn't pick its
    // Wayland backend either. QT_QPA_PLATFORM is overridden when it asks for
    // Wayland (many Sway/Hyprland setups export "wayland;xcb" globally), since
    // embedding can't work there; any other explicit platform is respected.
    const QString qpa = qEnvironmentVariable("QT_QPA_PLATFORM");
    if((qpa.isEmpty() || qpa.contains("wayland", Qt::CaseInsensitive)) &&
       !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
       !qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", "xcb");
        qunsetenv("WAYLAND_DISPLAY");
    }
#endif
    QApplication a(argc, argv);
    setlocale(LC_NUMERIC, "C"); // for mpv

    MainWindow w;
    w.show();

    // parse command line
    QStringList args = QApplication::arguments();
    QStringList::iterator arg = args.begin();
    if(++arg != args.end())
        w.Load(*arg);
    else
        w.Load();

    return a.exec();
}
