#include "core/payload.h"

#include "core/text.h"

#include <cwctype>
#include <vector>

namespace kodo {

namespace {

std::wstring Upper(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(std::towupper(c));
    return text;
}

// "K:value;" fields of WIFI:, MATMSG: and MECARD:, where a backslash escapes
// the next character. Returns (key, value) pairs in order.
std::vector<std::pair<std::wstring, std::wstring>> SplitFields(std::wstring_view body) {
    std::vector<std::pair<std::wstring, std::wstring>> fields;
    std::wstring key, value;
    bool inValue = false;
    for (size_t i = 0; i < body.size(); ++i) {
        const wchar_t c = body[i];
        if (inValue) {
            if (c == L'\\' && i + 1 < body.size()) {
                value += body[++i];
            } else if (c == L';') {
                fields.emplace_back(Upper(key), value);
                key.clear();
                value.clear();
                inValue = false;
            } else {
                value += c;
            }
        } else if (c == L':') {
            inValue = true;
        } else if (c != L';') {
            key += c;
        }
    }
    if (inValue) fields.emplace_back(Upper(key), value);
    return fields;
}

std::wstring EscapeField(std::wstring_view text) {
    std::wstring result;
    for (wchar_t c : text) {
        if (c == L'\\' || c == L';' || c == L',' || c == L':' || c == L'"') result += L'\\';
        result += c;
    }
    return result;
}

std::wstring EscapeVCard(std::wstring_view text) {
    std::wstring result;
    for (wchar_t c : ToUnixLineBreaks(text)) {
        if (c == L'\n') {
            result += L"\\n";
            continue;
        }
        if (c == L'\\' || c == L';' || c == L',') result += L'\\';
        result += c;
    }
    return result;
}

std::wstring UnescapeVCard(std::wstring_view text) {
    std::wstring result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) {
            const wchar_t next = text[++i];
            result += (next == L'n' || next == L'N') ? L'\n' : next;
        } else {
            result += text[i];
        }
    }
    return result;
}

// Query parameters of mailto: and sms: URIs.
std::wstring QueryValue(std::wstring_view query, std::wstring_view name) {
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find(L'&', start);
        if (end == std::wstring_view::npos) end = query.size();
        const std::wstring_view pair = query.substr(start, end - start);
        const size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos && Upper(std::wstring(pair.substr(0, equals))) == Upper(std::wstring(name))) {
            return PercentDecode(pair.substr(equals + 1));
        }
        start = end + 1;
    }
    return {};
}

void ParseVCard(std::wstring_view raw, Contact& contact) {
    // Unfold continuation lines (starting with a space or tab) first.
    std::vector<std::wstring> lines;
    for (const wchar_t c : ToUnixLineBreaks(raw) + L"\n") {
        if (lines.empty()) lines.emplace_back();
        if (c == L'\n') {
            lines.emplace_back();
        } else if ((c == L' ' || c == L'\t') && lines.back().empty() && lines.size() > 1) {
            lines.pop_back();  // Continue the previous line.
        } else {
            lines.back() += c;
        }
    }
    std::wstring structuredName;
    for (const auto& line : lines) {
        const size_t colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring property = Upper(line.substr(0, line.find_first_of(L";:")));
        if (const size_t dot = property.find(L'.'); dot != std::wstring::npos) property = property.substr(dot + 1);  // item1.TEL
        const std::wstring value = line.substr(colon + 1);
        if (property == L"FN" && contact.name.empty()) contact.name = UnescapeVCard(value);
        else if (property == L"N" && structuredName.empty()) structuredName = value;
        else if (property == L"TEL" && contact.phone.empty()) contact.phone = UnescapeVCard(value);
        else if (property == L"EMAIL" && contact.email.empty()) contact.email = UnescapeVCard(value);
        else if (property == L"ORG" && contact.organization.empty()) contact.organization = UnescapeVCard(value.substr(0, value.find(L';')));
        else if (property == L"URL" && contact.website.empty()) contact.website = UnescapeVCard(value);
    }
    if (contact.name.empty() && !structuredName.empty()) {
        // N:Family;Given;Additional;Prefix;Suffix
        const size_t semicolon = structuredName.find(L';');
        const std::wstring family = UnescapeVCard(structuredName.substr(0, semicolon));
        const std::wstring given = semicolon == std::wstring::npos ? L"" : UnescapeVCard(structuredName.substr(semicolon + 1, structuredName.find(L';', semicolon + 1) - semicolon - 1));
        contact.name = Trim(given + L" " + family);
    }
}

void ParseMeCard(std::wstring_view body, Contact& contact) {
    for (const auto& [key, value] : SplitFields(body)) {
        if (key == L"N" && contact.name.empty()) {
            // "Family,Given"
            const size_t comma = value.find(L',');
            contact.name = comma == std::wstring::npos ? value : Trim(value.substr(comma + 1) + L" " + value.substr(0, comma));
        } else if (key == L"TEL" && contact.phone.empty()) contact.phone = value;
        else if (key == L"EMAIL" && contact.email.empty()) contact.email = value;
        else if (key == L"ORG" && contact.organization.empty()) contact.organization = value;
        else if (key == L"URL" && contact.website.empty()) contact.website = value;
    }
}

int HexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

}  // namespace

bool IsWebLink(std::wstring_view text) {
    const std::wstring trimmed = Trim(text);
    const bool scheme = StartsWithNoCase(trimmed, L"http://") || StartsWithNoCase(trimmed, L"https://");
    if (!scheme || trimmed.size() <= 8) return false;
    for (wchar_t c : trimmed) {
        if (std::iswspace(c) || std::iswcntrl(c)) return false;
    }
    return true;
}

Payload ParsePayload(std::wstring_view raw) {
    Payload payload;
    payload.raw = std::wstring(raw);
    const std::wstring text = Trim(raw);

    if (IsWebLink(text)) {
        payload.kind = PayloadKind::Link;
    } else if (StartsWithNoCase(text, L"WIFI:")) {
        payload.kind = PayloadKind::Wifi;
        for (const auto& [key, value] : SplitFields(std::wstring_view(text).substr(5))) {
            if (key == L"S") payload.wifi.ssid = value;
            else if (key == L"P") payload.wifi.password = value;
            else if (key == L"T") payload.wifi.security = Upper(value) == L"NOPASS" ? L"" : Upper(value);
            else if (key == L"H") payload.wifi.hidden = Upper(value) == L"TRUE";
        }
    } else if (StartsWithNoCase(text, L"mailto:")) {
        payload.kind = PayloadKind::Email;
        const std::wstring_view rest = std::wstring_view(text).substr(7);
        const size_t question = rest.find(L'?');
        payload.email.to = PercentDecode(rest.substr(0, question));
        if (question != std::wstring_view::npos) {
            const std::wstring_view query = rest.substr(question + 1);
            payload.email.subject = QueryValue(query, L"subject");
            payload.email.body = ToUnixLineBreaks(QueryValue(query, L"body"));
        }
    } else if (StartsWithNoCase(text, L"MATMSG:")) {
        payload.kind = PayloadKind::Email;
        for (const auto& [key, value] : SplitFields(std::wstring_view(text).substr(7))) {
            if (key == L"TO") payload.email.to = value;
            else if (key == L"SUB") payload.email.subject = value;
            else if (key == L"BODY") payload.email.body = value;
        }
    } else if (StartsWithNoCase(text, L"tel:")) {
        payload.kind = PayloadKind::Phone;
        payload.number = PercentDecode(std::wstring_view(text).substr(4));
    } else if (StartsWithNoCase(text, L"SMSTO:")) {
        payload.kind = PayloadKind::Sms;
        const std::wstring rest = text.substr(6);
        const size_t colon = rest.find(L':');
        payload.number = rest.substr(0, colon);
        if (colon != std::wstring::npos) payload.message = rest.substr(colon + 1);
    } else if (StartsWithNoCase(text, L"sms:")) {
        payload.kind = PayloadKind::Sms;
        const std::wstring rest = text.substr(4);
        const size_t question = rest.find(L'?');
        payload.number = PercentDecode(std::wstring_view(rest).substr(0, question));
        if (question != std::wstring::npos) payload.message = QueryValue(std::wstring_view(rest).substr(question + 1), L"body");
    } else if (StartsWithNoCase(text, L"BEGIN:VCARD")) {
        payload.kind = PayloadKind::Contact;
        ParseVCard(text, payload.contact);
    } else if (StartsWithNoCase(text, L"MECARD:")) {
        payload.kind = PayloadKind::Contact;
        ParseMeCard(std::wstring_view(text).substr(7), payload.contact);
    } else if (StartsWithNoCase(text, L"geo:")) {
        payload.kind = PayloadKind::Location;
        std::wstring coordinates = text.substr(4, text.find_first_of(L";?") - 4);
        if (const size_t comma = coordinates.find(L','); comma != std::wstring::npos) {
            const size_t second = coordinates.find(L',', comma + 1);  // Optional altitude
            coordinates = coordinates.substr(0, comma) + L", " + coordinates.substr(comma + 1, second - comma - 1);
        }
        payload.location = coordinates;
    }
    return payload;
}

std::wstring BuildWifi(const WifiNetwork& network) {
    const bool open = network.security.empty();
    std::wstring text = L"WIFI:T:" + (open ? std::wstring(L"nopass") : network.security) + L";S:" + EscapeField(network.ssid) + L";";
    if (!open) text += L"P:" + EscapeField(network.password) + L";";
    if (network.hidden) text += L"H:true;";
    return text + L";";
}

std::wstring BuildEmail(const EmailMessage& message) {
    std::wstring text = L"mailto:" + PercentEncode(Trim(message.to));
    std::wstring query;
    if (!message.subject.empty()) query += L"subject=" + PercentEncode(message.subject);
    if (!message.body.empty()) query += (query.empty() ? L"" : L"&") + std::wstring(L"body=") + PercentEncode(ToWindowsLineBreaks(message.body));
    return query.empty() ? text : text + L"?" + query;
}

std::wstring BuildContact(const Contact& contact) {
    std::wstring text = L"BEGIN:VCARD\nVERSION:3.0\n";
    const std::wstring name = Trim(contact.name);
    // N is required by vCard 3.0; the whole name goes in the given name, which
    // every phone shows correctly whatever the naming order.
    text += L"N:;" + EscapeVCard(name) + L";;;\nFN:" + EscapeVCard(name) + L"\n";
    if (HasVisibleText(contact.organization)) text += L"ORG:" + EscapeVCard(Trim(contact.organization)) + L"\n";
    if (HasVisibleText(contact.phone)) text += L"TEL:" + EscapeVCard(Trim(contact.phone)) + L"\n";
    if (HasVisibleText(contact.email)) text += L"EMAIL:" + EscapeVCard(Trim(contact.email)) + L"\n";
    if (HasVisibleText(contact.website)) text += L"URL:" + EscapeVCard(Trim(contact.website)) + L"\n";
    return text + L"END:VCARD";
}

std::wstring KindName(PayloadKind kind) {
    switch (kind) {
        case PayloadKind::Link: return L"Link";
        case PayloadKind::Wifi: return L"Wi-Fi network";
        case PayloadKind::Email: return L"Email";
        case PayloadKind::Phone: return L"Phone number";
        case PayloadKind::Sms: return L"Text message";
        case PayloadKind::Contact: return L"Contact";
        case PayloadKind::Location: return L"Location";
        case PayloadKind::Text: break;
    }
    return L"Text";
}

std::wstring Describe(const Payload& payload) {
    std::wstring text;
    const auto line = [&](const wchar_t* label, const std::wstring& value) {
        if (value.empty()) return;
        if (!text.empty()) text += L"\n";
        text += std::wstring(label) + L": " + value;
    };
    switch (payload.kind) {
        case PayloadKind::Wifi:
            line(L"Network", payload.wifi.ssid);
            line(L"Password", payload.wifi.password);
            line(L"Security", payload.wifi.security.empty() ? L"None (open network)" : payload.wifi.security);
            if (payload.wifi.hidden) line(L"Hidden", L"Yes");
            return text;
        case PayloadKind::Email:
            line(L"To", payload.email.to);
            line(L"Subject", payload.email.subject);
            if (!payload.email.body.empty()) text += (text.empty() ? L"" : L"\n\n") + payload.email.body;
            return text;
        case PayloadKind::Phone:
            return payload.number;
        case PayloadKind::Sms:
            line(L"To", payload.number);
            line(L"Message", payload.message);
            return text;
        case PayloadKind::Contact:
            line(L"Name", payload.contact.name);
            line(L"Organization", payload.contact.organization);
            line(L"Phone", payload.contact.phone);
            line(L"Email", payload.contact.email);
            line(L"Website", payload.contact.website);
            return text.empty() ? payload.raw : text;
        case PayloadKind::Location:
            return payload.location;
        case PayloadKind::Link:
        case PayloadKind::Text:
            break;
    }
    return payload.raw;
}

std::wstring PercentEncode(std::wstring_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::wstring result;
    for (const unsigned char byte : ToUtf8(text)) {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~' || byte == '@') {
            result += static_cast<wchar_t>(byte);
        } else {
            result += L'%';
            result += static_cast<wchar_t>(kHex[byte >> 4]);
            result += static_cast<wchar_t>(kHex[byte & 15]);
        }
    }
    return result;
}

std::wstring PercentDecode(std::wstring_view text) {
    // Escapes are UTF-8 bytes; plain runs are converted whole so surrogate
    // pairs stay intact.
    std::string bytes;
    size_t plainStart = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0) {
            bytes += ToUtf8(text.substr(plainStart, i - plainStart));
            bytes += static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2]));
            i += 2;
            plainStart = i + 1;
        }
    }
    bytes += ToUtf8(text.substr(plainStart));
    return FromUtf8(bytes);
}

}  // namespace kodo
