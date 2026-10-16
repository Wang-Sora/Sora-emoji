#ifndef SORA_AUDIOPLAYER_H
#define SORA_AUDIOPLAYER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

class QMediaPlayer;
class QAudioOutput;

/**
 * 播放器封装：内部持有 QMediaPlayer + QAudioOutput，并维护一个简单的播放列表，
 * 供底部播放条的「上一首 / 下一首」使用。
 *
 * 只依赖 QtMultimedia，不掺任何界面代码，方便以后换成别的后端。
 */
class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    /** 播放模式：顺序（播完即停）/ 列表循环 / 单曲循环 / 随机 */
    enum class PlayMode { Sequential, LoopAll, LoopOne, Shuffle };
    Q_ENUM(PlayMode)

    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    void setPlaylist(const QStringList &files);
    QStringList playlist() const { return m_playlist; }

    void playIndex(int index);
    void playFile(const QString &filePath);
    void togglePlayPause();
    void next();
    void previous();
    void stop();
    void seek(qint64 ms);

    void setVolume(int percent); // 0..100
    int volume() const;

    void setPlayMode(PlayMode mode);
    PlayMode playMode() const { return m_mode; }

    bool isPlaying() const;
    int currentIndex() const { return m_index; }
    QString currentTitle() const;
    QString currentFile() const;

    /**
     * 用另一个不发声的 QMediaPlayer 逐首读取内嵌元数据（标题/歌手/专辑/封面），
     * 每读完一首发一次 metadataReady。读元数据要把整个文件头解析一遍，
     * 放在主播放器上会打断正在播的曲子，所以单开一个实例串行做。
     * index 就是传入列表的下标，与播放列表下标一致。
     */
    void probeMetadata(const QStringList &files);

Q_SIGNALS:
    void progressChanged(qint64 positionMs, qint64 durationMs);
    void playingChanged(bool playing);
    void trackChanged(const QString &title);
    void errorOccurred(const QString &message);
    void playModeChanged(AudioPlayer::PlayMode mode);
    void metadataReady(int index, const QString &title, const QString &artist,
                       const QString &album, const QImage &cover);

private:
    void applySource(const QString &filePath);
    void handleEndOfMedia();
    void startNextProbe();
    void readProbeMetadata();
    int randomIndex() const;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;

    QMediaPlayer *m_probe = nullptr;
    QStringList m_probeQueue;
    QString m_probeCurrent;
    int m_probeCursor = -1;
    bool m_probeEmitted = true;

    QStringList m_playlist;
    int m_index = -1;
    int m_volume = 60;
    PlayMode m_mode = PlayMode::LoopAll;
};

#endif // SORA_AUDIOPLAYER_H
