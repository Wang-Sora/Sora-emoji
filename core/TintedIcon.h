#ifndef SORA_TINTEDICON_H
#define SORA_TINTEDICON_H

#include <QIcon>
#include <QByteArray>
#include <QColor>
#include <QString>

/**
 * SVG 着色工具。
 *
 * Qt 的原生 SVG 图标无法在运行时改色（图标插件缓存 + 不支持 color 覆盖），
 * 所以这里自己用 QSvgRenderer 渲染成位图，再用 SourceIn 合成模式整体上色。
 * 这样同一份 SVG 可以在深/浅主题下呈现不同颜色。
 */
namespace TintedIcon {

QIcon create(const QString &resourcePath, const QColor &color, int size = 20);
QIcon create(const QByteArray &svgData, const QColor &color, int size = 20);

} // namespace TintedIcon

#endif // SORA_TINTEDICON_H
