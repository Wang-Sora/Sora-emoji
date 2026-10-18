#ifndef SORA_STICKERSPAGE_H
#define SORA_STICKERSPAGE_H

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class QSplitter;
class QToolButton;
class StickerStore;

/**
 * 表情库页：左侧操作列（监听开关 / 手动导入 / 存储位置 / 使用提示），右侧表情网格。
 *
 * 单击格子 = 写回剪贴板；右键 = 复制 / 打开 / 删除；图片文件可直接拖进来。
 */
class StickersPage : public QWidget
{
    Q_OBJECT

public:
    explicit StickersPage(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void reloadGrid();
    void importFromClipboard();
    void importFiles();
    void copyItem(QListWidgetItem *item);
    void showGridMenu(const QPoint &pos);
    void setStatus(const QString &text);
    void syncListenButton();
    void syncStoragePath();
    void updateCountLabel();

    StickerStore *m_store = nullptr;

    QSplitter *m_splitter = nullptr;
    bool m_splitterRestored = false;
    QToolButton *m_listenButton = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_grid = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_storagePath = nullptr;
};

#endif // SORA_STICKERSPAGE_H
