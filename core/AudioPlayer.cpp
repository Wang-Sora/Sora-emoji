#include "core/AudioPlayer.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_probe(new QMediaPlayer(this))
{
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(m_volume / 100.0);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        emit progressChanged(pos, m_player->duration());
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        emit progressChanged(m_player->position(), dur);
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                emit playingChanged(state == QMediaPlayer::PlayingState);
            });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia)
                    handleEndOfMedia();
            });

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &errorString) {
                if (!errorString.isEmpty())
                    emit errorOccurred(errorString);
            });

    // 元数据探测：一个文件只会被认领一次。setSource 之后状态机会先后抛
    // LoadingMedia / LoadedMedia / BufferedMedia，同一首歌可能来好几次终态，
    // 用「当前源 + 已发出」两个字段把重复和上一首的迟到事件都挡掉。
    connect(m_probe, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                const bool terminal = status == QMediaPlayer::LoadedMedia
                                      || status == QMediaPlayer::BufferedMedia
                                      || status == QMediaPlayer::InvalidMedia;
                if (!terminal)
                    return;
                if (m_probeEmitted || m_probe->source().toLocalFile() != m_probeCurrent)
                    return;
                if (m_probeCursor < 0 || m_probeCursor >= m_probeQueue.size())
                    return;

                m_probeEmitted = true;
                readProbeMetadata();
                // 让本轮的残余事件先跑完，再切下一首
                QTimer::singleShot(0, this, &AudioPlayer::startNextProbe);
            });
}

AudioPlayer::~AudioPlayer() = default;

void AudioPlayer::setPlaylist(const QStringList &files)
{
    // 保留正在播放的曲目：列表变化（比如又拖进来几首）不应该把当前曲目标成"没在播"
    const QString current = currentFile();
    m_playlist = files;
    m_index = current.isEmpty() ? -1 : m_playlist.indexOf(current);
}

void AudioPlayer::playIndex(int index)
{
    if (index < 0 || index >= m_playlist.size()) {
        emit errorOccurred(QStringLiteral("播放列表中没有这一项"));
        return;
    }

    m_index = index;
    applySource(m_playlist.at(index));
    m_player->play();
}

void AudioPlayer::playFile(const QString &filePath)
{
    const int index = m_playlist.indexOf(filePath);
    if (index >= 0) {
        playIndex(index);
        return;
    }

    m_index = -1;
    applySource(filePath);
    m_player->play();
}

void AudioPlayer::applySource(const QString &filePath)
{
    m_player->setSource(QUrl::fromLocalFile(filePath));
    emit trackChanged(QFileInfo(filePath).completeBaseName());
}

void AudioPlayer::togglePlayPause()
{
    if (m_player->source().isEmpty() && m_index < 0) {
        if (!m_playlist.isEmpty())
            playIndex(0);
        return;
    }

    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void AudioPlayer::handleEndOfMedia()
{
    if (m_playlist.isEmpty())
        return;

    const int count = m_playlist.size();

    switch (m_mode) {
    case PlayMode::LoopOne:
        // 单曲循环：不走 playIndex，避免重建整套解码管线带来的爆音
        m_player->setPosition(0);
        m_player->play();
        return;
    case PlayMode::Shuffle:
        playIndex(randomIndex());
        return;
    case PlayMode::Sequential:
        if (m_index + 1 >= count) {
            m_player->stop();
            return;
        }
        playIndex(m_index + 1);
        return;
    case PlayMode::LoopAll:
        playIndex((m_index + 1) % count);
        return;
    }
}

int AudioPlayer::randomIndex() const
{
    const int count = m_playlist.size();
    if (count <= 1)
        return 0;

    int index = m_index;
    while (index == m_index)
        index = QRandomGenerator::global()->bounded(count);
    return index;
}

void AudioPlayer::next()
{
    if (m_playlist.isEmpty())
        return;

    if (m_index < 0) {
        playIndex(0);
        return;
    }

    if (m_mode == PlayMode::Shuffle) {
        playIndex(randomIndex());
        return;
    }

    playIndex((m_index + 1) % m_playlist.size());
}

void AudioPlayer::previous()
{
    if (m_playlist.isEmpty())
        return;

    const int count = m_playlist.size();
    if (m_index < 0) {
        playIndex(0);
        return;
    }

    // 播放超过 3 秒时，先回到本曲开头
    if (m_player->position() > 3000) {
        m_player->setPosition(0);
        return;
    }

    playIndex((m_index - 1 + count) % count);
}

void AudioPlayer::stop()
{
    m_player->stop();
}

void AudioPlayer::seek(qint64 ms)
{
    m_player->setPosition(ms);
}

void AudioPlayer::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    m_audio->setVolume(m_volume / 100.0);
}

int AudioPlayer::volume() const
{
    return m_volume;
}

void AudioPlayer::setPlayMode(PlayMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    emit playModeChanged(m_mode);
}

bool AudioPlayer::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

QString AudioPlayer::currentTitle() const
{
    const QString file = currentFile();
    return file.isEmpty() ? QString() : QFileInfo(file).completeBaseName();
}

QString AudioPlayer::currentFile() const
{
    if (m_index >= 0 && m_index < m_playlist.size())
        return m_playlist.at(m_index);

    const QUrl url = m_player->source();
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

void AudioPlayer::probeMetadata(const QStringList &files)
{
    m_probeQueue = files;
    m_probeCursor = -1;
    m_probeCurrent.clear();
    m_probeEmitted = true;
    startNextProbe();
}

void AudioPlayer::startNextProbe()
{
    if (m_probeQueue.isEmpty())
        return;

    ++m_probeCursor;
    if (m_probeCursor >= m_probeQueue.size()) {
        m_probeQueue.clear();
        m_probeCursor = -1;
        m_probeCurrent.clear();
        m_probeEmitted = true;
        return;
    }

    m_probeCurrent = m_probeQueue.at(m_probeCursor);
    m_probeEmitted = false;
    m_probe->setSource(QUrl::fromLocalFile(m_probeCurrent));
}

void AudioPlayer::readProbeMetadata()
{
    const QMediaMetaData md = m_probe->metaData();

    QString artist = md.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = md.stringValue(QMediaMetaData::AlbumArtist);

    QImage cover;
    const QVariant art = md.value(QMediaMetaData::CoverArtImage);
    if (art.canConvert<QImage>())
        cover = art.value<QImage>();
    if (cover.isNull()) {
        const QVariant thumb = md.value(QMediaMetaData::ThumbnailImage);
        if (thumb.canConvert<QImage>())
            cover = thumb.value<QImage>();
    }

    emit metadataReady(m_probeCursor, md.stringValue(QMediaMetaData::Title), artist,
                       md.stringValue(QMediaMetaData::AlbumTitle), cover);
}
