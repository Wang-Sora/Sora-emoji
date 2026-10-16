#include "app/SoraMain.h"
#include "app/ThemeManager.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("Sora"));
    QApplication::setApplicationDisplayName(QStringLiteral("Sora"));
    QApplication::setOrganizationName(QStringLiteral("Sora"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));

    QFont font(QStringLiteral("Microsoft YaHei UI"), 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(font);

    ThemeManager::instance().setMode(ThemeManager::Mode::Dark);

    SoraMain window;
    window.show();

    return QApplication::exec();
}
