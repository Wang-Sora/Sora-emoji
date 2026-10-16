#ifndef SORA_THEMEMANAGER_H
#define SORA_THEMEMANAGER_H

#include <QObject>
#include <QColor>

/**
 * 全局主题管理器：负责加载 QSS 样式表，并对外暴露当前主题的语义色。
 *
 * QSS 本身不支持 CSS 变量，所以需要按主题动态着色的地方（例如 SVG 图标）
 * 统一从这里取色，主题切换时再重新着色。
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Dark, Light };

    static ThemeManager &instance();

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void toggle();

    bool isDark() const { return m_mode == Mode::Dark; }

    // 语义色
    QColor accent() const;
    QColor iconIdle() const;
    QColor iconHover() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor surface() const;
    QColor border() const;

Q_SIGNALS:
    void modeChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(ThemeManager)

    void loadStyleSheet();

    Mode m_mode = Mode::Dark;
    bool m_applied = false;
};

#endif // SORA_THEMEMANAGER_H
