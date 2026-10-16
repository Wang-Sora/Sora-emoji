#include "widgets/TitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QWindow>

TitleBar::TitleBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TitleBar"));
    setFixedHeight(40);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 10, 0);
    layout->setSpacing(8);

    auto *mark = new QLabel(this);
    mark->setObjectName(QStringLiteral("AppMark"));
    mark->setFixedSize(8, 8);
    layout->addWidget(mark);

    m_title = new QLabel(QStringLiteral("Sora"), this);
    m_title->setObjectName(QStringLiteral("AppTitle"));
    layout->addWidget(m_title);

    layout->addStretch(1);

    const struct {
        const char *name;
        const char *tip;
    } dots[] = {
        { "min",   "最小化" },
        { "max",   "最大化" },
        { "close", "关闭"   },
    };

    for (const auto &dot : dots) {
        auto *button = new QPushButton(this);
        button->setObjectName(QStringLiteral("WinDot"));
        button->setProperty("dot", QString::fromLatin1(dot.name));
        button->setToolTip(QString::fromUtf8(dot.tip));
        button->setCursor(Qt::ArrowCursor);
        button->setFixedSize(14, 14);
        button->setFlat(true);
        layout->addWidget(button);

        const QString kind = QString::fromLatin1(dot.name);
        if (kind == QLatin1String("min"))
            connect(button, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
        else if (kind == QLatin1String("max"))
            connect(button, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
        else
            connect(button, &QPushButton::clicked, this, &TitleBar::closeRequested);
    }
}

void TitleBar::setTitle(const QString &title)
{
    m_title->setText(title);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWindow *handle = window()->windowHandle()) {
            handle->startSystemMove();
            event->accept();
            return;
        }
    }

    QFrame::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
        event->accept();
        return;
    }

    QFrame::mouseDoubleClickEvent(event);
}
