#include "core/AiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

AiClient::AiClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

AiClient::~AiClient()
{
    // 只拆连接、不发信号：析构期间派生的接收方可能也正在析构，
    // 此时再回调进本对象会踩到已经释放的成员。
    releaseReply();
}

void AiClient::setConfig(const Config &config)
{
    m_config = config;
}

QUrl AiClient::endpoint() const
{
    QString base = m_config.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    return QUrl(base + QStringLiteral("/chat/completions"));
}

void AiClient::send(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return;

    if (isBusy()) {
        emit failed(QStringLiteral("上一条消息还在回复中，请稍候"));
        return;
    }

    if (m_config.apiKey.isEmpty()) {
        emit failed(QStringLiteral("还没有配置 API Key，请到「设置」里填写"));
        return;
    }

    QJsonArray messages;

    if (!m_config.systemPrompt.trimmed().isEmpty()) {
        QJsonObject system;
        system.insert(QStringLiteral("role"), QStringLiteral("system"));
        system.insert(QStringLiteral("content"), m_config.systemPrompt);
        messages.append(system);
    }

    for (const QJsonValue &value : std::as_const(m_messages))
        messages.append(value);

    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), prompt);
    messages.append(user);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_config.model);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), true);

    QNetworkRequest request(endpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setTransferTimeout(120000);

    m_buffer.clear();
    m_accum.clear();
    m_pendingUser = prompt;

    m_reply = m_net->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AiClient::onFinished);

    emit busyChanged(true);
}

void AiClient::abort()
{
    if (!m_reply)
        return;

    releaseReply();
    emit busyChanged(false);
}

void AiClient::releaseReply()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();

    m_buffer.clear();
}

void AiClient::clearHistory()
{
    m_messages = QJsonArray();
}

void AiClient::setHistory(const QJsonArray &history)
{
    m_messages = history;
}

void AiClient::onReadyRead()
{
    if (!m_reply)
        return;

    m_buffer.append(m_reply->readAll());

    // SSE 以空行分隔事件；\n\n 或 \r\n\r\n
    int index = 0;
    while (true) {
        int sep = m_buffer.indexOf("\n\n", index);
        int sepLen = 2;
        if (sep < 0) {
            sep = m_buffer.indexOf("\r\n\r\n", index);
            sepLen = 4;
        }
        if (sep < 0)
            break;

        const QByteArray event = m_buffer.mid(index, sep - index);
        index = sep + sepLen;
        consumeEvent(event);
    }

    if (index > 0)
        m_buffer.remove(0, index);
}

void AiClient::consumeEvent(const QByteArray &event)
{
    const QList<QByteArray> lines = event.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (!line.startsWith("data:"))
            continue;

        const QByteArray data = line.mid(5).trimmed();
        if (data.isEmpty())
            continue;

        if (data == "[DONE]")
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();

        if (obj.contains(QStringLiteral("error"))) {
            const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
            const QString message = err.value(QStringLiteral("message")).toString();
            emit failed(message.isEmpty() ? QStringLiteral("服务端返回了错误") : message);
            continue;
        }

        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;

        const QJsonObject delta = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject();
        const QString piece = delta.value(QStringLiteral("content")).toString();
        if (piece.isEmpty())
            continue;

        m_accum += piece;
        emit deltaReceived(piece);
    }
}

void AiClient::onFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    // 收尾：可能还有未以空行结束的残包
    m_buffer.append(reply->readAll());
    if (!m_buffer.isEmpty())
        consumeEvent(m_buffer);
    m_buffer.clear();

    const QNetworkReply::NetworkError error = reply->error();

    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        QString message = reply->errorString();
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const QJsonObject err = doc.object().value(QStringLiteral("error")).toObject();
            const QString detail = err.value(QStringLiteral("message")).toString();
            if (!detail.isEmpty())
                message = detail;
        }
        emit failed(message);
    } else if (error == QNetworkReply::NoError) {
        if (!m_accum.isEmpty()) {
            QJsonObject user;
            user.insert(QStringLiteral("role"), QStringLiteral("user"));
            user.insert(QStringLiteral("content"), m_pendingUser);
            m_messages.append(user);

            QJsonObject assistant;
            assistant.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            assistant.insert(QStringLiteral("content"), m_accum);
            m_messages.append(assistant);
        }
        emit replyFinished(m_accum);
    }

    m_accum.clear();
    m_pendingUser.clear();
    reply->deleteLater();
    emit busyChanged(false);
}
