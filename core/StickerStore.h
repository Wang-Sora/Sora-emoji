#ifndef SORA_STICKERSTORE_H
#define SORA_STICKERSTORE_H

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

class QMimeData;
class QTimer;

/**
 * 表情库存储：从剪贴板 / 文件采集图片，去重后落盘，并支持点击写回剪贴板。
 *
 * 目录布局（%APPDATA%/Sora/stickers/）：
 *   origin/    <sha256>.<ext>   原文件字节，绝不重编码 —— GIF 动画因此保真
 *   thumbs/    <sha256>.png     入库时预生成的 256px 缩略图，列表滚动不卡
 *   index.json                  元数据索引
 *
 * 文件名 = 内容 SHA-256，天然去重；类型一律靠魔数判断，不信任扩展名
 * （实测 QQ 表情缓存里存在「.jpg 后缀的真 GIF」）。
 */
class StickerStore : public QObject
{
    Q_OBJECT

public:
    explicit StickerStore(QObject *parent = nullptr);

    struct StickerEntry
    {
        QString hash;       // 内容 SHA-256（hex 小写），同时是 origin/thumbs 的文件名
        QString ext;        // 按魔数纠正后的扩展名：jpg/png/gif/webp/bmp
        QString source;     // 来源描述（原文件名或「剪贴板」）
        qint64 addedMs = 0; // 入库时间戳
        qint64 bytes = 0;
        int width = 0;
        int height = 0;
        bool animated = false;
    };

    enum class AddStatus
    {
        Added,      // 新入库
        Duplicate,  // 内容已存在（哈希命中）
        Empty,      // 来源里没有可识别的图片
        Failed      // 写盘 / 解码失败
    };

    struct AddOutcome
    {
        AddStatus status = AddStatus::Empty;
        QString hash;
        QString message; // 给状态栏的人类可读结果
    };

    /** 从剪贴板 mime 采集：优先文件引用（保住 GIF 动画），回落位图（只能拿到静帧） */
    AddOutcome addFromMime(const QMimeData *mime);
    AddOutcome addFromFile(const QString &path);
    AddOutcome addFromImage(const QImage &image, const QString &source);

    bool removeSticker(const QString &hash);
    QList<StickerEntry> entries() const { return m_entries; }

    QString originPath(const QString &hash) const;
    QString thumbPath(const QString &hash) const;
    QString storageRoot() const;

    /** 点击写回：静图同时给位图 + 文件引用（聊天框粘成图、资源管理器粘成文件）；
     *  GIF 只给文件引用 —— 位图会把动画退化成首帧。 */
    bool copyToClipboard(const QString &hash);

    /** 监听系统剪贴板自动收录；自己写回产生的那次 dataChanged 会被抑制，防止回环 */
    bool isListening() const { return m_listening; }
    void setListening(bool on);

Q_SIGNALS:
    /** 库内容变化（新增 / 删除），UI 据此刷新网格 */
    void changed();
    /** 一次采集动作的结果反馈（含重复 / 失败），UI 据此更新状态栏 */
    void imported(const StickerStore::AddOutcome &outcome);

private:
    AddOutcome addBytes(const QByteArray &data, const QString &source);
    void loadIndex();
    bool saveIndex() const;
    void onClipboardChanged();
    const StickerEntry *find(const QString &hash) const;

    QList<StickerEntry> m_entries;
    QString m_originDir;
    QString m_thumbDir;

    QTimer *m_collectTimer = nullptr; // dataChanged 防抖：一次复制可能连发多轮信号
    bool m_listening = false;
    bool m_suppressNext = false;      // 自己写回导致的 dataChanged 不再采集
};

#endif // SORA_STICKERSTORE_H
