#include "pages/AiPage.h"

#include "app/ThemeManager.h"
#include "core/AiClient.h"
#include "core/TintedIcon.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kAiBubbleMaxWidth = 640;

// Markdown -> HTML 直接交给 Qt 内置实现，自研解析器没必要且容易漏边界情况
QString markdownToHtml(const QString &text)
{
    QTextDocument doc;
    doc.setMarkdown(text);
    return doc.toHtml();
}

// 按最近活动时间倒序，列表顺序与「切到哪个会话」的判断都依赖它
void sortByUpdatedAt(QVector<ChatSession> &sessions)
{
    std::sort(sessions.begin(), sessions.end(),
              [](const ChatSession &a, const ChatSession &b) {
                  return a.updatedAt > b.updatedAt;
              });
}

QString makeSessionId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

AiPage::AiPage(AiClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    buildUi();

    loadSessions();
    refreshSessionList();
    renderSession();

    connect(m_input, &QLineEdit::returnPressed, this, &AiPage::submit);
    connect(m_send, &QToolButton::clicked, this, &AiPage::submit);
    connect(m_stop, &QToolButton::clicked, this, &AiPage::stopGeneration);

    connect(m_client, &AiClient::deltaReceived, this, [this](const QString &delta) {
        if (!m_pendingBubble) {
            m_pendingText.clear();
            m_pendingBubble = addBubble(QString(), false);
            // 流式阶段用纯文本快速追加，整段完成后才转富文本（见 finalizeAssistant）
            m_pendingBubble->setTextFormat(Qt::PlainText);
        }
        m_pendingText += delta;
        m_pendingBubble->setText(m_pendingText);
        scrollToBottom();
    });

    connect(m_client, &AiClient::replyFinished, this, [this](const QString &full) {
        finalizeAssistant(full);

        if (ChatSession *session = currentSession()) {
            // 客户端此时已把 user + assistant 写进 history，直接整段取回作为事实来源
            session->messages = m_client->history();
            session->updatedAt = QDateTime::currentDateTime();
            persistSessions();
            refreshSessionList();
        }
        m_pendingUserText.clear();
    });

    connect(m_client, &AiClient::failed, this, [this](const QString &message) {
        const QString text = QStringLiteral("出错了：%1").arg(message);
        if (m_pendingBubble) {
            m_pendingBubble->setTextFormat(Qt::RichText);
            m_pendingBubble->setText(markdownToHtml(text));
            m_pendingBubble = nullptr;
            m_pendingText.clear();
        } else {
            addBubble(text, false);
        }
        m_pendingUserText.clear();
        scrollToBottom();
    });

    connect(m_client, &AiClient::busyChanged, this, [this](bool busy) {
        m_send->setEnabled(!busy);
        m_stop->setVisible(busy);
    });

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &AiPage::retint);

    retint();
}

void AiPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 左侧会话栏和右侧聊天区交给 QSplitter，拖动中间的分隔条就能调会话栏宽度。
    // 宽度上下限写在左列的 min/max 上，分隔条本身在 QSS 里画成一条悬停才亮起的细槽。
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("LibrarySplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ---- 左侧会话侧栏 ----
    auto *column = new QFrame(this);
    column->setObjectName(QStringLiteral("ContextColumn"));
    column->setMinimumWidth(180);
    column->setMaximumWidth(440);

    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(12, 12, 12, 12);
    columnLayout->setSpacing(10);

    m_newChat = new QToolButton(column);
    m_newChat->setObjectName(QStringLiteral("Primary"));
    m_newChat->setText(QStringLiteral("新建会话"));
    m_newChat->setCursor(Qt::PointingHandCursor);
    m_newChat->setMinimumHeight(34);
    columnLayout->addWidget(m_newChat);

    m_sessionsList = new QListWidget(column);
    m_sessionsList->setObjectName(QStringLiteral("TrackList"));
    m_sessionsList->setFrameShape(QFrame::NoFrame);
    m_sessionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    columnLayout->addWidget(m_sessionsList, 1);

    m_deleteChat = new QToolButton(column);
    m_deleteChat->setObjectName(QStringLiteral("Ghost"));
    m_deleteChat->setText(QStringLiteral("删除当前会话"));
    m_deleteChat->setCursor(Qt::PointingHandCursor);
    m_deleteChat->setMinimumHeight(30);
    columnLayout->addWidget(m_deleteChat);

    m_splitter->addWidget(column);

    // ---- 右侧主区 ----
    auto *main = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *header = new QFrame(main);
    header->setObjectName(QStringLiteral("PageHeader"));
    header->setFixedHeight(52);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *title = new QLabel(QStringLiteral("AI 助手"), header);
    title->setObjectName(QStringLiteral("SectionTitle"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);

    auto *clear = new QToolButton(header);
    clear->setObjectName(QStringLiteral("Ghost"));
    clear->setText(QStringLiteral("清空对话"));
    clear->setCursor(Qt::PointingHandCursor);
    clear->setMinimumHeight(28);
    headerLayout->addWidget(clear);

    mainLayout->addWidget(header);

    m_scroll = new QScrollArea(main);
    m_scroll->setObjectName(QStringLiteral("ChatScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget;
    container->setObjectName(QStringLiteral("ChatContainer"));

    m_chat = new QVBoxLayout(container);
    m_chat->setContentsMargins(20, 16, 20, 16);
    m_chat->setSpacing(12);
    m_chat->addStretch(1);

    m_scroll->setWidget(container);
    mainLayout->addWidget(m_scroll, 1);

    auto *composer = new QFrame(main);
    composer->setObjectName(QStringLiteral("Composer"));
    composer->setFixedHeight(64);

    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(20, 12, 20, 12);
    composerLayout->setSpacing(10);

    m_input = new QLineEdit(composer);
    m_input->setPlaceholderText(QStringLiteral("说点什么…（Enter 发送）"));
    m_input->setMinimumHeight(36);
    composerLayout->addWidget(m_input, 1);

    // 停止按钮只在生成中出现，平时隐藏，避免挤占输入区
    m_stop = new QToolButton(composer);
    m_stop->setObjectName(QStringLiteral("Ghost"));
    m_stop->setText(QStringLiteral("停止"));
    m_stop->setCursor(Qt::PointingHandCursor);
    m_stop->setMinimumHeight(36);
    m_stop->setVisible(false);
    composerLayout->addWidget(m_stop);

    m_send = new QToolButton(composer);
    m_send->setObjectName(QStringLiteral("Primary"));
    m_send->setText(QStringLiteral("  发送"));
    m_send->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setMinimumHeight(36);
    composerLayout->addWidget(m_send);

    mainLayout->addWidget(composer);
    m_splitter->addWidget(main);

    // 权重全给聊天区：窗口变宽时多出来的空间归右侧，左列保持用户拖出来的宽度
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    root->addWidget(m_splitter);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings().setValue(QStringLiteral("ai/libraryWidth"), m_splitter->sizes().value(0));
    });

    // ---- 交互 ----
    connect(clear, &QToolButton::clicked, this, [this] {
        // 原「清空对话」语义太重，这里改成只清当前会话的消息
        ChatSession *session = currentSession();
        if (!session)
            return;
        session->messages = QJsonArray();
        session->title = QStringLiteral("新对话");
        session->updatedAt = QDateTime::currentDateTime();
        m_client->clearHistory();
        persistSessions();
        refreshSessionList();
        renderSession();
    });

    connect(m_newChat, &QToolButton::clicked, this, &AiPage::newSession);
    connect(m_deleteChat, &QToolButton::clicked, this, &AiPage::deleteCurrentSession);

    connect(m_sessionsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            switchToSession(item->data(Qt::UserRole).toString());
    });
}

void AiPage::showEvent(QShowEvent *event)
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

        const int saved = QSettings().value(QStringLiteral("ai/libraryWidth"), 220).toInt();
        const int width = qBound(column->minimumWidth(), saved, column->maximumWidth());

        m_splitter->setSizes({ width, qMax(1, m_splitter->width() - width) });
    });
}

void AiPage::submit()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;

    if (m_client->isBusy()) {
        addBubble(QStringLiteral("上一条还在回复中，稍等一下。"), false);
        scrollToBottom();
        return;
    }

    m_input->clear();

    ChatSession *session = currentSession();
    // 首条用户消息决定会话标题；已有消息的会话保持原标题
    if (session && session->messages.isEmpty())
        session->title = text.left(20);

    addBubble(text, true);

    m_pendingBubble = nullptr;
    m_pendingText.clear();
    m_pendingUserText = text;

    if (session) {
        session->updatedAt = QDateTime::currentDateTime();
        persistSessions();
        refreshSessionList();
    }

    m_client->send(text);
}

void AiPage::stopGeneration()
{
    if (!m_client->isBusy())
        return;

    // AiClient::abort() 只发 busyChanged(false)：既不发 replyFinished 也不发 failed，
    // 所以半截的回复必须由这里主动收尾，否则气泡会永远卡在流式状态。
    m_client->abort();

    const QString partial = m_pendingText;
    finalizeAssistant(partial);

    if (ChatSession *session = currentSession()) {
        // 被中断的这一轮不会被 AiClient 记进 history，这里手动补上 user + 半截 assistant
        QJsonObject user;
        user.insert(QStringLiteral("role"), QStringLiteral("user"));
        user.insert(QStringLiteral("content"), m_pendingUserText);
        session->messages.append(user);

        if (!partial.isEmpty()) {
            QJsonObject assistant;
            assistant.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            assistant.insert(QStringLiteral("content"), partial);
            session->messages.append(assistant);
        }

        m_client->setHistory(session->messages);   // 让客户端侧与界面保持一致
        session->updatedAt = QDateTime::currentDateTime();
        persistSessions();
        refreshSessionList();
    }

    m_pendingUserText.clear();
}

void AiPage::finalizeAssistant(const QString &full)
{
    const QString content = full.isEmpty() ? m_pendingText : full;

    // 一个 delta 都没来就中止：不生成空气泡
    if (content.isEmpty() && !m_pendingBubble) {
        m_pendingText.clear();
        return;
    }

    QLabel *bubble = m_pendingBubble;
    if (!bubble)
        bubble = addBubble(QString(), false);

    // 整段文本到位，一次性转 Markdown 富文本
    bubble->setTextFormat(Qt::RichText);
    bubble->setText(markdownToHtml(content));

    m_pendingBubble = nullptr;
    m_pendingText.clear();
    scrollToBottom();
}

void AiPage::loadSessions()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai/sessions"));

    const QStringList ids = settings.childGroups();
    for (const QString &id : ids) {
        settings.beginGroup(id);

        ChatSession session;
        session.id = settings.value(QStringLiteral("id"), id).toString();
        session.title = settings.value(QStringLiteral("title")).toString();
        session.updatedAt = QDateTime::fromString(
            settings.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);

        const QByteArray raw = settings.value(QStringLiteral("messages")).toString().toUtf8();
        session.messages = QJsonDocument::fromJson(raw).array();

        settings.endGroup();

        if (session.updatedAt.isNull())
            session.updatedAt = QDateTime::currentDateTime();
        m_sessions.append(session);
    }

    settings.endGroup();

    sortByUpdatedAt(m_sessions);

    if (m_sessions.isEmpty()) {
        ChatSession session;
        session.id = makeSessionId();
        session.title = QStringLiteral("新对话");
        session.updatedAt = QDateTime::currentDateTime();
        m_sessions.append(session);
    }

    m_currentSessionId = m_sessions.first().id;
}

void AiPage::persistSessions()
{
    QSettings settings;
    // 整组重写：否则被删除的会话会在 ini / 注册表里留下残留
    settings.remove(QStringLiteral("ai/sessions"));

    settings.beginGroup(QStringLiteral("ai/sessions"));
    for (const ChatSession &session : std::as_const(m_sessions)) {
        settings.beginGroup(session.id);
        settings.setValue(QStringLiteral("id"), session.id);
        settings.setValue(QStringLiteral("title"), session.title);
        settings.setValue(QStringLiteral("messages"),
                          QString::fromUtf8(QJsonDocument(session.messages)
                                                .toJson(QJsonDocument::Compact)));
        settings.setValue(QStringLiteral("updatedAt"),
                          session.updatedAt.toString(Qt::ISODate));
        settings.endGroup();
    }
    settings.endGroup();
}

void AiPage::newSession()
{
    ChatSession session;
    session.id = makeSessionId();
    session.title = QStringLiteral("新对话");
    session.updatedAt = QDateTime::currentDateTime();
    m_sessions.append(session);
    m_currentSessionId = session.id;

    persistSessions();
    refreshSessionList();
    renderSession();
}

void AiPage::deleteCurrentSession()
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).id == m_currentSessionId) {
            m_sessions.remove(i);
            break;
        }
    }

    // 一个都不剩时补回一个空会话，保证界面始终有可用的当前会话
    if (m_sessions.isEmpty()) {
        ChatSession session;
        session.id = makeSessionId();
        session.title = QStringLiteral("新对话");
        session.updatedAt = QDateTime::currentDateTime();
        m_sessions.append(session);
    }

    sortByUpdatedAt(m_sessions);
    m_currentSessionId = m_sessions.first().id;

    persistSessions();
    refreshSessionList();
    renderSession();
}

void AiPage::switchToSession(const QString &id)
{
    if (id.isEmpty() || id == m_currentSessionId)
        return;
    m_currentSessionId = id;
    renderSession();
}

void AiPage::refreshSessionList()
{
    sortByUpdatedAt(m_sessions);

    // 程序化改选中项会触发 itemClicked 回环，刷新期间屏蔽列表信号
    const QSignalBlocker blocker(m_sessionsList);
    m_sessionsList->clear();
    for (const ChatSession &session : std::as_const(m_sessions)) {
        const QString title = session.title.isEmpty() ? QStringLiteral("新对话")
                                                       : session.title;
        auto *item = new QListWidgetItem(title, m_sessionsList);
        item->setData(Qt::UserRole, session.id);
        item->setToolTip(title);
        if (session.id == m_currentSessionId)
            m_sessionsList->setCurrentItem(item);
    }
}

void AiPage::renderSession()
{
    clearBubbles();

    ChatSession *session = currentSession();
    if (!session)
        return;

    m_client->setHistory(session->messages);

    if (session->messages.isEmpty()) {
        addBubble(QStringLiteral("你好，我是 Sora 内置的助手。\n先在「设置」里填好 API Key，就可以开始聊天了。"),
                  false);
        return;
    }

    for (const QJsonValue &value : std::as_const(session->messages)) {
        const QJsonObject message = value.toObject();
        renderMessage(message.value(QStringLiteral("role")).toString(),
                      message.value(QStringLiteral("content")).toString());
    }
    scrollToBottom();
}

void AiPage::renderMessage(const QString &role, const QString &content)
{
    if (role == QStringLiteral("assistant")) {
        QLabel *bubble = addBubble(QString(), false);
        bubble->setTextFormat(Qt::RichText);
        bubble->setText(markdownToHtml(content));
    } else if (role == QStringLiteral("user")) {
        addBubble(content, true);
    }
    // system 等其它 role 不参与展示
}

void AiPage::clearBubbles()
{
    // 保留末尾的 stretch，其余全是气泡行
    while (m_chat->count() > 1) {
        QLayoutItem *item = m_chat->takeAt(0);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_pendingBubble = nullptr;
    m_pendingText.clear();
}

ChatSession *AiPage::currentSession()
{
    for (ChatSession &session : m_sessions) {
        if (session.id == m_currentSessionId)
            return &session;
    }
    return nullptr;
}

QLabel *AiPage::addBubble(const QString &text, bool fromUser)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *bubble = new QLabel(text, row);
    bubble->setObjectName(fromUser ? QStringLiteral("BubbleUser")
                                   : QStringLiteral("BubbleAi"));
    bubble->setWordWrap(true);
    bubble->setTextFormat(Qt::PlainText);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    bubble->setOpenExternalLinks(true);
    bubble->setMaximumWidth(fromUser ? 560 : kAiBubbleMaxWidth);

    if (fromUser) {
        rowLayout->addStretch(1);
        rowLayout->addWidget(bubble);
    } else {
        rowLayout->addWidget(bubble);
        rowLayout->addStretch(1);
    }

    m_chat->insertWidget(m_chat->count() - 1, row);
    scrollToBottom();

    return bubble;
}

void AiPage::scrollToBottom()
{
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_scroll->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void AiPage::retint()
{
    m_send->setIcon(TintedIcon::create(QStringLiteral(":/icons/send.svg"),
                                       QColor(QStringLiteral("#052E18")), 16));
}
