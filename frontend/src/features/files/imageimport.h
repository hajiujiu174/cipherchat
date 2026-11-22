#pragma once
#include <QImage>
#include <QString>
namespace Messager {
// All decoders inspect dimensions before allocating pixel buffers.
// 导入图片。
QImage importImage(const QString& path,QString* error=nullptr);
// 返回文件传输使用的图片AsPng。
QString imageAsPng(const QString& path,QString* error=nullptr);
}
