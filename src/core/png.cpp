#include "core/png.h"

#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <fstream>

namespace kodo {

namespace {

// Minimal owning wrapper; Kodo does not otherwise need WRL or C++/WinRT.
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

bool CreateFactory(Com<IWICImagingFactory>& factory) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
}

}  // namespace

std::vector<uint8_t> EncodePng(const BgraImage& image) {
    if (image.empty()) return {};
    Com<IWICImagingFactory> factory;
    Com<IStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    if (!CreateFactory(factory) || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(image.width, image.height))) {
        return {};
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return {};
    // WIC may pick a smaller format (QR codes are pure black and white).
    Com<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA, image.width * 4,
                                               static_cast<UINT>(image.pixels.size()),
                                               const_cast<BYTE*>(image.pixels.data()), &bitmap))) {
        return {};
    }
    if (FAILED(frame->WriteSource(bitmap.p, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};

    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return {};
    std::vector<uint8_t> data(static_cast<size_t>(stat.cbSize.QuadPart));
    const LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(data.data(), static_cast<ULONG>(data.size()), &read)) || read != data.size()) {
        return {};
    }
    return data;
}

bool SavePng(const BgraImage& image, const std::filesystem::path& path) {
    const std::vector<uint8_t> data = EncodePng(image);
    if (data.empty()) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return file.good();
}

std::optional<BgraImage> DecodeImage(const std::vector<uint8_t>& data) {
    Com<IWICImagingFactory> factory;
    Com<IStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    Com<IWICFormatConverter> converter;
    if (data.empty() || !CreateFactory(factory)) return std::nullopt;
    stream.p = SHCreateMemStream(data.data(), static_cast<UINT>(data.size()));
    if (!stream.p || FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }
    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    BgraImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels.resize(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) {
        return std::nullopt;
    }
    return image;
}

}  // namespace kodo
