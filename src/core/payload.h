#pragma once

#include <string>
#include <string_view>

namespace kodo {

// What a QR code holds, recognized from the common payload formats that
// phone cameras understand.
enum class PayloadKind { Text, Link, Wifi, Email, Phone, Sms, Contact, Location };

struct WifiNetwork {
    std::wstring ssid;
    std::wstring password;
    std::wstring security;  // "WPA", "WEP" or empty for an open network
    bool hidden = false;
};

struct EmailMessage {
    std::wstring to;
    std::wstring subject;
    std::wstring body;
};

struct Contact {
    std::wstring name;
    std::wstring phone;
    std::wstring email;
    std::wstring organization;
    std::wstring website;
};

struct Payload {
    PayloadKind kind = PayloadKind::Text;
    std::wstring raw;
    WifiNetwork wifi;
    EmailMessage email;
    Contact contact;
    std::wstring number;    // Phone and Sms
    std::wstring message;   // Sms
    std::wstring location;  // "latitude, longitude"
};

Payload ParsePayload(std::wstring_view raw);

// Only http and https links are offered to open: other schemes in a QR code
// could start arbitrary programs or files.
bool IsWebLink(std::wstring_view text);

// Builders for the Create QR templates.
std::wstring BuildWifi(const WifiNetwork& network);
std::wstring BuildEmail(const EmailMessage& message);
std::wstring BuildContact(const Contact& contact);  // vCard 3.0

// "Link", "Wi-Fi network"... for the result popup and the history.
std::wstring KindName(PayloadKind kind);
// Readable multi-line summary, e.g. "Network: Home\nPassword: secret".
std::wstring Describe(const Payload& payload);

std::wstring PercentEncode(std::wstring_view text);
std::wstring PercentDecode(std::wstring_view text);

}  // namespace kodo
