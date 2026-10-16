#include "app/ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

void ThemeManager::setMode(Mode mode)
{
    if (m_applied && m_mode == mode)
        return;

    m_mode = mode;
    loadStyleSheet();
    emit modeChanged();
}

void ThemeManager::toggle()
{
    setMode(m_mode == Mode::Dark ? Mode::Light : Mode::Dark);
}

void ThemeManager::loadStyleSheet()
{
    const QString path = m_mode == Mode::Dark
                             ? QStringLiteral(":/qss/dark.qss")
                             : QStringLiteral(":/qss/light.qss");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 样式表缺失时不影响启动，仅退化为 Qt 默认外观
        qApp->setStyleSheet(QString());
        m_applied = true;
        return;
    }

    QTextStream stream(&file);
    qApp->setStyleSheet(stream.readAll());
    m_applied = true;
}

QColor ThemeManager::accent() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#32F08C"))
                                : QColor(QStringLiteral("#0FDC78"));
}

QColor ThemeManager::iconIdle() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#8A93A0"))
                                : QColor(QStringLiteral("#6B7280"));
}

QColor ThemeManager::iconHover() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#E7EBF0"))
                                : QColor(QStringLiteral("#12161C"));
}

QColor ThemeManager::textPrimary() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#E7EBF0"))
                                : QColor(QStringLiteral("#12161C"));
}

QColor ThemeManager::textSecondary() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#8A93A0"))
                                : QColor(QStringLiteral("#6B7280"));
}

QColor ThemeManager::surface() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#0E1116"))
                                : QColor(QStringLiteral("#FFFFFF"));
}

QColor ThemeManager::border() const
{
    return m_mode == Mode::Dark ? QColor(QStringLiteral("#1F252E"))
                                : QColor(QStringLiteral("#E3E6EA"));
}
