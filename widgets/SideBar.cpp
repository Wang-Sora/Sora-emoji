#include "widgets/SideBar.h"

#include "app/ThemeManager.h"
#include "core/TintedIcon.h"

#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>

SideBar::SideBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("SideBar"));
    setFixedWidth(56);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 12, 0, 12);
    layout->setSpacing(6);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    addButton(QStringLiteral(":/icons/music.svg"), QStringLiteral("音乐"));
    addButton(QStringLiteral(":/icons/pdf.svg"), QStringLiteral("PDF 阅读"));
    addButton(QStringLiteral(":/icons/ai.svg"), QStringLiteral("AI 助手"));
    addButton(QStringLiteral(":/icons/settings.svg"), QStringLiteral("设置"));

    layout->addStretch(1);

    connect(m_group, &QButtonGroup::idClicked, this, &SideBar::currentChanged);
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &SideBar::retint);

    retint();
    setCurrentIndex(0);
}

void SideBar::addButton(const QString &iconPath, const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("NavButton"));
    button->setProperty("nav", true);
    button->setToolTip(tip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(40, 40);
    button->setIconSize(QSize(20, 20));

    const int id = m_buttons.size();
    m_group->addButton(button, id);
    m_buttons.append(button);
    m_iconPaths.append(iconPath);

    qobject_cast<QVBoxLayout *>(layout())->addWidget(button, 0, Qt::AlignHCenter);
}

void SideBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_buttons.size())
        return;

    m_buttons.at(index)->setChecked(true);
}

int SideBar::currentIndex() const
{
    return m_group->checkedId();
}

void SideBar::retint()
{
    const QColor idle = ThemeManager::instance().iconIdle();
    const QColor active = ThemeManager::instance().accent();

    for (int i = 0; i < m_buttons.size(); ++i) {
        QToolButton *button = m_buttons.at(i);
        const QString &path = m_iconPaths.at(i);

        QIcon icon = TintedIcon::create(path, idle);
        icon.addPixmap(TintedIcon::create(path, active).pixmap(20, 20), QIcon::Normal, QIcon::On);
        button->setIcon(icon);
    }
}
