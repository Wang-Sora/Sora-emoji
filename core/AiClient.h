#ifndef SORA_AICLIENT_H
#define SORA_AICLIENT_H

#include <QJsonArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * AI 对话客户端（OpenAI 兼容协议）。
 *
 * 之所以选 OpenAI 兼容的 /chat/completions 协议，是因为绝大多数厂商
 * （OpenAI、DeepSeek、通义、Kimi、智谱、Ollama、vLLM…）都提供同构接口，
 * 只需换 baseUrl + model + key 即可接入，无需为每家写一套代码。
 *
 * 只依赖 QtNetwork，负责流式解析 SSE，把增量文本通过 deltaReceived 抛给界面。
 */
class AiClient : public QObject
{
    Q_OBJECT

public:
    struct Config {
        // 记录用户在设置页选的预置服务商，仅用于回填下拉框；请求本身只认 baseUrl/model/key。
        // 默认值与下面的 baseUrl/model 保持一致，这样没保存过设置时下拉框也不会落到「自定义」
        QString providerId = QStringLiteral("openai");
        QString baseUrl = QStringLiteral("https://api.openai.com/v1");
        QString apiKey;
        QString model = QStringLiteral("gpt-4o-mini");
        QString systemPrompt = QStringLiteral("你是 Sora 客户端内置的助手，回答简洁、准确。");
    };

    explicit AiClient(QObject *parent = nullptr);
    ~AiClient() override;

    void setConfig(const Config &config);
    Config config() const { return m_config; }

    bool isBusy() const { return m_reply != nullptr; }
    bool hasApiKey() const { return !m_config.apiKey.isEmpty(); }
    QJsonArray history() const { return m_messages; }

    void send(const QString &text);
    void abort();
    void clearHistory();

    // 整体替换对话上下文：界面切换会话时用它把选中会话的历史灌回来
    void setHistory(const QJsonArray &history);

Q_SIGNALS:
    void deltaReceived(const QString &delta);
    void replyFinished(const QString &fullText);
    void failed(const QString &message);
    void busyChanged(bool busy);

private:
    void releaseReply();
    void onReadyRead();
    void onFinished();
    void consumeEvent(const QByteArray &event);
    QUrl endpoint() const;

    Config m_config;
    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_accum;
    QString m_pendingUser;
    QJsonArray m_messages;
};

#endif // SORA_AICLIENT_H
