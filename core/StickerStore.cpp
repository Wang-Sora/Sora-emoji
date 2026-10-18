#include "core/StickerStore.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

// 表情包合理的体积上限，防呆：拖进来一个 4K 视频也能被魔数误判成 jpg 之类
constexpr qint64 kMaxBytes = 50 * 1024 * 1024;

// 缩略图边长。展示用 icon 是 96px，存 256 留出高 DPI 和放大预览的余量
constexpr int kThumbSize = 256;

// 剪贴板 dataChanged 的防抖窗口：QQ/浏览器一次复制常连发多轮信号
constexpr int kCollectDelayMs = 350;

// 只信魔数不信扩展名：QQ 表情缓存存在「.jpg 后缀的真 GIF」
QString sniffImageFormat(const QByteArray &data)
{
    if (data.size() >= 8 && data.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral("png");
    if (data.size() >= 3 && data.startsWith("\xFF\xD8\xFF"))
        return QStringLiteral("jpg");
    if (data.size() >= 6 && (data.startsWith("GIF87a") || data.startsWith("GIF89a")))
        return QStringLiteral("gif");
    if (data.size() >= 2 && data.startsWith("BM"))
        return QStringLiteral("bmp");
    if (data.size() >= 12 && data.startsWith("RIFF") && data.mid(8, 4) == QByteArray("WEBP"))
        return QStringLiteral("webp");
    return {};
}

QString hashToName(const QString &hash, const QString &ext)
{
    return hash + QLatin1Char('.') + ext;
}

} // namespace

StickerStore::StickerStore(QObject *parent)
    : QObject(parent)
{
    // 存储位置：用户配置过就用配置的，否则落在 %APPDATA%/Sora/stickers
    m_root = QSettings().value(QStringLiteral("stickers/storageDir")).toString();
    if (m_root.isEmpty()) {
        m_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/stickers");
    }
    m_originDir = m_root + QStringLiteral("/origin");
    m_thumbDir = m_root + QStringLiteral("/thumbs");
    QDir().mkpath(m_originDir);
    QDir().mkpath(m_thumbDir);

    // 防抖窗口到了才真正去读剪贴板：读 mime 本身可能涉及跨进程取数，能少读一次是一次
    m_collectTimer = new QTimer(this);
    m_collectTimer->setSingleShot(true);
    m_collectTimer->setInterval(kCollectDelayMs);
    connect(m_collectTimer, &QTimer::timeout, this, [this] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        if (!mime)
            return;
        const AddOutcome outcome = addFromMime(mime);
        // 监听模式下 Empty 是常态（复制文字/网址），不打扰用户
        if (outcome.status != AddStatus::Empty)
            emit imported(outcome);
    });

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &StickerStore::onClipboardChanged);

    loadIndex();
}

StickerStore::AddOutcome StickerStore::addFromMime(const QMimeData *mime)
{
    if (!mime)
        return { AddStatus::Empty, {}, QStringLiteral("剪贴板是空的") };

    // 文件引用优先：字节级复制原文件，零重编码，GIF 动画 / PNG 透明全保真
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const AddOutcome outcome = addFromFile(url.toLocalFile());
            if (outcome.status != AddStatus::Empty)
                return outcome;
        }
    }

    // 回落位图：只能拿到静态首帧（GIF 会丢动画），但保底能收
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull())
            return addFromImage(image, QStringLiteral("剪贴板"));
    }

    return { AddStatus::Empty, {}, QStringLiteral("剪贴板里没有图片") };
}

StickerStore::AddOutcome StickerStore::addFromFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return { AddStatus::Empty, {}, QStringLiteral("不是文件：%1").arg(path) };
    if (info.size() > kMaxBytes)
        return { AddStatus::Failed, {}, QStringLiteral("文件超过 50MB，不收") };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { AddStatus::Failed, {}, QStringLiteral("读不了 %1").arg(info.fileName()) };

    const QString source = info.fileName();
    AddOutcome outcome = addBytes(file.readAll(), source);
    // 失败信息里带上文件名，多文件导入时才知道是哪个出的问题
    if (outcome.status == AddStatus::Failed)
        outcome.message = QStringLiteral("%1：%2").arg(source, outcome.message);
    return outcome;
}

StickerStore::AddOutcome StickerStore::addFromImage(const QImage &image, const QString &source)
{
    // 位图统一编码成 PNG 入库，alpha 通道不会丢
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return { AddStatus::Failed, {}, QStringLiteral("图片编码失败") };
    return addBytes(png, source);
}

StickerStore::AddOutcome StickerStore::addBytes(const QByteArray &data, const QString &source)
{
    if (data.isEmpty())
        return { AddStatus::Empty, {}, QStringLiteral("内容为空") };

    const QString ext = sniffImageFormat(data);
    if (ext.isEmpty())
        return { AddStatus::Failed, {}, QStringLiteral("不是可识别的图片（支持 JPG/PNG/GIF/WebP/BMP）") };

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

    if (find(hash))
        return { AddStatus::Duplicate, hash, QStringLiteral("已存在，跳过") };

    // 原子落盘，避免半截文件污染库
    const QString originFile = m_originDir + QLatin1Char('/') + hashToName(hash, ext);
    QSaveFile file(originFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return { AddStatus::Failed, {}, QStringLiteral("写盘失败：%1").arg(file.errorString()) };
    }

    // 首帧解码 + 动画探测 + 缩略图
    QByteArray bytes = data; // QBuffer 要非 const 指针，隐式共享，无实际拷贝
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    const QImage firstFrame = reader.read();
    if (firstFrame.isNull()) {
        QFile::remove(originFile);
        return { AddStatus::Failed, {}, QStringLiteral("图片解码失败：%1").arg(reader.errorString()) };
    }

    const bool animated = reader.supportsAnimation() && reader.imageCount() > 1;

    QImage thumb = firstFrame.scaled(kThumbSize, kThumbSize,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!thumb.save(m_thumbDir + QLatin1Char('/') + hash + QStringLiteral(".png"), "PNG"))
        thumb = QImage(); // 缩略图失败不阻断入库，网格里显示占位

    StickerEntry entry;
    entry.hash = hash;
    entry.ext = ext;
    entry.source = source;
    entry.addedMs = QDateTime::currentMSecsSinceEpoch();
    entry.bytes = data.size();
    entry.width = firstFrame.width();
    entry.height = firstFrame.height();
    entry.animated = animated;
    m_entries.prepend(entry); // 新的排前面

    saveIndex();
    emit changed();

    return { AddStatus::Added, hash,
             QStringLiteral("已入库 %1 · %2×%3").arg(ext.toUpper()).arg(entry.width).arg(entry.height) };
}

bool StickerStore::removeSticker(const QString &hash)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).hash != hash)
            continue;
        const StickerEntry entry = m_entries.at(i);
        m_entries.removeAt(i);
        QFile::remove(originPath(hash));
        QFile::remove(thumbPath(hash));
        saveIndex();
        emit changed();
        return true;
    }
    return false;
}

QString StickerStore::originPath(const QString &hash) const
{
    const StickerEntry *entry = find(hash);
    if (!entry)
        return {};
    return m_originDir + QLatin1Char('/') + hashToName(hash, entry->ext);
}

QString StickerStore::thumbPath(const QString &hash) const
{
    return m_thumbDir + QLatin1Char('/') + hash + QStringLiteral(".png");
}

QString StickerStore::storageRoot() const
{
    return m_root;
}

QString StickerStore::migrateStorage(const QString &newRoot)
{
    const QString target = QDir::fromNativeSeparators(QDir::cleanPath(newRoot));
    if (target.isEmpty() || target == m_root)
        return {};

    // 先把目标目录建出来，建不出来就不动老数据
    const QString newOrigin = target + QStringLiteral("/origin");
    const QString newThumb = target + QStringLiteral("/thumbs");
    if (!QDir().mkpath(newOrigin) || !QDir().mkpath(newThumb))
        return QStringLiteral("无法创建目标目录，老数据未动");

    // 第一遍：复制。哈希即内容，目标已有同名文件 = 同一张图，跳过即可。
    // 用复制而不是改名：中途失败时老库还完好，重试也无害。
    for (const StickerEntry &entry : std::as_const(m_entries)) {
        const QString originSrc = m_originDir + QLatin1Char('/') + hashToName(entry.hash, entry.ext);
        const QString originDst = newOrigin + QLatin1Char('/') + hashToName(entry.hash, entry.ext);
        if (QFileInfo::exists(originSrc) && !QFileInfo::exists(originDst)
            && !QFile::copy(originSrc, originDst)) {
            return QStringLiteral("复制失败（老数据未动）：%1").arg(originSrc);
        }

        const QString thumbSrc = m_thumbDir + QLatin1Char('/') + entry.hash + QStringLiteral(".png");
        const QString thumbDst = newThumb + QLatin1Char('/') + entry.hash + QStringLiteral(".png");
        if (QFileInfo::exists(thumbSrc) && !QFileInfo::exists(thumbDst)
            && !QFile::copy(thumbSrc, thumbDst)) {
            return QStringLiteral("复制缩略图失败（老数据未动）：%1").arg(thumbSrc);
        }
    }

    // 第二遍：全部复制成功，才清理源文件和旧索引
    for (const StickerEntry &entry : std::as_const(m_entries)) {
        QFile::remove(m_originDir + QLatin1Char('/') + hashToName(entry.hash, entry.ext));
        QFile::remove(m_thumbDir + QLatin1Char('/') + entry.hash + QStringLiteral(".png"));
    }
    QFile::remove(m_root + QStringLiteral("/index.json"));
    // 老根目录清得掉就清（非空说明还有用户自己的文件，留着别碰）
    QDir().rmdir(m_originDir);
    QDir().rmdir(m_thumbDir);
    QDir().rmdir(m_root);

    m_root = target;
    m_originDir = newOrigin;
    m_thumbDir = newThumb;
    saveIndex();
    QSettings().setValue(QStringLiteral("stickers/storageDir"), target);
    return {};
}

bool StickerStore::copyToClipboard(const QString &hash)
{
    const StickerEntry *entry = find(hash);
    if (!entry)
        return false;

    const QString path = originPath(hash);

    auto *mime = new QMimeData;
    if (entry->ext != QLatin1String("gif")) {
        QImage image(path);
        if (!image.isNull())
            mime->setImageData(image); // 聊天框直接粘成图
    }
    mime->setUrls({ QUrl::fromLocalFile(path) }); // 资源管理器粘成文件；GIF 全靠它保动画

    m_suppressNext = true;
    QGuiApplication::clipboard()->setMimeData(mime); // 所有权转移给剪贴板，不要再 delete

    // 兜底：万一 setMimeData 没触发 dataChanged（例如内容没变），别让抑制标志卡住下一次采集
    QTimer::singleShot(1000, this, [this] { m_suppressNext = false; });
    return true;
}

void StickerStore::setListening(bool on)
{
    if (m_listening == on)
        return;
    m_listening = on;
    if (!on)
        m_collectTimer->stop();
}

void StickerStore::onClipboardChanged()
{
    if (!m_listening)
        return;
    if (m_suppressNext) {
        m_suppressNext = false;
        return;
    }
    m_collectTimer->start();
}

const StickerStore::StickerEntry *StickerStore::find(const QString &hash) const
{
    for (const StickerEntry &entry : m_entries) {
        if (entry.hash == hash)
            return &entry;
    }
    return nullptr;
}

void StickerStore::loadIndex()
{
    QFile file(storageRoot() + QStringLiteral("/index.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    m_entries.clear();
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        StickerEntry entry;
        entry.hash = obj.value(QStringLiteral("hash")).toString();
        entry.ext = obj.value(QStringLiteral("ext")).toString();
        entry.source = obj.value(QStringLiteral("source")).toString();
        entry.addedMs = static_cast<qint64>(obj.value(QStringLiteral("addedMs")).toDouble());
        entry.bytes = static_cast<qint64>(obj.value(QStringLiteral("bytes")).toDouble());
        entry.width = obj.value(QStringLiteral("width")).toInt();
        entry.height = obj.value(QStringLiteral("height")).toInt();
        entry.animated = obj.value(QStringLiteral("animated")).toBool();
        if (entry.hash.isEmpty() || entry.ext.isEmpty())
            continue;
        // 索引里挂着但原文件没了（用户手动清了目录）：剔除并稍后重写索引
        if (!QFileInfo::exists(m_originDir + QLatin1Char('/') + hashToName(entry.hash, entry.ext)))
            continue;
        m_entries.append(entry);
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const StickerEntry &a, const StickerEntry &b) { return a.addedMs > b.addedMs; });
}

bool StickerStore::saveIndex() const
{
    QJsonArray array;
    for (const StickerEntry &entry : m_entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("hash"), entry.hash);
        obj.insert(QStringLiteral("ext"), entry.ext);
        obj.insert(QStringLiteral("source"), entry.source);
        obj.insert(QStringLiteral("addedMs"), double(entry.addedMs));
        obj.insert(QStringLiteral("bytes"), double(entry.bytes));
        obj.insert(QStringLiteral("width"), entry.width);
        obj.insert(QStringLiteral("height"), entry.height);
        obj.insert(QStringLiteral("animated"), entry.animated);
        array.append(obj);
    }

    QSaveFile file(storageRoot() + QStringLiteral("/index.json"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return file.commit();
}
