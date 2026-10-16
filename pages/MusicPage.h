#ifndef SORA_MUSICPAGE_H
#define SORA_MUSICPAGE_H

#include <QImage>
#include <QList>
#include <QStringList>
#include <QVector>
#include <QWidget>

class AudioPlayer;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QShowEvent;
class QSplitter;
class QToolButton;
class QVBoxLayout;

/**
 * 音乐页：左侧搜索 + 曲目列表，右侧大封面 + 曲名/歌手 + 歌词。
 *
 * 曲库 = 系统「音乐」目录（QStandardPaths::MusicLocation）+ 用户拖进来/导入的文件，
 * 后者记在 QSettings 里，重开还在。歌词是同目录同名的 .lrc。
 */
class MusicPage : public QWidget
{
    Q_OBJECT

public:
    explicit MusicPage(AudioPlayer *player, QWidget *parent = nullptr);

    void retint();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /** 一首曲子的展示信息，元数据缺失时各字段为空 */
    struct TrackMeta
    {
        QString title;
        QString artist;
        QString album;
        QImage cover;
    };

    void buildUi();
    void loadLibrary();
    void addFiles(const QStringList &files);
    void chooseFiles();
    void applyFilter(const QString &keyword);
    void onItemActivated(QListWidgetItem *item);
    void showTrack();
    void resetStage();
    void applyMetadata(int index, const QString &title, const QString &artist,
                       const QString &album, const QImage &cover);
    void updateCover(const QImage &cover);
    void cyclePlayMode();
    void syncPlayModeButton();
    void loadLyrics(const QString &audioPath);
    void updateLyrics(qint64 positionMs);

    AudioPlayer *m_player = nullptr;

    QSplitter *m_splitter = nullptr;
    bool m_splitterRestored = false;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_cover = nullptr;
    QLabel *m_songTitle = nullptr;
    QLabel *m_songArtist = nullptr;
    QToolButton *m_modeButton = nullptr;
    QScrollArea *m_lyricScroll = nullptr;
    QVBoxLayout *m_lyricLayout = nullptr;
    QVector<QLabel *> m_lyricLabels;

    QStringList m_files;
    QVector<TrackMeta> m_meta;

    QVector<qint64> m_lyricTimes;
    int m_lyricIndex = -1;
    qint64 m_position = 0;
};

#endif // SORA_MUSICPAGE_H
