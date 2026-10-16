#include "widgets/PlayerBar.h"

#include "app/ThemeManager.h"
#include "core/AudioPlayer.h"
#include "core/TintedIcon.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QPixmap placeholderCover(int size, const QColor &accent)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, size, size);
    QColor start = accent;
    start.setAlpha(90);
    QColor end = accent;
    end.setAlpha(30);
    gradient.setColorAt(0.0, start);
    gradient.setColorAt(1.0, end);

    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 8, 8);

    return pixmap;
}

} // namespace

PlayerBar::PlayerBar(AudioPlayer *player, QWidget *parent)
    : QFrame(parent)
    , m_player(player)
{
    setObjectName(QStringLiteral("PlayerBar"));
    setFixedHeight(72);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 18, 10);
    layout->setSpacing(14);

    auto *cover = new QLabel(this);
    cover->setObjectName(QStringLiteral("CoverThumb"));
    cover->setFixedSize(48, 48);
    cover->setAlignment(Qt::AlignCenter);
    layout->addWidget(cover);
    m_cover = cover;

    auto *info = new QVBoxLayout;
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(2);

    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setObjectName(QStringLiteral("TrackTitle"));
    m_artist = new QLabel(QStringLiteral("从「音乐」里挑一首开始"), this);
    m_artist->setObjectName(QStringLiteral("TrackArtist"));
    info->addWidget(m_title);
    info->addWidget(m_artist);
    layout->addLayout(info);

    layout->addSpacing(10);

    auto *prev = new QToolButton(this);
    prev->setObjectName(QStringLiteral("Transport"));
    prev->setCursor(Qt::PointingHandCursor);
    prev->setFixedSize(32, 32);
    prev->setIconSize(QSize(18, 18));
    layout->addWidget(prev);
    m_prev = prev;

    m_play = new QToolButton(this);
    m_play->setObjectName(QStringLiteral("TransportPrimary"));
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setFixedSize(38, 38);
    m_play->setIconSize(QSize(18, 18));
    layout->addWidget(m_play);

    auto *next = new QToolButton(this);
    next->setObjectName(QStringLiteral("Transport"));
    next->setCursor(Qt::PointingHandCursor);
    next->setFixedSize(32, 32);
    next->setIconSize(QSize(18, 18));
    layout->addWidget(next);
    m_next = next;

    m_progress = new QSlider(Qt::Horizontal, this);
    m_progress->setObjectName(QStringLiteral("ProgressSlider"));
    m_progress->setRange(0, 0);
    m_progress->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_progress, 1);

    m_time = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    m_time->setObjectName(QStringLiteral("TimeLabel"));
    layout->addWidget(m_time);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setObjectName(QStringLiteral("VolumeSlider"));
    m_volume->setRange(0, 100);
    m_volume->setValue(m_player->volume());
    m_volume->setFixedWidth(90);
    m_volume->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_volume);

    connect(prev, &QToolButton::clicked, m_player, &AudioPlayer::previous);
    connect(next, &QToolButton::clicked, m_player, &AudioPlayer::next);
    connect(m_play, &QToolButton::clicked, m_player, &AudioPlayer::togglePlayPause);

    connect(m_volume, &QSlider::valueChanged, m_player, &AudioPlayer::setVolume);

    connect(m_progress, &QSlider::sliderPressed, this, [this] { m_userSeeking = true; });
    connect(m_progress, &QSlider::sliderReleased, this, [this] {
        m_userSeeking = false;
        m_player->seek(m_progress->value());
    });
    connect(m_progress, &QSlider::sliderMoved, this, [this](int value) {
        setTimeLabel(value, m_progress->maximum());
    });

    connect(m_player, &AudioPlayer::progressChanged, this,
            [this](qint64 position, qint64 duration) {
                if (!m_userSeeking) {
                    m_progress->setRange(0, static_cast<int>(qMax<qint64>(duration, 0)));
                    m_progress->setValue(static_cast<int>(qMax<qint64>(position, 0)));
                }
                setTimeLabel(position, duration);
            });

    connect(m_player, &AudioPlayer::playingChanged, this,
            [this](bool) { syncPlayIcon(); });

    connect(m_player, &AudioPlayer::trackChanged, this, [this](const QString &title) {
        m_title->setText(title.isEmpty() ? QStringLiteral("未在播放") : title);
        m_artist->setText(QStringLiteral("读取中…"));
        showPlaceholderCover();
    });

    connect(m_player, &AudioPlayer::metadataReady, this,
            [this](int index, const QString &title, const QString &artist, const QString &,
                   const QImage &cover) {
                if (index != m_player->currentIndex())
                    return;
                applyMetadata(title, artist, cover);
            });

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &PlayerBar::retint);

    showPlaceholderCover();
    retint();
}

void PlayerBar::showPlaceholderCover()
{
    m_cover->setPixmap(placeholderCover(48, ThemeManager::instance().accent()));
}

void PlayerBar::applyMetadata(const QString &title, const QString &artist, const QImage &cover)
{
    if (!title.isEmpty())
        m_title->setText(title);

    if (!artist.isEmpty())
        m_artist->setText(artist);
    else if (m_artist->text() == QStringLiteral("读取中…"))
        m_artist->setText(QStringLiteral("未知歌手"));

    if (cover.isNull()) {
        showPlaceholderCover();
        return;
    }

    const QPixmap source = QPixmap::fromImage(cover).scaled(
        48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    QPixmap rounded(48, 48);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, 48, 48), 8, 8);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, source, (source.width() - 48) / 2, (source.height() - 48) / 2,
                       48, 48);
    m_cover->setPixmap(rounded);
}

void PlayerBar::syncPlayIcon()
{
    const bool playing = m_player->isPlaying();
    const QString path = playing ? QStringLiteral(":/icons/pause.svg")
                                 : QStringLiteral(":/icons/play.svg");
    m_play->setIcon(TintedIcon::create(path, QColor(QStringLiteral("#052E18")), 18));
}

void PlayerBar::retint()
{
    const QColor idle = ThemeManager::instance().iconIdle();

    m_prev->setIcon(TintedIcon::create(QStringLiteral(":/icons/prev.svg"), idle, 18));
    m_next->setIcon(TintedIcon::create(QStringLiteral(":/icons/next.svg"), idle, 18));

    syncPlayIcon();
}

void PlayerBar::setTimeLabel(qint64 positionMs, qint64 durationMs)
{
    m_time->setText(QStringLiteral("%1 / %2")
                        .arg(formatTime(positionMs), formatTime(durationMs)));
}

QString PlayerBar::formatTime(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("0:00");

    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
