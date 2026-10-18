#include "app/SoraMain.h"

#include "core/AiClient.h"
#include "core/AudioPlayer.h"
#include "pages/AiPage.h"
#include "pages/MusicPage.h"
#include "pages/PdfPage.h"
#include "pages/SettingsPage.h"
#include "pages/StickersPage.h"
#include "widgets/PlayerBar.h"
#include "widgets/SideBar.h"
#include "widgets/TitleBar.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

SoraMain::SoraMain(QWidget *parent)
    : QMainWindow(parent)
    , m_audio(new AudioPlayer(this))
    , m_ai(new AiClient(this))
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(940, 600);
    resize(1180, 720);
    setWindowTitle(QStringLiteral("Sora"));

    // 最大化和最小化共用一个定时器：节拍按屏幕刷新率取，配合 QElapsedTimer 计时。
    m_animTimer = new QTimer(this);
    m_animTimer->setTimerType(Qt::PreciseTimer);
    m_animTimer->setInterval(animIntervalMs());
    connect(m_animTimer, &QTimer::timeout, this, &SoraMain::tickAnim);

    buildUi();
    restoreWindowState();
    ensureTray();
}

SoraMain::~SoraMain()
{
    // 先摘掉托盘图标再析构，避免退出瞬间图标残留在通知区
    m_tray->hide();
}

void SoraMain::buildUi()
{
    auto *root = new QFrame(this);
    root->setObjectName(QStringLiteral("Root"));
    setCentralWidget(root);

    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_titleBar = new TitleBar(root);
    m_titleBar->setTitle(QStringLiteral("Sora"));
    rootLayout->addWidget(m_titleBar);

    auto *middle = new QWidget(root);
    auto *middleLayout = new QHBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    m_sidebar = new SideBar(middle);
    middleLayout->addWidget(m_sidebar);

    m_stack = new QStackedWidget(middle);
    m_stack->setObjectName(QStringLiteral("Pages"));
    m_stack->addWidget(new MusicPage(m_audio, m_stack));       // 0
    m_stack->addWidget(new PdfPage(m_stack));                  // 1
    m_stack->addWidget(new AiPage(m_ai, m_stack));             // 2
    m_stack->addWidget(new StickersPage(m_stack));             // 3
    m_stack->addWidget(new SettingsPage(m_ai, m_stack));       // 4
    m_stack->setCurrentIndex(0);

    middleLayout->addWidget(m_stack, 1);
    rootLayout->addWidget(middle, 1);

    auto *bottom = new QWidget(root);
    auto *bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    m_playerBar = new PlayerBar(m_audio, bottom);
    bottomLayout->addWidget(m_playerBar, 1);

    auto *grip = new QSizeGrip(bottom);
    grip->setObjectName(QStringLiteral("Grip"));
    grip->setFixedSize(16, 16);
    bottomLayout->addWidget(grip, 0, Qt::AlignBottom);

    rootLayout->addWidget(bottom);

    connect(m_sidebar, &SideBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &SoraMain::minimizeAnimated);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &SoraMain::toggleMaximize);
    connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);
}

void SoraMain::restoreWindowState()
{
    const QSettings settings;

    const QRect saved = settings.value(QStringLiteral("window/geometry")).toRect();
    if (saved.isValid()) {
        // 上一次的窗口可能落在已经拔掉的显示器上，落回可见区域才敢还原
        bool visible = false;
        const auto screens = QGuiApplication::screens();
        for (const QScreen *scr : screens) {
            if (scr->availableGeometry().intersects(saved)) {
                visible = true;
                break;
            }
        }
        if (visible) {
            setGeometry(saved);
            m_normalGeometry = saved;
        }
    }

    if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
        m_normalGeometry = geometry();
        m_maximized = true;
        QWidget::showMaximized();
    }
}

void SoraMain::setPage(int index)
{
    if (!m_stack || index < 0 || index >= m_stack->count())
        return;

    m_sidebar->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
}

void SoraMain::closeEvent(QCloseEvent *event)
{
    // 最大化时记的是还原矩形，否则下次"从最大化启动再还原"会变成整屏
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"),
                      m_maximized ? m_normalGeometry : geometry());
    settings.setValue(QStringLiteral("window/maximized"), m_maximized);

    // 托盘菜单的"退出"：不再询问，真正结束进程
    if (m_forceQuit) {
        QMainWindow::closeEvent(event);
        return;
    }

    // window/closeAction：ask（默认，每次询问）/ tray（最小化到托盘）/ exit（直接退出）
    // 与设置页「通用 → 关闭窗口时」写同一个键，两边随时可互相覆盖
    const QString action = settings.value(QStringLiteral("window/closeAction"),
                                          QStringLiteral("ask")).toString();

    if (action == QStringLiteral("exit")) {
        QMainWindow::closeEvent(event);
        return;
    }

    if (action == QStringLiteral("ask")) {
        bool remember = false;
        const CloseChoice choice = askCloseAction(&remember);
        if (choice == CloseChoice::Cancel) {
            event->ignore();   // 用户反悔，窗口留着
            return;
        }
        if (remember) {
            settings.setValue(QStringLiteral("window/closeAction"),
                              choice == CloseChoice::Tray
                                  ? QStringLiteral("tray") : QStringLiteral("exit"));
        }
        if (choice == CloseChoice::Exit) {
            QMainWindow::closeEvent(event);
            return;
        }
        // 本次选了"进托盘"但没勾记住：只对这一次生效，下次仍然询问
    }

    // 记忆值是 tray，或本次弹窗选了 tray，都走后台
    hideToTray();
    event->ignore();           // 不真正关闭，进程和音乐都留在后台
}

SoraMain::CloseChoice SoraMain::askCloseAction(bool *remember)
{
    QMessageBox box(QMessageBox::NoIcon, QStringLiteral("关闭 Sora"),
                    QStringLiteral("要直接退出，还是最小化到托盘后台继续播放？"),
                    QMessageBox::NoButton, this);
    const QAbstractButton *trayBtn =
        box.addButton(QStringLiteral("最小化到托盘"), QMessageBox::ActionRole);
    const QAbstractButton *exitBtn =
        box.addButton(QStringLiteral("直接退出"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    auto *rememberBox = new QCheckBox(QStringLiteral("记住我的选择，不再询问"), &box);
    box.setCheckBox(rememberBox);

    box.exec();
    *remember = rememberBox->isChecked();
    const QAbstractButton *clicked = box.clickedButton();
    if (clicked == trayBtn)
        return CloseChoice::Tray;
    if (clicked == exitBtn)
        return CloseChoice::Exit;
    return CloseChoice::Cancel;
}

void SoraMain::ensureTray()
{
    m_trayMenu = new QMenu(this);

    QAction *showAct = m_trayMenu->addAction(QStringLiteral("显示主窗口"));
    connect(showAct, &QAction::triggered, this, &SoraMain::restoreFromTray);

    m_trayPlayAction = m_trayMenu->addAction(QStringLiteral("暂停"));
    connect(m_trayPlayAction, &QAction::triggered, m_audio, &AudioPlayer::togglePlayPause);

    QAction *prevAct = m_trayMenu->addAction(QStringLiteral("上一首"));
    connect(prevAct, &QAction::triggered, m_audio, &AudioPlayer::previous);
    QAction *nextAct = m_trayMenu->addAction(QStringLiteral("下一首"));
    connect(nextAct, &QAction::triggered, m_audio, &AudioPlayer::next);

    m_trayMenu->addSeparator();
    QAction *quitAct = m_trayMenu->addAction(QStringLiteral("退出"));
    connect(quitAct, &QAction::triggered, this, [this] {
        m_forceQuit = true;
        // 窗口藏在托盘时 close() 不触发退出：quitOnLastWindowClosed 只认
        // "最后一个可见窗口"，隐藏窗口不算。所以隐藏状态下直接退；
        // 可见时照常走 close()，让 closeEvent 存一次窗口几何。
        if (isHidden())
            QCoreApplication::quit();
        else
            close();
    });

    // 菜单文案随播放状态变化（暂停 ↔ 播放）
    connect(m_audio, &AudioPlayer::playingChanged, this, [this](bool playing) {
        m_trayPlayAction->setText(playing ? QStringLiteral("暂停")
                                          : QStringLiteral("播放"));
        updateTrayTooltip();
    });
    connect(m_audio, &AudioPlayer::trackChanged, this, [this](const QString &) {
        updateTrayTooltip();
    });

    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/app.png")), this);
    m_tray->setContextMenu(m_trayMenu);
    m_tray->setToolTip(QStringLiteral("Sora"));
    // 左键单击图标恢复窗口；右键走上下文菜单
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            restoreFromTray();
    });
    m_tray->show();
}

void SoraMain::hideToTray()
{
    // 关闭时可能正处在最小化淡出动画中途，先停掉并把透明度复位，
    // 否则下次恢复窗口是隐形的
    m_anim = Anim::None;
    m_animTimer->stop();
    m_animatingMinimize = false;
    setWindowOpacity(1.0);
    hide();

    // 每次运行只提示一次，避免次次弹气泡惹人烦
    if (!m_trayNoticeShown) {
        m_trayNoticeShown = true;
        m_tray->showMessage(QStringLiteral("Sora 仍在运行"),
                            QStringLiteral("已最小化到托盘，音乐不会停。单击图标恢复窗口。"),
                            QSystemTrayIcon::Information, 2500);
    }
}

void SoraMain::restoreFromTray()
{
    if (isMinimized())
        QWidget::showNormal();
    setWindowOpacity(1.0);
    show();
    raise();
    activateWindow();
}

void SoraMain::updateTrayTooltip()
{
    if (!m_tray)
        return;

    const QString title = m_audio->currentTitle();
    if (m_audio->isPlaying()) {
        m_tray->setToolTip(title.isEmpty()
                               ? QStringLiteral("Sora 正在播放")
                               : QStringLiteral("Sora 正在播放：%1").arg(title));
    } else {
        m_tray->setToolTip(QStringLiteral("Sora"));
    }
}

void SoraMain::keyPressEvent(QKeyEvent *event)
{
    // 用窗口级 keyPressEvent 而不是 QShortcut：快捷键若挂在窗口上会先于焦点控件
    // 拿到空格，AI 输入框里就打不出空格了。焦点控件处理掉的按键不会冒泡到这里。
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        m_audio->togglePlayPause();
        event->accept();
        return;
    }

    if (event->modifiers() == Qt::ControlModifier && event->key() >= Qt::Key_1
        && event->key() <= Qt::Key_5) {
        setPage(event->key() - Qt::Key_1);
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

int SoraMain::animIntervalMs() const
{
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const qreal hz = scr ? scr->refreshRate() : 0.0;
    // 取不到刷新率时按 60Hz 兜底；下限 4ms 防止异常值把定时器打成空转
    const int interval = hz > 1.0 ? qRound(1000.0 / hz) : 17;
    return qBound(4, interval, 17);
}

void SoraMain::toggleMaximize()
{
    if (m_anim == Anim::Geometry)
        return;

    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!scr)
        return;

    const QRect from = geometry();

    if (m_maximized) {
        // 还原：必须先退出原生最大化状态，否则系统会忽略对已最大化窗口的
        // setGeometry，补间也就无从谈起。这一步之间没有重绘机会，看不到跳变。
        m_maximized = false;
        QWidget::showNormal();
        setGeometry(from);
        startGeometryAnim(from, m_normalGeometry);
    } else {
        m_normalGeometry = from;
        m_maximized = true;
        startGeometryAnim(from, scr->availableGeometry());
    }
}

void SoraMain::minimizeAnimated()
{
    if (m_animatingMinimize)
        return;

    // 最大化补间进行到一半要求最小化：先落到终点，避免几何停在半路
    if (m_anim == Anim::Geometry) {
        setGeometry(m_geoTo);
        m_anim = Anim::None;
        m_animTimer->stop();
        if (m_maximized)
            QWidget::showMaximized();
    }

    m_animatingMinimize = true;
    startFadeAnim(windowOpacity(), 0.0, 130);
}

void SoraMain::startGeometryAnim(const QRect &from, const QRect &to)
{
    if (from == to)
        return;

    m_geoFrom = from;
    m_geoTo = to;
    m_anim = Anim::Geometry;
    m_animDuration = 260;
    m_animClock.restart();
    m_animTimer->setInterval(animIntervalMs());
    m_animTimer->start();
}

void SoraMain::startFadeAnim(qreal from, qreal to, int ms)
{
    m_fadeFrom = from;
    m_fadeTo = to;
    m_anim = Anim::Fade;
    m_animDuration = ms;
    m_animClock.restart();
    m_animTimer->setInterval(animIntervalMs());
    m_animTimer->start();
}

void SoraMain::tickAnim()
{
    const qreal t = m_animDuration > 0
                        ? qMin<qreal>(1.0, qreal(m_animClock.elapsed()) / m_animDuration)
                        : 1.0;
    // 首尾导数为 0 的曲线：起手不猛冲、收尾不硬停，是"顺滑"的关键
    const qreal e = QEasingCurve(QEasingCurve::InOutSine).valueForProgress(t);

    if (m_anim == Anim::Geometry) {
        setGeometry(QRect(qRound(m_geoFrom.x() + (m_geoTo.x() - m_geoFrom.x()) * e),
                          qRound(m_geoFrom.y() + (m_geoTo.y() - m_geoFrom.y()) * e),
                          qRound(m_geoFrom.width() + (m_geoTo.width() - m_geoFrom.width()) * e),
                          qRound(m_geoFrom.height() + (m_geoTo.height() - m_geoFrom.height()) * e)));
    } else if (m_anim == Anim::Fade) {
        setWindowOpacity(m_fadeFrom + (m_fadeTo - m_fadeFrom) * e);
    }

    if (t >= 1.0)
        finishAnim();
}

void SoraMain::finishAnim()
{
    const Anim done = m_anim;
    m_anim = Anim::None;
    m_animTimer->stop();

    if (done == Anim::Geometry) {
        // 补间结束后再落到原生窗口状态，保留 Win+↓ / Snap Layouts 等系统行为。
        // 顺序很关键：先把窗口摆回还原矩形，再 showMaximized()——Windows 会在
        // 最大化那一刻把 rcNormalPosition 记成当前矩形，若此时窗口已经铺满屏幕，
        // 还原矩形就会被错记成整屏，Win+↓ 随之失效。两次调用在同一轮事件循环里，
        // 中间没有重绘机会，所以看不到跳变。
        if (m_maximized) {
            setGeometry(m_normalGeometry);
            QWidget::showMaximized();
        }
    } else if (done == Anim::Fade && m_animatingMinimize) {
        QWidget::showMinimized();   // 淡出结束后才真正最小化
    }
}

void SoraMain::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);

    if (event->type() != QEvent::WindowStateChange)
        return;

    if (isMinimized()) {
        // 由任务栏等方式触发的最小化没走我们的动画，这里补一次
        if (!m_animatingMinimize)
            minimizeAnimated();
        return;
    }

    // 与原生窗口状态保持同步：用户可能用 Win+↓、Snap Layouts 等方式改变状态，
    // 这种情况不走我们的补间，需要把内部状态纠正回来
    if (m_anim != Anim::Geometry)
        m_maximized = isMaximized();

    // 从最小化恢复：淡入。淡出结束时透明度已为 0，因此不会闪一帧
    m_animatingMinimize = false;
    if (windowOpacity() < 1.0)
        startFadeAnim(0.0, 1.0, 160);
}
