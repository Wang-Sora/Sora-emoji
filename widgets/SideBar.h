#ifndef SORA_SIDEBAR_H
#define SORA_SIDEBAR_H

#include <QFrame>
#include <QList>

class QButtonGroup;
class QToolButton;

/**
 * 左侧图标导航栏。
 *
 * 图标是 SVG + 运行时着色，所以主题切换时需要 retint() 重新生成一遍图标。
 */
class SideBar : public QFrame
{
    Q_OBJECT

public:
    explicit SideBar(QWidget *parent = nullptr);

    void setCurrentIndex(int index);
    int currentIndex() const;

Q_SIGNALS:
    void currentChanged(int index);

private:
    void retint();
    void addButton(const QString &iconPath, const QString &tip);

    QButtonGroup *m_group = nullptr;
    QList<QToolButton *> m_buttons;
    QList<QString> m_iconPaths;
};

#endif // SORA_SIDEBAR_H
