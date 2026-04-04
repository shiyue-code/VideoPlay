#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QProcessEnvironment>
#include <QDebug>
#include <QStandardPaths>
#include <oclero/qlementine.hpp>
#include "ui/mainwindow.h"
#include "utils/logger.h"

using namespace VideoPlay;

int main(int argc, char* argv[])
{
    // 初始化日志系统
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    QString logFile = QString("%1/videoplay.log").arg(logDir);
    Logger::instance().setLogFile(logFile);
    Logger::instance().info("=== VideoPlay starting ===");

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
        Logger::instance().error(QString("Exception: %1").arg(e.what()));
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("An unexpected error occurred: %1").arg(e.what()));
        return 1;
    } catch (...) {
        Logger::instance().error("Unknown exception occurred");
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("An unknown error occurred."));
        return 1;
    }
}
