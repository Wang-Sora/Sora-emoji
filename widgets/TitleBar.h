#ifndef SORA_TITLEBAR_H
#define SORA_TITLEBAR_H

#include <QFrame>
#include <QPoint>

class QLabel;
class QPushButton;

/**
 * 自定义标题栏（无边框窗口）。
 *
 * 基类用 QFrame 而不是裸 QWidget：Qt 对 QFrame 默认会绘制样式表背景，
 * 省掉手动设置 WA_StyledBackground 的坑。
 *
 * 拖拽走 QWindow::startSystemMove()，这是 Windows 上唯一能保留系统
 * 吸附/贴边/多屏 DPI 行为的方式，自己算坐标迁移窗口会有各种抖动。
 */
class TitleBar : public QFrame
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);

Q_SIGNALS:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QLabel *m_title = nullptr;
};

#endif // SORA_TITLEBAR_H
