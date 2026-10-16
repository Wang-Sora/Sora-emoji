#include "pages/PdfPage.h"

#include "app/ThemeManager.h"
#include "core/PdfController.h"
#include "core/TintedIcon.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPdfBookmarkModel>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// QSettings 的 key 会被按 '/' 拆成分组，Windows 路径里的 '\' 与 ':' 也不适合直接做 key；
// 这里对完整路径做 SHA1 摘要当 key，稳定且不与分组语义冲突。
QString positionKeyFor(const QString &filePath)
{
    return QString::fromLatin1(QCryptographicHash::hash(filePath.toUtf8(),
                                                        QCryptographicHash::Sha1).toHex());
}

} // namespace

PdfPage::PdfPage(QWidget *parent)
    : QWidget(parent)
    , m_ctrl(new PdfController(this))
{
    buildUi();
    installShortcuts();
    reloadLibrary();

    connect(m_ctrl, &PdfController::loaded, this, [this](const QString &, int pageCount) {
        // 换文档时，大纲模型、页码范围和阅读位置都要基于新文档重置
        m_bookmarks->setDocument(m_ctrl->document());
        if (m_pageValidator)
            m_pageValidator->setRange(1, qMax(1, pageCount));
        m_stack->setCurrentWidget(m_view);
        restorePosition();
        updatePageLabel();
    });

    connect(m_ctrl, &PdfController::failed, this, [this](const QString &message) {
        m_bookmarks->setDocument(nullptr);
        if (m_pageValidator)
            m_pageValidator->setRange(1, 1);
        m_hint->setText(message);
        m_stack->setCurrentWidget(m_hint);
    });

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &PdfPage::retint);

    retint();
}

void PdfPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 左列和右侧阅读区交给 QSplitter，拖动中间的分隔条就能调目录列宽度。
    // 宽度上下限写在左列的 min/max 上，分隔条本身在 QSS 里画成一条悬停才亮起的细槽。
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("LibrarySplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ---- 左：文档列表 + 目录树 ----
    auto *column = new QFrame(this);
    column->setObjectName(QStringLiteral("ContextColumn"));
    column->setMinimumWidth(180);
    column->setMaximumWidth(440);

    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(14, 16, 14, 16);
    columnLayout->setSpacing(12);

    auto *heading = new QLabel(QStringLiteral("文档"), column);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    columnLayout->addWidget(heading);

    m_docs = new QListWidget(column);
    m_docs->setObjectName(QStringLiteral("DocList"));
    m_docs->setFrameShape(QFrame::NoFrame);
    m_docs->setUniformItemSizes(true);
    // 文档列表与目录树各占一份拉伸，任何一段都不会被挤没
    columnLayout->addWidget(m_docs, 1);

    auto *openButton = new QToolButton(column);
    openButton->setObjectName(QStringLiteral("Primary"));
    openButton->setText(QStringLiteral("  打开 PDF…"));
    openButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    openButton->setMinimumHeight(34);
    columnLayout->addWidget(openButton);
    m_openButton = openButton;

    auto *outlineHeading = new QLabel(QStringLiteral("目录"), column);
    outlineHeading->setObjectName(QStringLiteral("SectionTitle"));
    columnLayout->addWidget(outlineHeading);

    m_outline = new QTreeView(column);
    m_outline->setObjectName(QStringLiteral("OutlineTree"));
    m_outline->setFrameShape(QFrame::NoFrame);
    m_outline->setHeaderHidden(true);
    m_outline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outline->setUniformRowHeights(true);
    m_outline->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bookmarks = new QPdfBookmarkModel(this);
    m_outline->setModel(m_bookmarks);
    columnLayout->addWidget(m_outline, 1);

    m_splitter->addWidget(column);

    // ---- 右：工具栏 + 渲染区 ----
    auto *stage = new QWidget(this);
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);

    auto *toolbar = new QFrame(stage);
    toolbar->setObjectName(QStringLiteral("PdfToolbar"));
    toolbar->setFixedHeight(48);

    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(14, 8, 14, 8);
    toolbarLayout->setSpacing(8);

    auto *zoomOut = new QToolButton(toolbar);
    zoomOut->setObjectName(QStringLiteral("Ghost"));
    zoomOut->setCursor(Qt::PointingHandCursor);
    zoomOut->setFixedSize(30, 30);
    zoomOut->setIconSize(QSize(16, 16));
    zoomOut->setToolTip(QStringLiteral("缩小"));
    m_zoomOut = zoomOut;

    auto *zoomIn = new QToolButton(toolbar);
    zoomIn->setObjectName(QStringLiteral("Ghost"));
    zoomIn->setCursor(Qt::PointingHandCursor);
    zoomIn->setFixedSize(30, 30);
    zoomIn->setIconSize(QSize(16, 16));
    zoomIn->setToolTip(QStringLiteral("放大"));
    m_zoomIn = zoomIn;

    auto *fitWidth = new QToolButton(toolbar);
    fitWidth->setObjectName(QStringLiteral("Ghost"));
    fitWidth->setCursor(Qt::PointingHandCursor);
    fitWidth->setFixedSize(30, 30);
    fitWidth->setIconSize(QSize(16, 16));
    fitWidth->setToolTip(QStringLiteral("适应宽度"));
    m_fitWidth = fitWidth;

    m_pageEdit = new QLineEdit(toolbar);
    m_pageEdit->setFixedWidth(52);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setToolTip(QStringLiteral("输入页码后回车跳转"));
    m_pageValidator = new QIntValidator(1, 1, m_pageEdit);
    m_pageEdit->setValidator(m_pageValidator);

    m_pageModeButton = new QToolButton(toolbar);
    m_pageModeButton->setObjectName(QStringLiteral("Ghost"));
    m_pageModeButton->setCursor(Qt::PointingHandCursor);
    m_pageModeButton->setMinimumHeight(30);

    m_pageLabel = new QLabel(QStringLiteral("未打开文档"), toolbar);
    m_pageLabel->setObjectName(QStringLiteral("Hint"));

    toolbarLayout->addWidget(m_pageEdit);
    toolbarLayout->addSpacing(6);
    toolbarLayout->addWidget(zoomOut);
    toolbarLayout->addWidget(zoomIn);
    toolbarLayout->addWidget(fitWidth);
    toolbarLayout->addWidget(m_pageModeButton);
    toolbarLayout->addSpacing(6);
    toolbarLayout->addWidget(m_pageLabel);
    toolbarLayout->addStretch(1);

    stageLayout->addWidget(toolbar);

    m_stack = new QStackedWidget(stage);

    m_hint = new QLabel(QStringLiteral("从左侧选择一份 PDF，或点击「打开 PDF…」"), m_stack);
    m_hint->setObjectName(QStringLiteral("Hint"));
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);

    m_view = new QPdfView(m_stack);
    m_view->setObjectName(QStringLiteral("PdfView"));
    m_view->setDocument(m_ctrl->document());
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_view->setPageSpacing(12);
    m_view->setDocumentMargins(QMargins(16, 16, 16, 16));

    m_stack->addWidget(m_hint);
    m_stack->addWidget(m_view);
    m_stack->setCurrentWidget(m_hint);

    stageLayout->addWidget(m_stack, 1);

    m_splitter->addWidget(stage);

    // 权重全给阅读区：窗口变宽时多出来的空间归右侧，左列保持用户拖出来的宽度
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    root->addWidget(m_splitter);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings().setValue(QStringLiteral("pdf/libraryWidth"), m_splitter->sizes().value(0));
    });

    connect(m_docs, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            openPath(item->data(Qt::UserRole).toString());
    });

    connect(openButton, &QToolButton::clicked, this, &PdfPage::chooseFile);

    connect(m_outline, &QTreeView::clicked, this, &PdfPage::jumpToBookmark);

    connect(zoomIn, &QToolButton::clicked, this, &PdfPage::zoomIn);
    connect(zoomOut, &QToolButton::clicked, this, &PdfPage::zoomOut);
    connect(fitWidth, &QToolButton::clicked, this, &PdfPage::fitToWidth);

    // 回车或失焦提交跳页；越界值由 gotoPage 内部夹取
    connect(m_pageEdit, &QLineEdit::editingFinished, this, [this] {
        gotoPage(m_pageEdit->text().toInt());
    });

    connect(m_pageModeButton, &QToolButton::clicked, this, [this] {
        const bool multi = m_view->pageMode() == QPdfView::PageMode::MultiPage;
        m_view->setPageMode(multi ? QPdfView::PageMode::SinglePage
                                  : QPdfView::PageMode::MultiPage);
    });

    connect(m_view, &QPdfView::zoomFactorChanged, this, [this](qreal) {
        updatePageLabel();
    });

    connect(m_view, &QPdfView::pageModeChanged, this, [this](QPdfView::PageMode) {
        updatePageModeButton();
    });

    // 滚动或跳页都会触发：把页码写回输入框，并持久化阅读位置
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this](int page) {
        if (m_pageEdit) {
            QSignalBlocker blocker(m_pageEdit);
            m_pageEdit->setText(QString::number(page + 1));
        }
        savePosition();
    });

    updatePageModeButton();
}

void PdfPage::showEvent(QShowEvent *event)
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

        const int saved = QSettings().value(QStringLiteral("pdf/libraryWidth"), 248).toInt();
        const int width = qBound(column->minimumWidth(), saved, column->maximumWidth());

        m_splitter->setSizes({ width, qMax(1, m_splitter->width() - width) });
    });
}

void PdfPage::installShortcuts()
{
    // 统一限制为“本页及其子控件”生效，避免抢走其他页面的同名快捷键
    const Qt::ShortcutContext ctx = Qt::WidgetWithChildrenShortcut;

    auto *open = new QShortcut(QKeySequence::Open, this);
    open->setContext(ctx);
    connect(open, &QShortcut::activated, this, &PdfPage::chooseFile);

    auto *zoomInShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this);
    zoomInShortcut->setContext(ctx);
    connect(zoomInShortcut, &QShortcut::activated, this, &PdfPage::zoomIn);

    auto *zoomOutShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    zoomOutShortcut->setContext(ctx);
    connect(zoomOutShortcut, &QShortcut::activated, this, &PdfPage::zoomOut);

    auto *prevPage = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    prevPage->setContext(ctx);
    connect(prevPage, &QShortcut::activated, this, [this] { stepPage(-1); });

    auto *nextPage = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    nextPage->setContext(ctx);
    connect(nextPage, &QShortcut::activated, this, [this] { stepPage(1); });
}

void PdfPage::reloadLibrary()
{
    m_docs->clear();

    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    for (const QString &location : locations) {
        QDir dir(location);
        if (!dir.exists())
            continue;

        const QFileInfoList entries = dir.entryInfoList({ QStringLiteral("*.pdf") },
                                                        QDir::Files | QDir::NoDotAndDotDot,
                                                        QDir::Name);
        for (const QFileInfo &info : entries) {
            auto *item = new QListWidgetItem(info.fileName(), m_docs);
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setToolTip(info.absoluteFilePath());
        }
    }

    if (m_docs->count() == 0) {
        auto *item = new QListWidgetItem(QStringLiteral("（示例）文档目录里还没有 PDF"), m_docs);
        item->setFlags(Qt::NoItemFlags);
    }
}

void PdfPage::chooseFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 PDF"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("PDF 文件 (*.pdf)"));

    if (!file.isEmpty())
        openPath(file);
}

void PdfPage::openPath(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    m_hint->setText(QStringLiteral("正在打开…"));
    m_stack->setCurrentWidget(m_hint);

    if (!m_ctrl->load(filePath))
        updatePageLabel();
}

void PdfPage::updatePageLabel()
{
    if (m_ctrl->pageCount() <= 0) {
        m_pageLabel->setText(QStringLiteral("未打开文档"));
        return;
    }

    const int percent = qRound(m_view->zoomFactor() * 100);
    m_pageLabel->setText(QStringLiteral("%1 · 共 %2 页 · %3%")
                             .arg(m_ctrl->fileName())
                             .arg(m_ctrl->pageCount())
                             .arg(percent));
}

void PdfPage::updatePageModeButton()
{
    const bool multi = m_view->pageMode() == QPdfView::PageMode::MultiPage;
    m_pageModeButton->setText(multi ? QStringLiteral("连续") : QStringLiteral("单页"));
    m_pageModeButton->setToolTip(multi ? QStringLiteral("当前为连续滚动，点击切换为单页")
                                       : QStringLiteral("当前为单页，点击切换为连续滚动"));
}

void PdfPage::gotoPage(int oneBased)
{
    const int pageCount = m_ctrl->pageCount();
    if (pageCount <= 0)
        return;

    // jump() 的页码是 0 基；位置给左上角、缩放传 0 表示沿用当前缩放
    const int index = qBound(0, oneBased - 1, pageCount - 1);
    m_view->pageNavigator()->jump(index, QPointF(), 0);
}

void PdfPage::stepPage(int delta)
{
    if (m_ctrl->pageCount() <= 0)
        return;

    gotoPage(m_view->pageNavigator()->currentPage() + 1 + delta);
}

void PdfPage::savePosition()
{
    const QString path = m_ctrl->filePath();
    if (path.isEmpty() || m_ctrl->pageCount() <= 0)
        return;

    // 页码一变就落盘，不依赖 closeEvent，避免窗口关闭链路收不到子控件事件
    QSettings settings;
    settings.beginGroup(QStringLiteral("pdf/positions"));
    settings.setValue(positionKeyFor(path), m_view->pageNavigator()->currentPage() + 1);
    settings.endGroup();
}

void PdfPage::restorePosition()
{
    const QString path = m_ctrl->filePath();
    const int pageCount = m_ctrl->pageCount();
    if (path.isEmpty() || pageCount <= 0)
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("pdf/positions"));
    const int saved = settings.value(positionKeyFor(path), 1).toInt();
    settings.endGroup();

    if (saved >= 1 && saved <= pageCount)
        gotoPage(saved);
}

void PdfPage::zoomIn()
{
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(qMin(m_view->zoomFactor() * 1.15, 6.0));
    updatePageLabel();
}

void PdfPage::zoomOut()
{
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(qMax(m_view->zoomFactor() / 1.15, 0.2));
    updatePageLabel();
}

void PdfPage::fitToWidth()
{
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    updatePageLabel();
}

void PdfPage::jumpToBookmark(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // 大纲里的 Page 与 jump() 同为 0 基页码，直接透传可避免额外 +1/-1 出错
    const int page = index.data(int(QPdfBookmarkModel::Role::Page)).toInt();
    m_view->pageNavigator()->jump(page, QPointF(), 0);
}

void PdfPage::retint()
{
    const QColor idle = ThemeManager::instance().iconIdle();

    m_zoomOut->setIcon(TintedIcon::create(QStringLiteral(":/icons/zoom-out.svg"), idle, 16));
    m_zoomIn->setIcon(TintedIcon::create(QStringLiteral(":/icons/zoom-in.svg"), idle, 16));
    m_fitWidth->setIcon(TintedIcon::create(QStringLiteral(":/icons/fit-width.svg"), idle, 16));

    m_openButton->setIcon(TintedIcon::create(QStringLiteral(":/icons/open.svg"),
                                             QColor(QStringLiteral("#052E18")), 16));
}
