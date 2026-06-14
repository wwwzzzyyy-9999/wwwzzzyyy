#include "logindialog.h"
#include "videomainwindow.h"
#include "networkmanager.h"

#include <QApplication>
#include <QImage>

extern "C" {
#include "libavformat/avformat.h"
}

#undef main
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qRegisterMetaType<QImage>("QImage");
    avformat_network_init();

    NetworkManager networkManager;

    while (true) {
        LoginDialog loginDialog(nullptr, &networkManager);
        if (loginDialog.exec() != QDialog::Accepted)
            break;

        VideoMainWindow w(&networkManager);

        bool loggedOut = false;
        QObject::connect(&w, &VideoMainWindow::SIG_logout, [&]() {
            networkManager.setToken("");
            loggedOut = true;
            w.close();
        });

        w.show();
        a.exec();

        if (!loggedOut)
            break;
    }

    return 0;
}
