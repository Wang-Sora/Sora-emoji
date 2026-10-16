#ifndef SORA_AIPAGE_H
#define SORA_AIPAGE_H

#include <QDateTime>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QWidget>

class AiClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QShowEvent;
class QSplitter;
class QToolButton;
class QVBoxLayout;

/// 单个会话的完整状态：纯数据聚合，不含 Qt 元对象，便于整体落盘。
struct ChatSession {
    QString id;
    QString title;
    QJsonArray messages;
    QDateTime updatedAt;
};

/**
 * AI 对话页：左侧会话列表 + 气泡式聊天区 + 底部输入框。
 *
 * 流式回复时先把 delta 以纯文本快速追加进气泡（逐字效果），等整段返回后再一次性
 * 转成 Markdown 富文本——每个 delta 都跑一次 Markdown 解析会明显卡顿，没必要。
 */
class AiPage : public QWidget
{
    Q_OBJECT

public:
    explicit AiPage(AiClient *client, QWidget *parent = nullptr);

    void retint();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void submit();
    void stopGeneration();

    void loadSessions();
    void persistSessions();
    void newSession();
    void deleteCurrentSession();
    void switchToSession(const QString &id);
    void refreshSessionList();
    void renderSession();
    void renderMessage(const QString &role, const QString &content);
    void clearBubbles();
    // 把流式气泡定稿成 Markdown 富文本；full 为空时用已累积的纯文本兜底
    void finalizeAssistant(const QString &full);

    ChatSession *currentSession();

    QLabel *addBubble(const QString &text, bool fromUser);
    void scrollToBottom();

    AiClient *m_client = nullptr;

    QSplitter *m_splitter = nullptr;
    bool m_splitterRestored = false;

    // 左侧会话侧栏
    QListWidget *m_sessionsList = nullptr;
    QToolButton *m_newChat = nullptr;
    QToolButton *m_deleteChat = nullptr;

    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_chat = nullptr;
    QLineEdit *m_input = nullptr;
    QToolButton *m_send = nullptr;
    QToolButton *m_stop = nullptr;

    QLabel *m_pendingBubble = nullptr;
    QString m_pendingText;
    QString m_pendingUserText;

    QVector<ChatSession> m_sessions;
    QString m_currentSessionId;
};

#endif // SORA_AIPAGE_H
