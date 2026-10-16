#include "core/PdfController.h"

#include <QFileInfo>
#include <QPdfDocument>

PdfController::PdfController(QObject *parent)
    : QObject(parent)
    , m_doc(new QPdfDocument(this))
{
    connect(m_doc, &QPdfDocument::statusChanged, this,
            [this](QPdfDocument::Status status) {
                if (status == QPdfDocument::Status::Ready)
                    emit loaded(fileName(), m_doc->pageCount());
                else if (status == QPdfDocument::Status::Error)
                    emit failed(describeError());
            });
}

PdfController::~PdfController() = default;

bool PdfController::load(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    m_path = filePath;
    const QPdfDocument::Error error = m_doc->load(filePath);
    if (error != QPdfDocument::Error::None) {
        // statusChanged 也会同步触发一次 failed，这里不重复发
        return false;
    }

    return true;
}

QString PdfController::fileName() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

int PdfController::pageCount() const
{
    return m_doc->pageCount();
}

QString PdfController::describeError() const
{
    switch (m_doc->error()) {
    case QPdfDocument::Error::None:
        return QString();
    case QPdfDocument::Error::FileNotFound:
        return QStringLiteral("找不到文件，请确认路径是否正确");
    case QPdfDocument::Error::InvalidFileFormat:
        return QStringLiteral("文件不是有效的 PDF，或已损坏");
    case QPdfDocument::Error::IncorrectPassword:
        return QStringLiteral("该 PDF 受密码保护，暂不支持");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QStringLiteral("该 PDF 使用了不支持的加密方式");
    case QPdfDocument::Error::DataNotYetAvailable:
        return QStringLiteral("文件数据尚未就绪，请稍后重试");
    default:
        return QStringLiteral("打开 PDF 失败（未知错误）");
    }
}
