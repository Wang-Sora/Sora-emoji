#include "pages/MusicPage.h"

#include "app/ThemeManager.h"
#include "core/AudioPlayer.h"
#include "core/TintedIcon.h"

#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

const QStringList kAudioSuffixes = {
    QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
    QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("ogg"),
};

/** 大封面：有内嵌封面就等比裁切后画进圆角，没有就用主题色渐变兜底 */
QPixmap coverArt(int size, const QColor &accent, const QImage &image)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), 18, 18);
    painter.setClipPath(clip);

    if (!image.isNull()) {
        const QImage scaled = image.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        painter.drawImage(QRect(0, 0, size, size), scaled,
                          QRect((scaled.width() - size) / 2, (scaled.height() - size) / 2,
                                size, size));
        return pixmap;
    }

    QLinearGradient gradient(0, 0, size, size);
    QColor start = accent;
    start.setAlpha(150);
    QColor mid = accent;
    mid.setAlpha(60);
    QColor end = accent;
    end.setAlpha(20);
    gradient.setColorAt(0.0, start);
    gradient.setColorAt(0.55, mid);
    gradient.setColorAt(1.0, end);

    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 18, 18);

    return pixmap;
}

QString humanName(const QString &filePath)
{
    return QFileInfo(filePath).completeBaseName();
}

/** LRC 里中文常见 GBK，UTF-8 解出替换字符时退回本地编码 */
QString readLyricFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    const QByteArray raw = file.readAll();
    QString text = QString::fromUtf8(raw);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLocal8Bit(raw);
    return text;
}

/** 解析 LRC：一行可挂多个时间标签，毫秒按两位=百分秒 / 三位=毫秒解释 */
QVector<QPair<qint64, QString>> parseLrc(const QString &text)
{
    static const QRegularExpression tag(
        QStringLiteral(R"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])"));

    QVector<QPair<qint64, QString>> lines;

    const QStringList raw = text.split(QRegularExpression(QStringLiteral("[\r\n]")),
                                       Qt::SkipEmptyParts);
    for (const QString &line : raw) {
        QVector<qint64> stamps;
        int lastEnd = 0;

        auto matches = tag.globalMatch(line);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString fraction = match.captured(3);
            qint64 ms = 0;
            if (!fraction.isEmpty())
                ms = fraction.toLongLong() * (fraction.size() == 2 ? 10 : 1);

            stamps.append((match.captured(1).toLongLong() * 60
                           + match.captured(2).toLongLong()) * 1000 + ms);
            lastEnd = match.capturedEnd();
        }

        if (stamps.isEmpty())
            continue;

        const QString content = line.mid(lastEnd).trimmed();
        for (qint64 stamp : std::as_const(stamps))
            lines.append({stamp, content});
    }

    std::sort(lines.begin(), lines.end(),
              [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                  return a.first < b.first;
              });

    return lines;
}

/** 把拖进来的文件/文件夹摊平成音频文件列表，文件夹递归扫 */
QStringList collectAudioFiles(const QStringList &paths)
{
    QStringList out;

    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                if (kAudioSuffixes.contains(QFileInfo(file).suffix().toLower()))
                    out.append(file);
            }
        } else if (info.isFile() && kAudioSuffixes.contains(info.suffix().toLower())) {
            out.append(info.absoluteFilePath());
        }
    }

    out.removeDuplicates();
    return out;
}

} // namespace

MusicPage::MusicPage(AudioPlayer *player, QWidget *parent)
    : QWidget(parent)
    , m_player(player)
{
    setAcceptDrops(true);
    buildUi();

    static const int kModeDefault = static_cast<int>(AudioPlayer::PlayMode::LoopAll);
    const int saved = QSettings().value(QStringLiteral("music/playMode"), kModeDefault).toInt();
    if (saved >= static_cast<int>(AudioPlayer::PlayMode::Sequential)
        && saved <= static_cast<int>(AudioPlayer::PlayMode::Shuffle)) {
        m_player->setPlayMode(static_cast<AudioPlayer::PlayMode>(saved));
    }
    syncPlayModeButton();

    loadLibrary();

    connect(m_player, &AudioPlayer::trackChanged, this, &MusicPage::showTrack);
    connect(m_player, &AudioPlayer::metadataReady, this, &MusicPage::applyMetadata);
    connect(m_player, &AudioPlayer::playModeChanged, this, &MusicPage::syncPlayModeButton);

    connect(m_player, &AudioPlayer::progressChanged, this,
            [this](qint64 position, qint64) {
                m_position = position;
                updateLyrics(position);
            });

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &MusicPage::retint);

    retint();
}

void MusicPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 左列和右侧舞台交给 QSplitter，拖动中间的分隔条就能调曲库宽度。
    // 宽度上下限写在左列的 min/max 上，分隔条本身在 QSS 里画成一条悬停才亮起的细槽。
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("LibrarySplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ---- 左：搜索 + 曲目列表 ----
    auto *column = new QFrame(this);
    column->setObjectName(QStringLiteral("ContextColumn"));
    column->setMinimumWidth(180);
    column->setMaximumWidth(440);

    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(14, 16, 14, 16);
    columnLayout->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    auto *heading = new QLabel(QStringLiteral("音乐库"), column);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    header->addWidget(heading);
    header->addStretch(1);

    m_modeButton = new QToolButton(column);
    m_modeButton->setObjectName(QStringLiteral("Ghost"));
    m_modeButton->setCursor(Qt::PointingHandCursor);
    m_modeButton->setFixedHeight(26);
    m_modeButton->setToolTip(QStringLiteral("播放模式：顺序 / 列表循环 / 单曲循环 / 随机"));
    header->addWidget(m_modeButton);

    auto *importButton = new QToolButton(column);
    importButton->setObjectName(QStringLiteral("Ghost"));
    importButton->setCursor(Qt::PointingHandCursor);
    importButton->setFixedHeight(26);
    importButton->setText(QStringLiteral("导入"));
    importButton->setToolTip(QStringLiteral("添加音乐文件或文件夹（也可以直接把文件拖进本页）"));
    header->addWidget(importButton);

    columnLayout->addLayout(header);

    m_search = new QLineEdit(column);
    m_search->setPlaceholderText(QStringLiteral("搜索歌曲…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(TintedIcon::create(QStringLiteral(":/icons/search.svg"),
                                           ThemeManager::instance().iconIdle(), 16),
                        QLineEdit::LeadingPosition);
    columnLayout->addWidget(m_search);

    m_list = new QListWidget(column);
    m_list->setObjectName(QStringLiteral("TrackList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    columnLayout->addWidget(m_list, 1);

    auto *hint = new QLabel(QStringLiteral("把音频文件或文件夹拖进来即可加入曲库"), column);
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    columnLayout->addWidget(hint);

    m_splitter->addWidget(column);

    // ---- 右：封面 + 曲名/歌手 + 歌词 ----
    auto *stage = new QWidget(this);
    stage->setObjectName(QStringLiteral("Stage"));

    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(32, 28, 32, 28);
    stageLayout->setSpacing(14);

    m_cover = new QLabel(stage);
    m_cover->setObjectName(QStringLiteral("CoverArt"));
    m_cover->setAlignment(Qt::AlignCenter);
    stageLayout->addWidget(m_cover, 0, Qt::AlignHCenter);

    m_songTitle = new QLabel(QStringLiteral("还没有选择歌曲"), stage);
    m_songTitle->setObjectName(QStringLiteral("SongTitle"));
    m_songTitle->setAlignment(Qt::AlignCenter);
    stageLayout->addWidget(m_songTitle);

    m_songArtist = new QLabel(QStringLiteral("从左侧列表中点击一首开始播放"), stage);
    m_songArtist->setObjectName(QStringLiteral("SongArtist"));
    m_songArtist->setAlignment(Qt::AlignCenter);
    stageLayout->addWidget(m_songArtist);

    stageLayout->addSpacing(10);

    m_lyricScroll = new QScrollArea(stage);
    m_lyricScroll->setObjectName(QStringLiteral("LyricScroll"));
    m_lyricScroll->setWidgetResizable(true);
    m_lyricScroll->setFrameShape(QFrame::NoFrame);
    m_lyricScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricScroll->viewport()->setAutoFillBackground(false);

    auto *lyricHost = new QWidget;
    lyricHost->setObjectName(QStringLiteral("LyricsHost"));
    lyricHost->setAutoFillBackground(false);

    m_lyricLayout = new QVBoxLayout(lyricHost);
    m_lyricLayout->setContentsMargins(0, 0, 0, 0);
    m_lyricLayout->setSpacing(8);
    m_lyricLayout->addStretch(1);
    m_lyricLayout->addStretch(1);

    m_lyricScroll->setWidget(lyricHost);
    stageLayout->addWidget(m_lyricScroll, 1);

    m_splitter->addWidget(stage);
    // 权重全给舞台：窗口变宽时多出来的空间归右侧，左列保持用户拖出来的宽度
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    root->addWidget(m_splitter);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings().setValue(QStringLiteral("music/libraryWidth"), m_splitter->sizes().value(0));
    });

    connect(m_search, &QLineEdit::textChanged, this, &MusicPage::applyFilter);
    connect(m_list, &QListWidget::itemActivated, this, &MusicPage::onItemActivated);
    connect(m_list, &QListWidget::itemClicked, this, &MusicPage::onItemActivated);
    connect(importButton, &QToolButton::clicked, this, &MusicPage::chooseFiles);
    connect(m_modeButton, &QToolButton::clicked, this, &MusicPage::cyclePlayMode);
}

void MusicPage::loadLibrary()
{
    QStringList files;

    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::MusicLocation);
    for (const QString &location : locations) {
        QDir dir(location);
        if (!dir.exists())
            continue;

        const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                        QDir::Name);
        for (const QFileInfo &info : entries) {
            if (kAudioSuffixes.contains(info.suffix().toLower()))
                files.append(info.absoluteFilePath());
        }
    }

    const QStringList imported = QSettings().value(QStringLiteral("music/extraFiles")).toStringList();
    for (const QString &file : imported) {
        if (QFileInfo::exists(file) && !files.contains(file))
            files.append(file);
    }

    m_files = files;
    m_meta = QVector<TrackMeta>(m_files.size());
    m_player->setPlaylist(m_files);

    m_list->clear();
    for (const QString &file : std::as_const(m_files)) {
        auto *item = new QListWidgetItem(humanName(file), m_list);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }

    if (m_files.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("（示例）还没有音乐文件"), m_list);
        item->setFlags(Qt::NoItemFlags);
        m_songArtist->setText(QStringLiteral("把音频拖进本页，或点「导入」挑选文件"));
    }

    applyFilter(m_search->text());

    // 元数据逐首异步补全，读到哪首刷哪首
    m_player->probeMetadata(m_files);

    if (m_player->currentIndex() < 0)
        resetStage();
    else
        showTrack();
}

void MusicPage::addFiles(const QStringList &files)
{
    if (files.isEmpty())
        return;

    QSettings settings;
    QStringList imported = settings.value(QStringLiteral("music/extraFiles")).toStringList();
    for (const QString &file : files) {
        if (!imported.contains(file))
            imported.append(file);
    }
    settings.setValue(QStringLiteral("music/extraFiles"), imported);

    loadLibrary();
}

void MusicPage::chooseFiles()
{
    const QString filter = QStringLiteral("音频文件 (*.mp3 *.flac *.wav *.m4a *.aac *.ogg);;所有文件 (*)");
    const QStringList picked = QFileDialog::getOpenFileNames(
        this, QStringLiteral("添加音乐到曲库"), QDir::homePath(), filter);

    addFiles(collectAudioFiles(picked));
}

void MusicPage::applyFilter(const QString &keyword)
{
    const QString needle = keyword.trimmed();
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item->data(Qt::UserRole).toString().isEmpty()) {
            item->setHidden(!needle.isEmpty());
            continue;
        }
        const bool hit = needle.isEmpty()
                         || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!hit);
    }
}

void MusicPage::onItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;

    const QString file = item->data(Qt::UserRole).toString();
    if (file.isEmpty())
        return;

    m_player->playFile(file);
}

void MusicPage::showTrack()
{
    const int index = m_player->currentIndex();
    if (index < 0 || index >= m_files.size()) {
        resetStage();
        return;
    }

    const TrackMeta &meta = m_meta.at(index);

    m_songTitle->setText(meta.title.isEmpty() ? humanName(m_files.at(index)) : meta.title);

    QStringList credits;
    if (!meta.artist.isEmpty())
        credits << meta.artist;
    if (!meta.album.isEmpty())
        credits << meta.album;
    m_songArtist->setText(credits.isEmpty() ? QStringLiteral("本地音乐")
                                            : credits.join(QStringLiteral(" · ")));

    updateCover(meta.cover);
    loadLyrics(m_files.at(index));

    if (index < m_list->count() && m_list->currentRow() != index)
        m_list->setCurrentRow(index);
}

void MusicPage::resetStage()
{
    m_songTitle->setText(QStringLiteral("还没有选择歌曲"));
    if (m_files.isEmpty())
        m_songArtist->setText(QStringLiteral("把音频拖进本页，或点「导入」挑选文件"));
    else
        m_songArtist->setText(QStringLiteral("从左侧列表中点击一首开始播放"));

    updateCover(QImage());
    loadLyrics(QString());
}

void MusicPage::applyMetadata(int index, const QString &title, const QString &artist,
                              const QString &album, const QImage &cover)
{
    if (index < 0 || index >= m_meta.size())
        return;

    TrackMeta &meta = m_meta[index];
    meta.title = title;
    meta.artist = artist;
    meta.album = album;
    meta.cover = cover;

    if (QListWidgetItem *item = m_list->item(index)) {
        item->setText(title.isEmpty() ? humanName(m_files.at(index)) : title);
        item->setToolTip(artist.isEmpty() ? m_files.at(index)
                                          : artist + QStringLiteral(" - ") + m_files.at(index));
    }

    if (!m_search->text().trimmed().isEmpty())
        applyFilter(m_search->text());

    if (index == m_player->currentIndex())
        showTrack();
}

void MusicPage::updateCover(const QImage &cover)
{
    m_cover->setPixmap(coverArt(168, ThemeManager::instance().accent(), cover));
}

void MusicPage::cyclePlayMode()
{
    using Mode = AudioPlayer::PlayMode;
    static const Mode kOrder[] = {Mode::LoopAll, Mode::LoopOne, Mode::Shuffle, Mode::Sequential};
    constexpr int kCount = int(sizeof(kOrder) / sizeof(kOrder[0]));

    const Mode current = m_player->playMode();
    Mode next = kOrder[0];
    for (int i = 0; i < kCount; ++i) {
        if (kOrder[i] == current) {
            next = kOrder[(i + 1) % kCount];
            break;
        }
    }

    m_player->setPlayMode(next);
}

void MusicPage::syncPlayModeButton()
{
    using Mode = AudioPlayer::PlayMode;
    switch (m_player->playMode()) {
    case Mode::Sequential:
        m_modeButton->setText(QStringLiteral("顺序"));
        break;
    case Mode::LoopAll:
        m_modeButton->setText(QStringLiteral("循环"));
        break;
    case Mode::LoopOne:
        m_modeButton->setText(QStringLiteral("单曲"));
        break;
    case Mode::Shuffle:
        m_modeButton->setText(QStringLiteral("随机"));
        break;
    }

    QSettings().setValue(QStringLiteral("music/playMode"), static_cast<int>(m_player->playMode()));
}

void MusicPage::loadLyrics(const QString &audioPath)
{
    m_lyricTimes.clear();
    m_lyricIndex = -1;

    for (QLabel *label : std::as_const(m_lyricLabels)) {
        m_lyricLayout->removeWidget(label);
        label->deleteLater();
    }
    m_lyricLabels.clear();

    QStringList texts;

    if (audioPath.isEmpty()) {
        texts << QStringLiteral("还没有播放中的曲目");
    } else {
        const QFileInfo info(audioPath);
        const QString lyricPath = info.absolutePath() + QLatin1Char('/')
                                  + info.completeBaseName() + QStringLiteral(".lrc");
        const QVector<QPair<qint64, QString>> lines = parseLrc(readLyricFile(lyricPath));

        if (lines.isEmpty()) {
            texts << QStringLiteral("暂无歌词")
                  << QStringLiteral("把同名 .lrc 文件放在音频同目录就能滚动显示");
        } else {
            for (const QPair<qint64, QString> &line : lines) {
                m_lyricTimes.append(line.first);
                texts << (line.second.isEmpty() ? QStringLiteral("♪") : line.second);
            }
        }
    }

    for (const QString &text : std::as_const(texts)) {
        auto *label = new QLabel(text, m_lyricScroll->widget());
        label->setObjectName(QStringLiteral("Lyrics"));
        label->setAlignment(Qt::AlignHCenter);
        label->setWordWrap(true);
        // 插在首尾两个弹簧之间，歌词少时居中、多时可滚
        m_lyricLayout->insertWidget(m_lyricLayout->count() - 1, label);
        m_lyricLabels.append(label);
    }

    m_lyricScroll->verticalScrollBar()->setValue(0);
    updateLyrics(m_position);
}

void MusicPage::updateLyrics(qint64 positionMs)
{
    if (m_lyricTimes.isEmpty())
        return;

    const auto it = std::upper_bound(m_lyricTimes.cbegin(), m_lyricTimes.cend(), positionMs);
    const int index = int(it - m_lyricTimes.cbegin()) - 1;
    if (index < 0 || index == m_lyricIndex)
        return;

    m_lyricIndex = index;

    for (int i = 0; i < m_lyricLabels.size(); ++i) {
        QLabel *label = m_lyricLabels.at(i);
        const bool active = (i == index);
        if (label->property("active").toBool() == active)
            continue;
        label->setProperty("active", active);
        label->style()->unpolish(label);
        label->style()->polish(label);
    }

    m_lyricScroll->ensureWidgetVisible(m_lyricLabels.at(index), 0,
                                       m_lyricScroll->viewport()->height() / 3);
}

void MusicPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (m_splitterRestored)
        return;
    m_splitterRestored = true;

    // showEvent 触发时 QSplitter 还没拿到最终几何，此刻 setSizes 的像素值会被
    // 当成比例放大（左列会一路顶到最大宽度）。等这轮事件循环把布局跑完再套用。
    QTimer::singleShot(0, this, [this] {
        QWidget *column = m_splitter->widget(0);
        if (!column)
            return;

        const int saved = QSettings().value(QStringLiteral("music/libraryWidth"), 248).toInt();
        const int width = qBound(column->minimumWidth(), saved, column->maximumWidth());

        m_splitter->setSizes({ width, qMax(1, m_splitter->width() - width) });
    });
}

void MusicPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MusicPage::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MusicPage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : std::as_const(urls)) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }

    const QStringList files = collectAudioFiles(paths);
    if (files.isEmpty()) {
        event->ignore();
        return;
    }

    addFiles(files);
    event->acceptProposedAction();
}

void MusicPage::retint()
{
    const int index = m_player->currentIndex();
    updateCover(index >= 0 && index < m_meta.size() ? m_meta.at(index).cover : QImage());

    if (m_search) {
        const QList<QAction *> actions = m_search->actions();
        if (!actions.isEmpty()) {
            actions.first()->setIcon(TintedIcon::create(QStringLiteral(":/icons/search.svg"),
                                                        ThemeManager::instance().iconIdle(), 16));
        }
    }
}
