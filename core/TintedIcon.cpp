#include "core/TintedIcon.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

QIcon render(const QByteArray &svgData, const QColor &color, int size)
{
    if (svgData.isEmpty() || size <= 0)
        return QIcon();

    QSvgRenderer renderer(svgData);

    const qreal dpr = 2.0; // 固定 2 倍图，缩到常规尺寸后边缘更干净
    QImage image(QSize(size, size) * dpr, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter);

        // 用整块纯色做 SourceIn，把 SVG 的所有不透明像素统一替换成目标色
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }

    image.setDevicePixelRatio(dpr);

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);

    return QIcon(pixmap);
}

} // namespace

namespace TintedIcon {

QIcon create(const QString &resourcePath, const QColor &color, int size)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon();

    return render(file.readAll(), color, size);
}

QIcon create(const QByteArray &svgData, const QColor &color, int size)
{
    return render(svgData, color, size);
}

} // namespace TintedIcon
