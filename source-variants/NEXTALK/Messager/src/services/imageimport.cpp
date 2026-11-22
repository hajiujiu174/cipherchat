#include "imageimport.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincodec.h>
#endif
namespace Messager {
QImage importImage(const QString& path,QString* error) {
    auto fail=[&](const QString& message){if(error)*error=message;return QImage{};};
    if(QFileInfo(path).size()>16*1024*1024)return fail("请选择不超过16MB的图片");
    QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();
    if(size.isValid()){
        if(qint64(size.width())*size.height()>16*1024*1024)return fail("请选择不超过1600万像素的图片");
        reader.setScaledSize(size.scaled(QSize(256,256).boundedTo(size),Qt::KeepAspectRatio));const auto image=reader.read();if(!image.isNull())return image;
    }
#ifdef Q_OS_WIN
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;
    QImage result;UINT width=0,height=0;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
       SUCCEEDED(factory->CreateDecoderFromFilename(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)) &&
       SUCCEEDED(decoder->GetFrame(0,&frame)) && SUCCEEDED(frame->GetSize(&width,&height)) && width && height && quint64(width)*height<=16*1024*1024 &&
       SUCCEEDED(factory->CreateFormatConverter(&converter)) && SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) {
        QImage pixels(int(width),int(height),QImage::Format_RGBA8888);
        if(!pixels.isNull()&&SUCCEEDED(converter->CopyPixels(nullptr,UINT(pixels.bytesPerLine()),UINT(pixels.sizeInBytes()),pixels.bits())))result=pixels.scaled(QSize(256,256).boundedTo(pixels.size()),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
    if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(factory)factory->Release();if(SUCCEEDED(initialized))CoUninitialize();
    if(!result.isNull())return result;
#endif
    return fail("无法解码这张图片，或图片尺寸超过限制");
}
QString imageAsPng(const QString& path,QString* error) {
    const auto image=importImage(path,error);if(image.isNull())return {};
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG")||bytes.size()>288*1024){if(error)*error="图片处理失败";return {};}
    return QString::fromLatin1(bytes.toBase64());
}
}
