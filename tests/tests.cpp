// Kodo's automated tests: a minimal runner without dependencies.
//
//   KodoTests.exe <build dir>
//
// The build dir holds Kodo.exe, used by the lifecycle test. Tests that need
// something unavailable report SKIP instead of passing.

#include "app/screen_capture.h"
#include "core/clipboard.h"
#include "core/geometry.h"
#include "core/history.h"
#include "core/hotkey.h"
#include "core/image.h"
#include "core/payload.h"
#include "core/png.h"
#include "core/qr.h"
#include "core/settings.h"
#include "core/text.h"

#include <windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace kodo;
namespace fs = std::filesystem;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> run;
};

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
};

struct Failure {
    std::string message;
};
struct Skip {
    std::string reason;
};

#define TEST(name)                                     \
    static void name();                                \
    static Registrar name##_registrar(#name, name);    \
    static void name()

#define CHECK(expression)                                                                                  \
    do {                                                                                                   \
        if (!(expression)) throw Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #expression}; \
    } while (false)

#define SKIP(reason) throw Skip{reason}

fs::path buildDir;

constexpr wchar_t kTestSettingsKey[] = L"Software\\Kodo QR Instant Tests";
constexpr wchar_t kTestRunValue[] = L"Kodo QR Instant Test";

fs::path TemporaryFile(const wchar_t* name) {
    wchar_t folder[MAX_PATH];
    GetTempPathW(MAX_PATH, folder);
    return fs::path(folder) / L"KodoTests" / name;
}

// A QR code as it appears on screen: rendered, placed on a background with
// other content around it, optionally inverted (dark themes).
BgraImage RenderOnScreen(const std::wstring& text, int moduleSize, bool inverted = false, int border = 40) {
    const auto matrix = EncodeQrCode(text);
    if (!matrix) return {};
    const BgraImage code = RenderQrCode(*matrix, moduleSize);
    BgraImage screen;
    screen.width = code.width + 2 * border;
    screen.height = code.height + 2 * border;
    screen.pixels.assign(static_cast<size_t>(screen.width) * screen.height * 4, 240);
    for (int y = 0; y < code.height; ++y) {
        std::copy_n(&code.pixels[static_cast<size_t>(y) * code.width * 4], static_cast<size_t>(code.width) * 4,
                    &screen.pixels[(static_cast<size_t>(y + border) * screen.width + border) * 4]);
    }
    for (size_t i = 0; i < screen.pixels.size(); i += 4) {
        if (inverted) {
            for (int c = 0; c < 3; ++c) screen.pixels[i + c] = static_cast<uint8_t>(255 - screen.pixels[i + c]);
        }
        screen.pixels[i + 3] = 255;
    }
    return screen;
}

}  // namespace

// --- Geometry and capture coordinates -----------------------------------------

TEST(SelectionWorksInEveryDragDirection) {
    const RECT bounds{0, 0, 1000, 800};
    const RECT forward = SelectionRect({10, 20}, {110, 70}, bounds);
    const RECT backward = SelectionRect({110, 70}, {10, 20}, bounds);
    CHECK(EqualRect(&forward, &backward));
    CHECK(forward.left == 10 && forward.top == 20);
    // The end pixel is included: 10..110 is 101 pixels wide.
    CHECK(forward.right - forward.left == 101 && forward.bottom - forward.top == 51);
}

TEST(SelectionIsClampedToTheScreen) {
    const RECT bounds{0, 0, 1000, 800};
    const RECT rect = SelectionRect({-50, -50}, {2000, 2000}, bounds);
    CHECK(rect.left == 0 && rect.top == 0 && rect.right == 1000 && rect.bottom == 800);
}

TEST(TinyOrEmptySelectionsAreRejected) {
    const RECT bounds{0, 0, 1000, 800};
    CHECK(!IsUsableSelection(SelectionRect({5, 5}, {5, 5}, bounds)));
    CHECK(!IsUsableSelection(SelectionRect({5, 5}, {300, 6}, bounds)));
    CHECK(IsUsableSelection(SelectionRect({5, 5}, {40, 30}, bounds)));
}

TEST(NegativeVirtualScreenCoordinatesMapToTheSnapshot) {
    // A monitor left of and above the primary one: the virtual screen starts at (-1920, -400).
    const RECT virtualScreen{-1920, -400, 2560, 1440};
    const POINT primary = ScreenToSnapshot({0, 0}, virtualScreen);
    CHECK(primary.x == 1920 && primary.y == 400);
    const RECT onScreen = SnapshotToScreen(RECT{100, 50, 300, 120}, virtualScreen);
    CHECK(onScreen.left == -1820 && onScreen.top == -350 && onScreen.right == -1620 && onScreen.bottom == -280);
}

TEST(CropCopiesTheRightPixelsAndClamps) {
    BgraImage image;
    image.width = 4;
    image.height = 3;
    image.pixels.resize(4 * 3 * 4);
    for (int i = 0; i < 12; ++i) image.pixels[i * 4] = static_cast<uint8_t>(i);
    const BgraImage crop = Crop(image, RECT{1, 1, 3, 3});
    CHECK(crop.width == 2 && crop.height == 2);
    CHECK(crop.pixels[0] == 5 && crop.pixels[4] == 6 && crop.pixels[8] == 9 && crop.pixels[12] == 10);
    const BgraImage clamped = Crop(image, RECT{-5, -5, 50, 50});
    CHECK(clamped.width == 4 && clamped.height == 3);
}

TEST(UpscaleRepeatsPixels) {
    BgraImage image;
    image.width = 2;
    image.height = 1;
    image.pixels = {0, 0, 0, 255, 255, 255, 255, 255};
    const BgraImage large = Upscale(image, 3);
    CHECK(large.width == 6 && large.height == 3);
    CHECK(large.pixels[(2 * 6 + 2) * 4] == 0 && large.pixels[(2 * 6 + 3) * 4] == 255);
}

// --- Shortcuts -------------------------------------------------------------------

TEST(HotkeysRoundTrip) {
    const Hotkey hotkey{MOD_CONTROL | MOD_SHIFT, 'Q'};
    CHECK(Unpack(Pack(hotkey)) == hotkey);
    CHECK(FromHotkeyControl(ToHotkeyControl(hotkey)) == hotkey);
}

TEST(HotkeysNeedAModifier) {
    CHECK((Hotkey{MOD_CONTROL | MOD_SHIFT, 'Q'}.IsValid()));
    CHECK(!(Hotkey{0, 'Q'}.IsValid()));
    CHECK(!(Hotkey{MOD_CONTROL, 0}.IsValid()));
}

TEST(HotkeysHaveReadableNames) {
    CHECK(ToString({MOD_CONTROL | MOD_SHIFT, 'Q'}) == L"Ctrl + Shift + Q");
    CHECK(ToString({MOD_ALT, VK_F2}) == L"Alt + F2");
}

TEST(HotkeyRegistrationAndConflicts) {
    // Registering the same shortcut twice must fail the second time, which is
    // how Kodo detects a shortcut taken by another application.
    const Hotkey hotkey{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11};
    if (!RegisterHotKey(nullptr, 0x6B1, hotkey.modifiers, hotkey.key)) SKIP("test shortcut already in use");
    bool second = false;
    std::thread other([&] {
        second = RegisterHotKey(nullptr, 0x6B2, hotkey.modifiers, hotkey.key) != FALSE;
        if (second) UnregisterHotKey(nullptr, 0x6B2);
    });
    other.join();
    CHECK(!second);
    CHECK(UnregisterHotKey(nullptr, 0x6B1));
}

// --- QR codes ----------------------------------------------------------------------

TEST(CreatedCodesReadBack) {
    for (const std::wstring text : {L"https://github.com/VitomDev", L"Hello, world!", L"1234567890"}) {
        const auto found = DecodeQrCodes(RenderOnScreen(text, 6));
        CHECK(found.size() == 1 && found[0] == text);
    }
}

TEST(AccentsEmojiAndOtherAlphabetsSurvive) {
    for (const std::wstring text : {L"Espa\u00f1a, ma\u00f1ana, cami\u00f3n", L"\u65e5\u672c\u8a9e\u306e\u30c6\u30ad\u30b9\u30c8",
                                    L"\u041f\u0440\u0438\u0432\u0435\u0442 \U0001F600", L"Line 1\nLine 2"}) {
        const auto found = DecodeQrCodes(RenderOnScreen(text, 5));
        CHECK(found.size() == 1 && found[0] == text);
    }
}

TEST(ReadsTinyCodesOnScreen) {
    // Two pixels per module, as a QR in a web page at normal zoom can be.
    const std::wstring text = L"https://example.com/a/rather/long/path?with=query&and=more";
    const auto found = DecodeQrCodes(RenderOnScreen(text, 2, false, 12));
    CHECK(found.size() == 1 && found[0] == text);
}

TEST(ReadsLightCodesOnDarkBackgrounds) {
    const auto found = DecodeQrCodes(RenderOnScreen(L"dark theme", 5, true));
    CHECK(found.size() == 1 && found[0] == L"dark theme");
}

TEST(ReadsSeveralCodesLargestFirst) {
    const BgraImage smallCode = RenderOnScreen(L"smallCode", 3, false, 10);
    const BgraImage largeCode = RenderOnScreen(L"largeCode", 8, false, 10);
    BgraImage screen;
    screen.width = smallCode.width + largeCode.width + 20;
    screen.height = std::max(smallCode.height, largeCode.height);
    screen.pixels.assign(static_cast<size_t>(screen.width) * screen.height * 4, 255);
    const auto paste = [&](const BgraImage& image, int left) {
        for (int y = 0; y < image.height; ++y) {
            std::copy_n(&image.pixels[static_cast<size_t>(y) * image.width * 4], static_cast<size_t>(image.width) * 4,
                        &screen.pixels[(static_cast<size_t>(y) * screen.width + left) * 4]);
        }
    };
    paste(smallCode, 0);
    paste(largeCode, smallCode.width + 20);
    const auto found = DecodeQrCodes(screen);
    CHECK(found.size() == 2 && found[0] == L"largeCode" && found[1] == L"smallCode");
}

TEST(ImagesWithoutCodesFindNothing) {
    BgraImage blank;
    blank.width = blank.height = 200;
    blank.pixels.assign(200 * 200 * 4, 255);
    CHECK(DecodeQrCodes(blank).empty());
    CHECK(DecodeQrCodes(BgraImage{}).empty());
}

TEST(TooMuchTextIsRejected) {
    CHECK(!EncodeQrCode(L""));
    CHECK(EncodeQrCode(std::wstring(2000, L'a')));
    CHECK(!EncodeQrCode(std::wstring(5000, L'a')));
}

TEST(RenderedCodesHaveAQuietZone) {
    const auto matrix = EncodeQrCode(L"quiet");
    CHECK(matrix && matrix->size >= 21);
    const BgraImage image = RenderQrCode(*matrix, 3);
    CHECK(image.width == (matrix->size + 8) * 3 && image.height == image.width);
    // The corner is white quiet zone; the first module (finder pattern) is dark.
    CHECK(image.pixels[0] == 255);
    const size_t finder = (static_cast<size_t>(12) * image.width + 12) * 4;
    CHECK(image.pixels[finder] == 0);
    CHECK(ModuleSizeFor(*matrix, 1000) == 1000 / (matrix->size + 8));
}

// --- Payloads ----------------------------------------------------------------------

TEST(RecognizesLinks) {
    CHECK(ParsePayload(L"https://example.com").kind == PayloadKind::Link);
    CHECK(ParsePayload(L"  HTTP://EXAMPLE.COM/x  ").kind == PayloadKind::Link);
    // Other schemes could start programs: shown as text, never opened.
    CHECK(ParsePayload(L"file:///C:/Windows/notepad.exe").kind == PayloadKind::Text);
    CHECK(ParsePayload(L"javascript:alert(1)").kind == PayloadKind::Text);
    CHECK(ParsePayload(L"https://example.com and more words").kind == PayloadKind::Text);
    CHECK(!IsWebLink(L"https://"));
}

TEST(ReadsWifiCodes) {
    const Payload payload = ParsePayload(L"WIFI:S:My\\;Home;T:WPA;P:pa\\:ss\\\\word;H:true;;");
    CHECK(payload.kind == PayloadKind::Wifi);
    CHECK(payload.wifi.ssid == L"My;Home");
    CHECK(payload.wifi.password == L"pa:ss\\word");
    CHECK(payload.wifi.security == L"WPA");
    CHECK(payload.wifi.hidden);
    const Payload open = ParsePayload(L"WIFI:T:nopass;S:Cafe;;");
    CHECK(open.wifi.ssid == L"Cafe" && open.wifi.security.empty() && open.wifi.password.empty());
    CHECK(Describe(payload).find(L"Password: pa:ss\\word") != std::wstring::npos);
}

TEST(WifiCodesRoundTrip) {
    WifiNetwork network{L"Caf\u00e9 \"5G\", 2nd;floor", L"p@ss:w;rd\\", L"WPA", false};
    const std::wstring text = BuildWifi(network);
    CHECK(text.rfind(L"WIFI:T:WPA;S:", 0) == 0 && text.ends_with(L";;"));
    const Payload parsed = ParsePayload(text);
    CHECK(parsed.wifi.ssid == network.ssid && parsed.wifi.password == network.password && parsed.wifi.security == L"WPA");
    CHECK(BuildWifi({L"Open", L"ignored", L"", true}) == L"WIFI:T:nopass;S:Open;H:true;;");
}

TEST(EmailCodesRoundTrip) {
    EmailMessage message{L"hello@example.com", L"Hola & adi\u00f3s", L"Line one\nLine two = 100%"};
    const std::wstring text = BuildEmail(message);
    CHECK(text.rfind(L"mailto:hello@example.com?subject=Hola%20%26%20adi%C3%B3s&body=", 0) == 0);
    CHECK(text.find(L"%0D%0A") != std::wstring::npos);
    const Payload parsed = ParsePayload(text);
    CHECK(parsed.kind == PayloadKind::Email);
    CHECK(parsed.email.to == message.to && parsed.email.subject == message.subject && parsed.email.body == message.body);
    CHECK(BuildEmail({L"a@b.c", L"", L""}) == L"mailto:a@b.c");
    const Payload matmsg = ParsePayload(L"MATMSG:TO:x@y.z;SUB:Hi;BODY:There;;");
    CHECK(matmsg.kind == PayloadKind::Email && matmsg.email.to == L"x@y.z" && matmsg.email.body == L"There");
}

TEST(ContactCodesRoundTrip) {
    Contact contact{L"Ana Garc\u00eda", L"+34 600 000 000", L"ana@example.com", L"Acme, Inc.", L"https://example.com"};
    const std::wstring text = BuildContact(contact);
    CHECK(text.rfind(L"BEGIN:VCARD\nVERSION:3.0\n", 0) == 0 && text.ends_with(L"END:VCARD"));
    CHECK(text.find(L"ORG:Acme\\, Inc.") != std::wstring::npos);
    const Payload parsed = ParsePayload(text);
    CHECK(parsed.kind == PayloadKind::Contact);
    CHECK(parsed.contact.name == contact.name && parsed.contact.phone == contact.phone && parsed.contact.email == contact.email);
    CHECK(parsed.contact.organization == contact.organization && parsed.contact.website == contact.website);
    // Optional fields are left out.
    CHECK(BuildContact({L"Bo", L"", L"", L"", L""}) == L"BEGIN:VCARD\nVERSION:3.0\nN:;Bo;;;\nFN:Bo\nEND:VCARD");
}

TEST(ReadsContactsFromOtherGenerators) {
    const Payload vcard = ParsePayload(L"BEGIN:VCARD\r\nVERSION:4.0\r\nN:Doe;John;;;\r\nitem1.TEL;TYPE=CELL:+1 555\r\n"
                                       L"EMAIL;TYPE=work:john@example.com\r\nORG:Example;Sales\r\nEND:VCARD");
    CHECK(vcard.contact.name == L"John Doe" && vcard.contact.phone == L"+1 555");
    CHECK(vcard.contact.email == L"john@example.com" && vcard.contact.organization == L"Example");
    const Payload mecard = ParsePayload(L"MECARD:N:Doe,Jane;TEL:123;EMAIL:jane@x.y;;");
    CHECK(mecard.kind == PayloadKind::Contact && mecard.contact.name == L"Jane Doe" && mecard.contact.phone == L"123");
}

TEST(ReadsPhoneSmsAndLocation) {
    CHECK(ParsePayload(L"tel:+34600000000").number == L"+34600000000");
    const Payload smsto = ParsePayload(L"SMSTO:+1555:See you at 5");
    CHECK(smsto.kind == PayloadKind::Sms && smsto.number == L"+1555" && smsto.message == L"See you at 5");
    const Payload sms = ParsePayload(L"sms:+1555?body=Hi%20there");
    CHECK(sms.number == L"+1555" && sms.message == L"Hi there");
    const Payload geo = ParsePayload(L"geo:40.4168,-3.7038,650");
    CHECK(geo.kind == PayloadKind::Location && geo.location == L"40.4168, -3.7038");
    CHECK(ParsePayload(L"Just some text").kind == PayloadKind::Text);
}

TEST(PercentEncodingKeepsUnicode) {
    const std::wstring text = L"a b/\u00f1?\U0001F600";
    CHECK(PercentDecode(PercentEncode(text)) == text);
    CHECK(PercentDecode(L"100%") == L"100%");  // A stray percent sign stays as it is.
    CHECK(PercentDecode(L"\U0001F600%20x") == L"\U0001F600 x");
}

// --- Images and clipboard ---------------------------------------------------------------

TEST(PngRoundTrip) {
    const auto matrix = EncodeQrCode(L"png");
    const BgraImage image = RenderQrCode(*matrix, 4);
    const std::vector<uint8_t> png = EncodePng(image);
    CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    const auto decoded = DecodeImage(png);
    CHECK(decoded && decoded->width == image.width && decoded->height == image.height);
    CHECK(decoded->pixels == image.pixels);

    const fs::path file = TemporaryFile(L"code.png");
    fs::create_directories(file.parent_path());
    CHECK(SavePng(image, file));
    CHECK(fs::file_size(file) == png.size());
    fs::remove(file);
}

TEST(ClipboardStoresUnicodeText) {
    const auto previous = GetClipboardText(nullptr);
    const std::wstring text = L"Kodo \u00f1 \u65e5\u672c\u8a9e \U0001F600\r\nline 2";
    CHECK(SetClipboardText(nullptr, text));
    CHECK(GetClipboardText(nullptr) == text);
    if (previous) SetClipboardText(nullptr, *previous);
}

TEST(ClipboardStoresImagesAsBitmapAndPng) {
    const auto previous = GetClipboardText(nullptr);
    const BgraImage image = RenderQrCode(*EncodeQrCode(L"clipboard"), 3);
    CHECK(SetClipboardImage(nullptr, image));
    CHECK(IsClipboardFormatAvailable(CF_DIB));
    CHECK(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")));
    // What another app pastes is the same code.
    CHECK(OpenClipboard(nullptr));
    const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(GetClipboardData(CF_DIB)));
    CHECK(header && header->biWidth == image.width && header->biHeight == image.height);
    BgraImage pasted;
    pasted.width = header->biWidth;
    pasted.height = header->biHeight;
    pasted.pixels.resize(static_cast<size_t>(pasted.width) * pasted.height * 4);
    const auto* bits = reinterpret_cast<const uint8_t*>(header + 1);
    for (int y = 0; y < pasted.height; ++y) {
        std::copy_n(bits + static_cast<size_t>(pasted.height - 1 - y) * pasted.width * 4, static_cast<size_t>(pasted.width) * 4,
                    &pasted.pixels[static_cast<size_t>(y) * pasted.width * 4]);
    }
    GlobalUnlock(GetClipboardData(CF_DIB));
    CloseClipboard();
    const auto found = DecodeQrCodes(pasted);
    CHECK(found.size() == 1 && found[0] == L"clipboard");
    if (previous) SetClipboardText(nullptr, *previous);
}

TEST(ReadsACodeShownOnTheRealScreen) {
    // Draws a code in a topmost window and reads it back from a screen capture,
    // the same path as the Read QR shortcut.
    const BgraImage code = RenderQrCode(*EncodeQrCode(L"on screen"), 4);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KodoTestCode";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 100, 100,
                                  code.width, code.height, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(window);
    HDC dc = GetDC(window);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), code.width, -code.height, 1, 32, BI_RGB};
    for (int i = 0; i < 10; ++i) {
        SetDIBitsToDevice(dc, 0, 0, code.width, code.height, 0, 0, 0, code.height, code.pixels.data(), &info, DIB_RGB_COLORS);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        Sleep(30);
    }
    RECT bounds;
    GetWindowRect(window, &bounds);
    auto snapshot = ScreenSnapshot::Capture();
    ReleaseDC(window, dc);
    DestroyWindow(window);
    CHECK(snapshot);
    const POINT topLeft = ScreenToSnapshot({bounds.left, bounds.top}, snapshot->bounds());
    const BgraImage captured = snapshot->Crop(RECT{topLeft.x - 20, topLeft.y - 20, topLeft.x + code.width + 20, topLeft.y + code.height + 20});
    const auto found = DecodeQrCodes(captured);
    if (found.empty()) SKIP("the test window was not visible (locked or remote session)");
    CHECK(found[0] == L"on screen");
}

// --- History ----------------------------------------------------------------------------

TEST(HistoryKeepsNewestFirst) {
    const fs::path file = TemporaryFile(L"history.txt");
    History history(file);
    history.Clear();
    CHECK(history.Load().empty());
    CHECK(history.Add(HistoryEntry::Origin::Read, L"first", 100));
    CHECK(history.Add(HistoryEntry::Origin::Created, L"line 1\nline 2\twith \\ tab", 200));
    const auto entries = history.Load();
    CHECK(entries.size() == 2);
    CHECK(entries[0].text == L"line 1\nline 2\twith \\ tab" && entries[0].origin == HistoryEntry::Origin::Created && entries[0].time == 200);
    CHECK(entries[1].text == L"first" && entries[1].origin == HistoryEntry::Origin::Read);
    CHECK(history.Remove(1));
    CHECK(history.Load().size() == 1);
    CHECK(history.Clear());
    CHECK(!fs::exists(file));
}

TEST(HistoryDoesNotRepeatTheNewestEntry) {
    History history(TemporaryFile(L"history-repeat.txt"));
    history.Clear();
    history.Add(HistoryEntry::Origin::Read, L"same", 1);
    history.Add(HistoryEntry::Origin::Read, L"same", 5);
    const auto entries = history.Load();
    CHECK(entries.size() == 1 && entries[0].time == 5);
    CHECK(!history.Add(HistoryEntry::Origin::Read, L"   ", 6));  // Blank text is not stored.
    history.Clear();
}

TEST(HistoryIsLimited) {
    History history(TemporaryFile(L"history-limit.txt"));
    history.Clear();
    for (int i = 0; i < static_cast<int>(History::kMaxEntries) + 15; ++i) {
        history.Add(HistoryEntry::Origin::Created, L"entry " + std::to_wstring(i), i);
    }
    const auto entries = history.Load();
    CHECK(entries.size() == History::kMaxEntries);
    CHECK(entries.front().text == L"entry " + std::to_wstring(History::kMaxEntries + 14));
    history.Clear();
}

TEST(HistoryIgnoresDamagedLines) {
    const fs::path file = TemporaryFile(L"history-damaged.txt");
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "garbage\nR\t10\tgood\nX\t1\tbad\n\nC\tnot a number\tstill text\n";
    const auto entries = History(file).Load();
    CHECK(entries.size() == 2 && entries[0].text == L"good" && entries[1].text == L"still text");
    History(file).Clear();
}

TEST(TimesAreReadable) {
    const int64_t now = UnixTimeNow();
    CHECK(FormatTime(now).size() == 5);  // "14:05"
    CHECK(FormatTime(now - 24 * 3600).rfind(L"Yesterday ", 0) == 0);
    CHECK(FormatTime(0).find(L"1970") != std::wstring::npos || FormatTime(0).find(L"1969") != std::wstring::npos);
}

TEST(TextHelpers) {
    CHECK(ToWindowsLineBreaks(L"a\nb\r\nc\rd") == L"a\r\nb\r\nc\r\nd");
    CHECK(ToUnixLineBreaks(L"a\r\nb\rc") == L"a\nb\nc");
    CHECK(Trim(L"  x y \n") == L"x y");
    CHECK(OneLinePreview(L"one\n  two   three", 100) == L"one two three");
    CHECK(OneLinePreview(L"abcdefghij", 4) == L"abcd\u2026");
    CHECK(FromUtf8(ToUtf8(L"\u00f1\U0001F600")) == L"\u00f1\U0001F600");
}

// --- Settings and Start with Windows ---------------------------------------------------

TEST(SettingsPersist) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    Settings settings = Settings::Load(kTestSettingsKey);
    CHECK(settings.readHotkey == (Hotkey{MOD_CONTROL | MOD_SHIFT, 'Q'}));
    CHECK(settings.createHotkey == (Hotkey{MOD_CONTROL | MOD_SHIFT, 'G'}));
    CHECK(settings.keepHistory);

    settings.readHotkey = {MOD_CONTROL | MOD_ALT, 'R'};
    settings.keepHistory = false;
    CHECK(settings.Save(kTestSettingsKey));
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.readHotkey == settings.readHotkey && loaded.createHotkey == settings.createHotkey);
    CHECK(!loaded.keepHistory);
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(InvalidStoredShortcutsFallBackToDefaults) {
    HKEY key;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kTestSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const DWORD noModifier = Pack({0, 'Q'});
    RegSetValueExW(key, L"ReadHotkey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&noModifier), sizeof(noModifier));
    RegCloseKey(key);
    CHECK(Settings::Load(kTestSettingsKey).readHotkey == (Hotkey{MOD_CONTROL | MOD_SHIFT, 'Q'}));

    // Two equal shortcuts cannot both work.
    const DWORD same = Pack({MOD_CONTROL | MOD_SHIFT, 'G'});
    RegSetKeyValueW(HKEY_CURRENT_USER, kTestSettingsKey, L"ReadHotkey", REG_DWORD, &same, sizeof(same));
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(!(loaded.readHotkey == loaded.createHotkey));
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(FirstRunIsReportedOnce) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    CHECK(ConsumeFirstRun(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    CHECK(Settings{}.Save(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(AutostartCanBeEnabledDisabledAndReenabled) {
    const std::wstring exe = L"C:\\Program Files\\Kodo Test\\Kodo.exe";
    SetAutostartEnabled(false, exe, kTestRunValue);
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    wchar_t value[512]{};
    DWORD size = sizeof(value);
    CHECK(RegGetValueW(HKEY_CURRENT_USER, kRunKey, kTestRunValue, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS);
    CHECK(std::wstring(value) == L"\"C:\\Program Files\\Kodo Test\\Kodo.exe\" --background");
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
    CHECK(!IsAutostartEnabled(kTestRunValue));
}

TEST(AutostartRespectsTaskManagerDisable) {
    const std::wstring exe = L"C:\\Kodo\\Kodo.exe";
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    // Task Manager marks a disabled startup entry with an odd first byte.
    const BYTE disabled[12]{3};
    RegSetKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kTestRunValue, REG_BINARY, disabled, sizeof(disabled));
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
}

// --- Application lifecycle ----------------------------------------------------------------------

namespace {

HWND FindKodoWindow() { return FindWindowW(L"KodoQRInstantTray", nullptr); }

PROCESS_INFORMATION StartKodo() {
    std::wstring command = L"\"" + (buildDir / L"Kodo.exe").wstring() + L"\" --background";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (process.hThread) CloseHandle(process.hThread);
    return process;
}

HWND WaitForKodoWindow() {
    for (int i = 0; i < 100; ++i) {
        if (HWND window = FindKodoWindow()) return window;
        Sleep(50);
    }
    return nullptr;
}

}  // namespace

TEST(SingleInstanceAndCleanShutdown) {
    if (!fs::exists(buildDir / L"Kodo.exe")) SKIP("Kodo.exe not built");
    if (FindKodoWindow()) SKIP("Kodo is already running on this machine");

    // Keep the user's first-run state: mark it done for this run, restore it after.
    const bool firstRunPending = ConsumeFirstRun();
    struct RestoreFirstRun {
        bool pending;
        ~RestoreFirstRun() {
            if (pending) RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"FirstRunDone");
        }
    } restoreFirstRun{firstRunPending};

    // Hold Kodo's default shortcut so it starts with a conflict: it must keep running.
    const bool holdingShortcut = RegisterHotKey(nullptr, 0x6B3, MOD_CONTROL | MOD_SHIFT, 'Q') != FALSE;

    PROCESS_INFORMATION first = StartKodo();
    CHECK(first.hProcess != nullptr);
    HWND window = WaitForKodoWindow();
    CHECK(window != nullptr);

    // A second launch exits at once and leaves the first one running.
    PROCESS_INFORMATION second = StartKodo();
    CHECK(second.hProcess != nullptr);
    CHECK(WaitForSingleObject(second.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD secondExit = 1;
    GetExitCodeProcess(second.hProcess, &secondExit);
    CloseHandle(second.hProcess);
    CHECK(secondExit == 0);
    CHECK(WaitForSingleObject(first.hProcess, 0) == WAIT_TIMEOUT);
    if (holdingShortcut) UnregisterHotKey(nullptr, 0x6B3);

    // WM_CLOSE is what the installer sends; Kodo must exit cleanly.
    PostMessageW(window, WM_CLOSE, 0, 0);
    CHECK(WaitForSingleObject(first.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD firstExit = 1;
    GetExitCodeProcess(first.hProcess, &firstExit);
    CloseHandle(first.hProcess);
    CHECK(firstExit == 0);
    CHECK(FindKodoWindow() == nullptr);
}

// --- Runner -----------------------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // Capture coordinates are physical pixels, as in Kodo.exe.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    buildDir = argc > 1 ? fs::path(argv[1]) : fs::path(argv[0]).parent_path();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& test : Tests()) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << "\n";
        } catch (const Skip& skip) {
            ++skipped;
            std::cout << "SKIP " << test.name << ": " << skip.reason << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << test.name << ": exception: " << e.what() << "\n";
        }
    }
    std::error_code error;
    fs::remove_all(TemporaryFile(L"").parent_path(), error);
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    CoUninitialize();
    return failed == 0 ? 0 : 1;
}
