#include "pages/StickersPage.h"

#include "core/StickerStore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// 网格外观：icon 96px，格子多留 20px 给文字和边框
constexpr int kIconSize = 96;
constexpr int kCellWidth = 118;
constexpr int kCellHeight = 140;

} // namespace

StickersPage::StickersPage(QWidget *parent)
    : QWidget(parent)
    , m_store(new StickerStore(this))
{
    buildUi();

    connect(m_store, &StickerStore::changed, this, &StickersPage::reloadGrid);
    connect(m_store, &StickerStore::imported, this, [this](const StickerStore::AddOutcome &outcome) {
        setStatus(outcome.message);
    });

    // 监听状态持久化：默认开，让第一次复制表情包就能看到效果
    const bool listen = QSettings().value(QStringLiteral("stickers/listen"), true).toBool();
    m_store->setListening(listen);

    reloadGrid();
    syncListenButton();
    updateCountLabel();
    setStatus(QStringLiteral("就绪 —— 复制一张表情包试试"));
}

void StickersPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 页头 ----
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("PageHeader"));
    header->setFixedHeight(52);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *title = new QLabel(QStringLiteral("表情库"), header);
    title->setObjectName(QStringLiteral("SectionTitle"));
    headerLayout->addWidget(title);

    headerLayout->addStretch(1);

    m_countLabel = new QLabel(header);
    m_countLabel->setObjectName(QStringLiteral("Hint"));
    headerLayout->addWidget(m_countLabel);

    root->addWidget(header);

    // ---- 左列 + 网格舞台 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("LibrarySplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    auto *column = new QFrame(m_splitter);
    column->setObjectName(QStringLiteral("ContextColumn"));
    column->setMinimumWidth(180);
    column->setMaximumWidth(440);

    auto *colLayout = new QVBoxLayout(column);
    colLayout->setContentsMargins(14, 16, 14, 16);
    colLayout->setSpacing(10);

    m_listenButton = new QToolButton(column);
    m_listenButton->setObjectName(QStringLiteral("Ghost"));
    m_listenButton->setCheckable(true);
    m_listenButton->setMinimumHeight(32);
    m_listenButton->setCursor(Qt::PointingHandCursor);
    connect(m_listenButton, &QToolButton::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("stickers/listen"), on);
        m_store->setListening(on);
        syncListenButton();
        updateCountLabel();
        setStatus(on ? QStringLiteral("监听已开启 —— 复制图片会自动入库")
                     : QStringLiteral("监听已关闭"));
    });
    colLayout->addWidget(m_listenButton);

    auto *importClip = new QToolButton(column);
    importClip->setObjectName(QStringLiteral("Ghost"));
    importClip->setText(QStringLiteral("从剪贴板导入"));
    importClip->setMinimumHeight(32);
    importClip->setCursor(Qt::PointingHandCursor);
    connect(importClip, &QToolButton::clicked, this, &StickersPage::importFromClipboard);
    colLayout->addWidget(importClip);

    auto *importFiles = new QToolButton(column);
    importFiles->setObjectName(QStringLiteral("Ghost"));
    importFiles->setText(QStringLiteral("导入图片文件…"));
    importFiles->setMinimumHeight(32);
    importFiles->setCursor(Qt::PointingHandCursor);
    connect(importFiles, &QToolButton::clicked, this, &StickersPage::importFiles);
    colLayout->addWidget(importFiles);

    // 存储位置：迁去别的盘/目录。迁移由 StickerStore 做（先复制后清理，失败不伤老库）
    auto *storageBtn = new QToolButton(column);
    storageBtn->setObjectName(QStringLiteral("Ghost"));
    storageBtn->setText(QStringLiteral("更改存储位置…"));
    storageBtn->setMinimumHeight(32);
    storageBtn->setCursor(Qt::PointingHandCursor);
    connect(storageBtn, &QToolButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择表情包存储位置"), m_store->storageRoot());
        if (dir.isEmpty())
            return;
        const QString error = m_store->migrateStorage(dir);
        if (!error.isEmpty()) {
            setStatus(error);
            return;
        }
        syncStoragePath();
        setStatus(QStringLiteral("已迁移到 %1").arg(
            QDir::toNativeSeparators(m_store->storageRoot())));
    });
    colLayout->addWidget(storageBtn);

    m_storagePath = new QLabel(column);
    m_storagePath->setObjectName(QStringLiteral("Hint"));
    m_storagePath->setWordWrap(true);
    syncStoragePath();
    colLayout->addWidget(m_storagePath);

    colLayout->addStretch(1);

    auto *tips = new QLabel(column);
    tips->setObjectName(QStringLiteral("Hint"));
    tips->setWordWrap(true);
    tips->setText(QStringLiteral("单击格子 = 复制到剪贴板\n右键格子 = 打开 / 删除\n把图片文件拖进右侧也能入库"));
    colLayout->addWidget(tips);

    m_splitter->addWidget(column);

    auto *stage = new QWidget(m_splitter);
    stage->setObjectName(QStringLiteral("Stage"));

    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(16, 16, 16, 10);
    stageLayout->setSpacing(8);

    m_grid = new QListWidget(stage);
    m_grid->setObjectName(QStringLiteral("StickerGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setSpacing(12);
    m_grid->setIconSize(QSize(kIconSize, kIconSize));
    m_grid->setGridSize(QSize(kCellWidth, kCellHeight));
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setDragEnabled(false); // 拖拽留给页面级 dropEvent 收文件，格子本身不拖
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QListWidget::itemClicked, this, &StickersPage::copyItem);
    connect(m_grid, &QListWidget::customContextMenuRequested, this, &StickersPage::showGridMenu);
    stageLayout->addWidget(m_grid, 1);

    m_status = new QLabel(stage);
    m_status->setObjectName(QStringLiteral("Hint"));
    stageLayout->addWidget(m_status);

    m_splitter->addWidget(stage);

    // 权重全给网格：窗口变宽时多出来的空间归右侧，左列保持用户拖出来的宽度
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    root->addWidget(m_splitter, 1);
}

void StickersPage::showEvent(QShowEvent *event)
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

        const int saved = QSettings().value(QStringLiteral("stickers/libraryWidth"), 220).toInt();
        const int width = qBound(column->minimumWidth(), saved, column->maximumWidth());

        m_splitter->setSizes({ width, qMax(1, m_splitter->width() - width) });
    });
}

void StickersPage::reloadGrid()
{
    m_grid->clear();

    const QList<StickerStore::StickerEntry> entries = m_store->entries();
    for (const StickerStore::StickerEntry &entry : entries) {
        QImage thumb(m_store->thumbPath(entry.hash));
        if (thumb.isNull())
            thumb = QImage(kIconSize, kIconSize, QImage::Format_ARGB32); // 占位：透明底

        auto *item = new QListWidgetItem(m_grid);
        item->setIcon(QIcon(QPixmap::fromImage(thumb)));
        item->setText(entry.ext.toUpper());
        item->setData(Qt::UserRole, entry.hash);

        const QString when = QDateTime::fromMSecsSinceEpoch(entry.addedMs)
                                 .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        item->setToolTip(QStringLiteral("%1 × %2 · %3%4\n来源：%5\n%6")
                             .arg(entry.width)
                             .arg(entry.height)
                             .arg(entry.ext.toUpper(),
                                  entry.animated ? QStringLiteral(" · 动图") : QString(),
                                  entry.source, when));
    }

    updateCountLabel();
}

void StickersPage::importFromClipboard()
{
    const StickerStore::AddOutcome outcome =
        m_store->addFromMime(QGuiApplication::clipboard()->mimeData());
    setStatus(outcome.message);
}

void StickersPage::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入图片"),
        QString(),
        QStringLiteral("图片 (*.jpg *.jpeg *.png *.gif *.webp *.bmp)"));
    if (files.isEmpty())
        return;

    int added = 0;
    int skipped = 0;
    QString lastMessage;
    for (const QString &file : files) {
        const StickerStore::AddOutcome outcome = m_store->addFromFile(file);
        lastMessage = outcome.message;
        if (outcome.status == StickerStore::AddStatus::Added)
            ++added;
        else if (outcome.status == StickerStore::AddStatus::Duplicate)
            ++skipped;
    }

    if (files.size() > 1)
        setStatus(QStringLiteral("导入完成：%1 张新入库，%2 张重复").arg(added).arg(skipped));
    else
        setStatus(lastMessage);
}

void StickersPage::copyItem(QListWidgetItem *item)
{
    if (!item)
        return;
    if (m_store->copyToClipboard(item->data(Qt::UserRole).toString()))
        setStatus(QStringLiteral("已复制到剪贴板，去聊天框粘贴吧"));
}

void StickersPage::showGridMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_grid->itemAt(pos);
    if (!item)
        return;

    const QString hash = item->data(Qt::UserRole).toString();
    const QString origin = m_store->originPath(hash);

    QMenu menu(m_grid);
    QAction *copyAction = menu.addAction(QStringLiteral("复制到剪贴板"));
    menu.addSeparator();
    QAction *openAction = menu.addAction(QStringLiteral("打开原文件"));
    QAction *revealAction = menu.addAction(QStringLiteral("在文件夹中显示"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("删除"));

    QAction *chosen = menu.exec(m_grid->mapToGlobal(pos));
    if (chosen == copyAction) {
        copyItem(item);
    } else if (chosen == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(origin));
    } else if (chosen == revealAction) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                { QStringLiteral("/select,") + QDir::toNativeSeparators(origin) });
    } else if (chosen == deleteAction) {
        if (m_store->removeSticker(hash))
            setStatus(QStringLiteral("已删除"));
    }
}

void StickersPage::setStatus(const QString &text)
{
    m_status->setText(text);
}

void StickersPage::syncListenButton()
{
    const bool on = m_store->isListening();
    m_listenButton->setChecked(on);
    m_listenButton->setText(on ? QStringLiteral("监听剪贴板 · 开")
                               : QStringLiteral("监听剪贴板 · 关"));
}

void StickersPage::syncStoragePath()
{
    if (!m_storagePath)
        return;
    const QString path = QDir::toNativeSeparators(m_store->storageRoot());
    m_storagePath->setText(QStringLiteral("当前：%1").arg(path));
    m_storagePath->setToolTip(path);
}

void StickersPage::updateCountLabel()
{
    const int count = m_store->entries().size();
    const bool on = m_store->isListening();
    m_countLabel->setText(count == 0
        ? QStringLiteral("还没有表情 —— 复制一张，或把图片拖进来")
        : QStringLiteral("共 %1 张%2").arg(count).arg(on ? QStringLiteral(" · 监听中") : QString()));
}

void StickersPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void StickersPage::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage())
        event->acceptProposedAction();
}

void StickersPage::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasUrls() && !mime->hasImage())
        return;

    int added = 0;
    int skipped = 0;
    QString lastMessage;
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const StickerStore::AddOutcome outcome = m_store->addFromFile(url.toLocalFile());
            lastMessage = outcome.message;
            if (outcome.status == StickerStore::AddStatus::Added)
                ++added;
            else if (outcome.status == StickerStore::AddStatus::Duplicate)
                ++skipped;
        }
    } else {
        const StickerStore::AddOutcome outcome =
            m_store->addFromImage(qvariant_cast<QImage>(mime->imageData()), QStringLiteral("拖入"));
        lastMessage = outcome.message;
        if (outcome.status == StickerStore::AddStatus::Added)
            ++added;
    }

    if (added + skipped > 1)
        setStatus(QStringLiteral("拖入完成：%1 张新入库，%2 张重复").arg(added).arg(skipped));
    else
        setStatus(lastMessage);

    event->acceptProposedAction();
}
