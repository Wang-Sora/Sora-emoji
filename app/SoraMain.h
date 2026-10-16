#ifndef SORA_SORAMAIN_H
#define SORA_SORAMAIN_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QRect>

class AiClient;
class AudioPlayer;
class PlayerBar;
class QTimer;
class SideBar;
class TitleBar;
class QStackedWidget;

/**
 * 主窗口：无边框 + 自定义标题栏，负责把导航栏、页面栈、播放条装配到一起。
 *
 * 页面顺序：0 音乐 / 1 PDF / 2 AI / 3 设置。
 */
class SoraMain : public QMainWindow
{
    Q_OBJECT

public:
    explicit SoraMain(QWidget *parent = nullptr);
    ~SoraMain() override;

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildUi();
    void restoreWindowState();
    void setPage(int index);
    void toggleMaximize();
    void minimizeAnimated();

    // 窗口动效统一由一个 QTimer 驱动，节拍取屏幕刷新率。
    // Qt 默认的动画节拍固定 16ms，在 144Hz 这类高刷屏上会和垂直同步错拍，
    // 观感就是一卡一卡；这里改成按刷新率采样 + 用 QElapsedTimer 计时，
    // 即便某帧被系统拖慢，动画的总时长与进度依然是准的。
    enum class Anim { None, Geometry, Fade };
    void tickAnim();
    void finishAnim();
    void startGeometryAnim(const QRect &from, const QRect &to);
    void startFadeAnim(qreal from, qreal to, int ms);
    int animIntervalMs() const;

    AudioPlayer *m_audio = nullptr;
    AiClient *m_ai = nullptr;

    TitleBar *m_titleBar = nullptr;
    SideBar *m_sidebar = nullptr;
    QStackedWidget *m_stack = nullptr;
    PlayerBar *m_playerBar = nullptr;

    QTimer *m_animTimer = nullptr;
    QElapsedTimer m_animClock;
    Anim m_anim = Anim::None;
    int m_animDuration = 0;

    QRect m_geoFrom;
    QRect m_geoTo;
    qreal m_fadeFrom = 1.0;
    qreal m_fadeTo = 1.0;

    QRect m_normalGeometry;
    bool m_maximized = false;
    bool m_animatingMinimize = false;
};

#endif // SORA_SORAMAIN_H
