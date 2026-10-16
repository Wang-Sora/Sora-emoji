#ifndef SORA_PDFCONTROLLER_H
#define SORA_PDFCONTROLLER_H

#include <QObject>
#include <QString>

class QPdfDocument;

/**
 * PDF 文档封装：负责加载文件、把状态翻译成统一的 loaded / failed 信号。
 *
 * 注意：QPdfDocument 没有 errorOccurred 信号，加载失败只能通过
 * statusChanged(Status::Error) 拿到，所以这里统一在控制器里做一次翻译，
 * 界面层就不用再关心底层枚举。
 */
class PdfController : public QObject
{
    Q_OBJECT

public:
    explicit PdfController(QObject *parent = nullptr);
    ~PdfController() override;

    bool load(const QString &filePath);
    QPdfDocument *document() const { return m_doc; }

    QString filePath() const { return m_path; }
    QString fileName() const;
    int pageCount() const;

Q_SIGNALS:
    void loaded(const QString &fileName, int pageCount);
    void failed(const QString &message);

private:
    QString describeError() const;

    QPdfDocument *m_doc = nullptr;
    QString m_path;
};

#endif // SORA_PDFCONTROLLER_H
