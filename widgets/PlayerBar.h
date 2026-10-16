#ifndef SORA_PLAYERBAR_H
#define SORA_PLAYERBAR_H

#include <QFrame>
#include <QImage>
#include <QPixmap>

class AudioPlayer;
class QLabel;
class QSlider;
class QToolButton;

/**
 * 底部播放条：缩略图 + 曲名/歌手 + 传输控制 + 进度条 + 音量。
 * 只跟 AudioPlayer 打交道，不感知播放列表是从哪来的。
 */
class PlayerBar : public QFrame
{
    Q_OBJECT

public:
    explicit PlayerBar(AudioPlayer *player, QWidget *parent = nullptr);

    void retint();

private:
    void syncPlayIcon();
    void setTimeLabel(qint64 positionMs, qint64 durationMs);
    void showPlaceholderCover();
    void applyMetadata(const QString &title, const QString &artist, const QImage &cover);
    static QString formatTime(qint64 ms);

    AudioPlayer *m_player = nullptr;

    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_artist = nullptr;
    QLabel *m_time = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_play = nullptr;
    QToolButton *m_next = nullptr;
    QSlider *m_progress = nullptr;
    QSlider *m_volume = nullptr;

    bool m_userSeeking = false;
};

#endif // SORA_PLAYERBAR_H
