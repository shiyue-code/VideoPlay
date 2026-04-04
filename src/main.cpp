#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QProcessEnvironment>
#include <QDebug>
#include <oclero/qlementine.hpp>
#include "mainwindow.h"

using namespace VideoPlay;

int main(int argc, char* argv[])
{
    // 使用 Windows 多媒体后端
    qputenv("QT_MEDIA_BACKEND", "windows");
    
    QApplication app(argc, argv);
    
    // 使用 Qlementine 样式
    app.setStyle(new oclero::qlementine::QlementineStyle);
    
    app.setApplicationName("VideoPlay");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VideoPlay");
    app.setOrganizationDomain("videoplay.local");

    try {
        MainWindow window;
        window.show();

        if (argc > 1) {
            QString filePath = QString::fromLocal8Bit(argv[1]);
            if (QFile::exists(filePath)) {
                window.openFile(filePath);
            }
        }

        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("An unexpected error occurred: %1").arg(e.what()));
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("An unknown error occurred."));
        return 1;
    }
}
