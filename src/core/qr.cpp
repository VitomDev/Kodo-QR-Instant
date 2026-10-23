#include "core/qr.h"

#include "core/text.h"

#include <BitMatrix.h>
#include <ReadBarcode.h>
#include <qrcode/QRErrorCorrectionLevel.h>
#include <qrcode/QRWriter.h>

#include <algorithm>

namespace kodo {

namespace {

// Screen codes can be only a few pixels per module; below this the image is
// enlarged before a second attempt.
constexpr int kSmallImage = 400;

double Area(const ZXing::Barcode& barcode) {
    const auto& p = barcode.position();
    // Shoelace formula over the four corners.
    double sum = 0;
    for (int i = 0; i < 4; ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % 4];
        sum += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::abs(sum) / 2;
}

std::vector<std::wstring> Decode(const BgraImage& image) {
    const ZXing::ImageView view(image.pixels.data(), image.width, image.height, ZXing::ImageFormat::BGRA);
    const auto options = ZXing::ReaderOptions()
                             .setFormats(ZXing::BarcodeFormat::QRCode)
                             .setTryHarder(true)
                             .setTryRotate(true)
                             .setTryInvert(true)
                             .setTryDownscale(true)
                             .setTextMode(ZXing::TextMode::Plain);
    auto barcodes = ZXing::ReadBarcodes(view, options);
    std::stable_sort(barcodes.begin(), barcodes.end(), [](const auto& a, const auto& b) { return Area(a) > Area(b); });
    std::vector<std::wstring> texts;
    for (const auto& barcode : barcodes) {
        if (!barcode.isValid()) continue;
        std::wstring text = FromUtf8(barcode.text());
        if (!text.empty() && std::find(texts.begin(), texts.end(), text) == texts.end()) texts.push_back(std::move(text));
    }
    return texts;
}

bool FitsLatin1(const std::wstring& text) {
    return std::all_of(text.begin(), text.end(), [](wchar_t c) { return c < 0x100; });
}

}  // namespace

std::vector<std::wstring> DecodeQrCodes(const BgraImage& image) {
    if (image.empty()) return {};
    std::vector<std::wstring> texts = Decode(image);
    if (texts.empty() && std::max(image.width, image.height) < kSmallImage) {
        BgraImage larger = Upscale(image, std::clamp(kSmallImage * 2 / std::max(image.width, image.height), 2, 6));
        texts = Decode(larger);
        Wipe(larger.pixels);
    }
    return texts;
}

std::optional<QrMatrix> EncodeQrCode(const std::wstring& text) {
    if (text.empty()) return std::nullopt;
    try {
        ZXing::QRCode::Writer writer;
        writer.setMargin(0).setErrorCorrectionLevel(ZXing::QRCode::ErrorCorrectionLevel::Medium);
        if (!FitsLatin1(text)) writer.setEncoding(ZXing::CharacterSet::UTF8);
        // Size 0 asks for one pixel per module.
        const ZXing::BitMatrix bits = writer.encode(text, 0, 0);
        QrMatrix matrix;
        matrix.size = bits.width();
        matrix.dark.resize(static_cast<size_t>(matrix.size) * matrix.size);
        for (int y = 0; y < matrix.size; ++y) {
            for (int x = 0; x < matrix.size; ++x) matrix.dark[static_cast<size_t>(y) * matrix.size + x] = bits.get(x, y) ? 1 : 0;
        }
        return matrix;
    } catch (const std::exception&) {
        // Too much data for the largest QR version.
        return std::nullopt;
    }
}

BgraImage RenderQrCode(const QrMatrix& matrix, int moduleSize) {
    BgraImage image;
    if (matrix.empty() || moduleSize <= 0) return image;
    const int modules = matrix.size + 2 * kQuietZoneModules;
    image.width = image.height = modules * moduleSize;
    image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) {
            if (!matrix.IsDark(x, y)) continue;
            const int left = (x + kQuietZoneModules) * moduleSize;
            const int top = (y + kQuietZoneModules) * moduleSize;
            for (int row = top; row < top + moduleSize; ++row) {
                uint8_t* pixel = &image.pixels[(static_cast<size_t>(row) * image.width + left) * 4];
                for (int i = 0; i < moduleSize; ++i, pixel += 4) pixel[0] = pixel[1] = pixel[2] = 0;
            }
        }
    }
    return image;
}

int ModuleSizeFor(const QrMatrix& matrix, int targetPixels) {
    if (matrix.empty()) return 1;
    return std::max(1, targetPixels / (matrix.size + 2 * kQuietZoneModules));
}

}  // namespace kodo
