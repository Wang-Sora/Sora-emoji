#ifndef SORA_PDFPAGE_H
#define SORA_PDFPAGE_H

#include <QWidget>

class PdfController;
class QIntValidator;
class QLabel;
class QLineEdit;
class QListWidget;
class QModelIndex;
class QPdfBookmarkModel;
class QPdfView;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QTreeView;

/**
 * PDF 阅读页：左侧文档列表 + 目录树，右侧 QPdfView 渲染区 + 工具栏。
 *
 * QPdfView 是 QtPdfWidgets 提供的现成阅读控件（底层 PDFium），
 * 翻页、滚动、缩放都已经内置，这里只需要把工具栏按钮接到它的 API 上。
 */
class PdfPage : public QWidget
{
    Q_OBJECT

public:
    explicit PdfPage(QWidget *parent = nullptr);

    void retint();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void installShortcuts();
    void reloadLibrary();
    void openPath(const QString &filePath);
    void chooseFile();
    void updatePageLabel();
    void updatePageModeButton();
    void gotoPage(int oneBased);
    void stepPage(int delta);
    void savePosition();
    void restorePosition();
    void zoomIn();
    void zoomOut();
    void fitToWidth();
    void jumpToBookmark(const QModelIndex &index);

    PdfController *m_ctrl = nullptr;
    QSplitter *m_splitter = nullptr;
    bool m_splitterRestored = false;
    QPdfView *m_view = nullptr;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_docs = nullptr;
    QTreeView *m_outline = nullptr;
    QPdfBookmarkModel *m_bookmarks = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLineEdit *m_pageEdit = nullptr;
    QIntValidator *m_pageValidator = nullptr;
    QToolButton *m_zoomOut = nullptr;
    QToolButton *m_zoomIn = nullptr;
    QToolButton *m_fitWidth = nullptr;
    QToolButton *m_pageModeButton = nullptr;
    QToolButton *m_openButton = nullptr;
};

#endif // SORA_PDFPAGE_H
