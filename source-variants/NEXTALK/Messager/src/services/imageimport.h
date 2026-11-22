#pragma once
#include <QImage>
#include <QString>
namespace Messager {
// All decoders inspect dimensions before allocating pixel buffers.
QImage importImage(const QString& path,QString* error=nullptr);
QString imageAsPng(const QString& path,QString* error=nullptr);
}
