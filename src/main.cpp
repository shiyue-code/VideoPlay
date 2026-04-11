#include <QApplication>
#include <ElaApplication.h>

#include "ui/ela/ElaVideoWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // 初始化 ElaWidgetTools
    eApp->init();
    
    // 设置应用信息
    app.setApplicationName("VideoPlay");
    app.setApplicationVersion("1.1.0");
    app.setOrganizationName("VideoPlay");
    
    // 创建主窗口
    VideoPlay::ElaVideoWindow window;
    window.show();
    
    return app.exec();
}
