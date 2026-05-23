#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <esp_system.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

namespace {

constexpr const char* kTag = "CKPV";
constexpr const char* kVersion = "0.1.0";
constexpr uint8_t kMaxEntries = 24;
constexpr size_t kAccountLen = 32;
constexpr size_t kUsernameLen = 48;
constexpr size_t kPasswordLen = 72;
constexpr size_t kPlainMax = 4096;
constexpr uint8_t kSaltLen = 16;
constexpr uint8_t kNonceLen = 12;
constexpr uint8_t kTagLen = 16;
constexpr uint8_t kKeyLen = 32;
constexpr uint8_t kMagicLen = 8;
constexpr uint16_t kKdfRounds = 2048;
constexpr uint32_t kArmTimeoutMs = 5000;

constexpr int kEncoderA = 4;
constexpr int kEncoderB = 5;
constexpr int kEncoderButton = 0;
constexpr int kPowerEnable = 15;
constexpr int kBacklight = 21;

const uint8_t kMagic[kMagicLen] = {'C', 'K', 'T', 'P', 'V', '1', 0, 0};

struct VaultEntry {
    char account[kAccountLen];
    char username[kUsernameLen];
    char password[kPasswordLen];
};

TFT_eSPI tft;
USBHIDKeyboard Keyboard;
Preferences prefs;

VaultEntry entries[kMaxEntries];
uint8_t entryCount = 0;
uint8_t selected = 0;
bool unlocked = false;
bool hasVault = false;
bool keyReady = false;
bool typeArmed = false;
uint32_t armedAt = 0;
uint8_t vaultKey[kKeyLen];
uint8_t vaultSalt[kSaltLen];
String serialLine;
int lastA = HIGH;
int lastButton = HIGH;
uint32_t lastButtonChange = 0;

void secureZero(void* ptr, size_t len) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while(len--) *p++ = 0;
}

void safeCopy(char* dst, size_t len, const String& src) {
    if(len == 0) return;
    size_t n = min(len - 1, static_cast<size_t>(src.length()));
    memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

void sanitize(String& s) {
    s.trim();
    s.replace("\t", " ");
    s.replace("\r", " ");
    s.replace("\n", " ");
}

void printHelp() {
    Serial.println();
    Serial.println(F("CK42X T-Embed PassVault commands:"));
    Serial.println(F("  HELP"));
    Serial.println(F("  STATUS"));
    Serial.println(F("  PIN <pin>                    first-run setup"));
    Serial.println(F("  UNLOCK <pin>                 unlock vault"));
    Serial.println(F("  LOCK                         clear key from RAM"));
    Serial.println(F("  ADD account|username|pass"));
    Serial.println(F("  GEN account|username|preset  presets: memorable, strict, long, nosymbol"));
    Serial.println(F("  LIST"));
    Serial.println(F("  SHOW <index>"));
    Serial.println(F("  TYPE <index>                 HID type after countdown"));
    Serial.println(F("  DEL <index>"));
    Serial.println(F("  CLEAR confirm                wipe vault"));
    Serial.println();
}

void drawStatus(const String& line1, const String& line2 = "", const String& line3 = "") {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(8, 8);
    tft.print(F("CK42X PassVault"));
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 34);
    tft.print(line1);
    if(line2.length()) {
        tft.setCursor(8, 52);
        tft.print(line2);
    }
    if(line3.length()) {
        tft.setCursor(8, 70);
        tft.print(line3);
    }
    tft.setCursor(8, 148);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print(F("Serial: HELP | ck42x.com"));
}

void drawVault() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(8, 6);
    tft.printf("CK42X PassVault v%s", kVersion);

    if(!hasVault) {
        drawStatus(F("No vault yet."), F("Serial: PIN <pin>"), F("Then ADD or GEN entries."));
        return;
    }
    if(!unlocked) {
        drawStatus(F("Vault locked."), F("Serial: UNLOCK <pin>"), F("PIN never shows on screen."));
        return;
    }
    if(entryCount == 0) {
        drawStatus(F("Unlocked. Vault empty."), F("Serial: ADD or GEN"), F("Generated entries save encrypted."));
        return;
    }

    if(selected >= entryCount) selected = entryCount - 1;
    const VaultEntry& e = entries[selected];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(8, 28);
    tft.printf("Entry %u/%u", selected + 1, entryCount);
    tft.setCursor(8, 48);
    tft.print(F("Account: "));
    tft.print(e.account);
    tft.setCursor(8, 68);
    tft.print(F("User: "));
    tft.print(e.username);
    tft.setCursor(8, 88);
    tft.print(F("Password: "));
    for(size_t i = 0; i < strnlen(e.password, kPasswordLen); ++i) tft.print('*');

    tft.setCursor(8, 118);
    if(typeArmed) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.print(F("ARMED: press again to type"));
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print(F("Rotate select | press arm"));
    }
    tft.setCursor(8, 148);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print(F("HID types password only"));
}

void sha256Once(const uint8_t* data, size_t len, uint8_t out[32]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, out);
#else
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, data, len);
    mbedtls_sha256_finish_ret(&ctx, out);
#endif
    mbedtls_sha256_free(&ctx);
}

void deriveKey(const String& pin, const uint8_t salt[kSaltLen], uint8_t out[kKeyLen]) {
    static const char domain[] = "CK42X-T-Embed-PassVault-KDF-v1";
    uint8_t block[128];
    size_t domainLen = strlen(domain);
    size_t pinLen = min(static_cast<size_t>(pin.length()), static_cast<size_t>(48));
    memcpy(block, domain, domainLen);
    memcpy(block + domainLen, salt, kSaltLen);
    memcpy(block + domainLen + kSaltLen, pin.c_str(), pinLen);
    sha256Once(block, domainLen + kSaltLen + pinLen, out);

    uint8_t roundBuf[32 + kSaltLen + 4];
    for(uint16_t round = 1; round < kKdfRounds; ++round) {
        memcpy(roundBuf, out, 32);
        memcpy(roundBuf + 32, salt, kSaltLen);
        roundBuf[48] = round & 0xff;
        roundBuf[49] = (round >> 8) & 0xff;
        roundBuf[50] = 0;
        roundBuf[51] = 0;
        sha256Once(roundBuf, sizeof(roundBuf), out);
    }
    secureZero(block, sizeof(block));
    secureZero(roundBuf, sizeof(roundBuf));
}

String serializeEntries() {
    String out;
    out.reserve(entryCount * 96 + 8);
    for(uint8_t i = 0; i < entryCount; ++i) {
        out += entries[i].account;
        out += '\t';
        out += entries[i].username;
        out += '\t';
        out += entries[i].password;
        out += '\n';
    }
    return out;
}

void clearEntries() {
    secureZero(entries, sizeof(entries));
    entryCount = 0;
    selected = 0;
}

bool parseEntries(const uint8_t* data, size_t len) {
    clearEntries();
    size_t start = 0;
    while(start < len && entryCount < kMaxEntries) {
        size_t end = start;
        while(end < len && data[end] != '\n') ++end;
        if(end > start) {
            String line;
            line.reserve(end - start);
            for(size_t i = start; i < end; ++i) line += static_cast<char>(data[i]);
            int first = line.indexOf('\t');
            int second = line.indexOf('\t', first + 1);
            if(first > 0 && second > first) {
                safeCopy(entries[entryCount].account, kAccountLen, line.substring(0, first));
                safeCopy(entries[entryCount].username, kUsernameLen, line.substring(first + 1, second));
                safeCopy(entries[entryCount].password, kPasswordLen, line.substring(second + 1));
                entryCount++;
            }
        }
        start = end + 1;
    }
    if(selected >= entryCount) selected = entryCount ? entryCount - 1 : 0;
    return true;
}

void randomBytes(uint8_t* dst, size_t len) {
    for(size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t n = min(static_cast<size_t>(4), len - i);
        memcpy(dst + i, &r, n);
    }
}

bool saveVault() {
    if(!keyReady) return false;
    String plain = serializeEntries();
    if(plain.length() > kPlainMax) return false;

    uint8_t nonce[kNonceLen];
    uint8_t tag[kTagLen];
    randomBytes(nonce, sizeof(nonce));

    size_t plainLen = plain.length();
    size_t blobLen = kMagicLen + kSaltLen + kNonceLen + kTagLen + plainLen;
    uint8_t* blob = static_cast<uint8_t*>(malloc(blobLen));
    if(!blob) return false;

    memcpy(blob, kMagic, kMagicLen);
    memcpy(blob + kMagicLen, vaultSalt, kSaltLen);
    memcpy(blob + kMagicLen + kSaltLen, nonce, kNonceLen);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey, 256);
    if(rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm,
            MBEDTLS_GCM_ENCRYPT,
            plainLen,
            nonce,
            kNonceLen,
            nullptr,
            0,
            reinterpret_cast<const uint8_t*>(plain.c_str()),
            blob + kMagicLen + kSaltLen + kNonceLen + kTagLen,
            kTagLen,
            tag);
    }
    mbedtls_gcm_free(&gcm);
    if(rc == 0) {
        memcpy(blob + kMagicLen + kSaltLen + kNonceLen, tag, kTagLen);
        rc = prefs.putBytes("vault", blob, blobLen) == blobLen ? 0 : -1;
    }
    secureZero(blob, blobLen);
    free(blob);
    hasVault = (rc == 0);
    return rc == 0;
}

bool loadVaultWithPin(const String& pin) {
    size_t blobLen = prefs.getBytesLength("vault");
    if(blobLen < kMagicLen + kSaltLen + kNonceLen + kTagLen) return false;
    uint8_t* blob = static_cast<uint8_t*>(malloc(blobLen));
    if(!blob) return false;
    size_t got = prefs.getBytes("vault", blob, blobLen);
    if(got != blobLen || memcmp(blob, kMagic, kMagicLen) != 0) {
        free(blob);
        return false;
    }

    memcpy(vaultSalt, blob + kMagicLen, kSaltLen);
    deriveKey(pin, vaultSalt, vaultKey);
    keyReady = true;

    const uint8_t* nonce = blob + kMagicLen + kSaltLen;
    const uint8_t* tag = blob + kMagicLen + kSaltLen + kNonceLen;
    const uint8_t* cipher = blob + kMagicLen + kSaltLen + kNonceLen + kTagLen;
    size_t plainLen = blobLen - (kMagicLen + kSaltLen + kNonceLen + kTagLen);
    uint8_t* plain = static_cast<uint8_t*>(malloc(plainLen + 1));
    if(!plain) {
        free(blob);
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey, 256);
    if(rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm,
            plainLen,
            nonce,
            kNonceLen,
            nullptr,
            0,
            tag,
            kTagLen,
            cipher,
            plain);
    }
    mbedtls_gcm_free(&gcm);

    bool ok = false;
    if(rc == 0) {
        plain[plainLen] = 0;
        ok = parseEntries(plain, plainLen);
    }
    secureZero(plain, plainLen + 1);
    free(plain);
    secureZero(blob, blobLen);
    free(blob);
    if(!ok) {
        secureZero(vaultKey, sizeof(vaultKey));
        keyReady = false;
    }
    return ok;
}

bool createVault(const String& pin) {
    if(pin.length() < 4) return false;
    clearEntries();
    randomBytes(vaultSalt, sizeof(vaultSalt));
    deriveKey(pin, vaultSalt, vaultKey);
    keyReady = true;
    unlocked = true;
    return saveVault();
}

String generatedPassword(const String& preset) {
    const char* lower = "abcdefghijkmnopqrstuvwxyz";
    const char* upper = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    const char* digits = "23456789";
    const char* symbols = "!@#$%*?";
    const char* words[] = {"iron", "orbit", "crown", "field", "ember", "vault", "raven", "maple", "river", "signal", "wolf", "bee"};

    String p = preset;
    p.toLowerCase();
    if(p == "long") {
        String out;
        for(uint8_t i = 0; i < 4; ++i) {
            if(i) out += '-';
            out += words[esp_random() % (sizeof(words) / sizeof(words[0]))];
        }
        out += String(esp_random() % 90 + 10);
        return out;
    }

    uint8_t len = p == "memorable" ? 16 : 18;
    String alphabet = String(lower) + upper + digits;
    if(p != "nosymbol") alphabet += symbols;
    String out;
    out.reserve(len);
    out += upper[esp_random() % strlen(upper)];
    out += lower[esp_random() % strlen(lower)];
    out += digits[esp_random() % strlen(digits)];
    if(p != "nosymbol") out += symbols[esp_random() % strlen(symbols)];
    while(out.length() < len) out += alphabet[esp_random() % alphabet.length()];
    for(uint8_t i = 0; i < out.length(); ++i) {
        uint8_t j = esp_random() % out.length();
        char tmp = out[i];
        out.setCharAt(i, out[j]);
        out.setCharAt(j, tmp);
    }
    return out;
}

int findEntry(const String& account) {
    for(uint8_t i = 0; i < entryCount; ++i) {
        if(account.equalsIgnoreCase(entries[i].account)) return i;
    }
    return -1;
}

bool upsertEntry(const String& account, const String& username, const String& password) {
    if(!unlocked || !keyReady || !account.length() || !password.length()) return false;
    int idx = findEntry(account);
    if(idx < 0) {
        if(entryCount >= kMaxEntries) return false;
        idx = entryCount++;
    }
    safeCopy(entries[idx].account, kAccountLen, account);
    safeCopy(entries[idx].username, kUsernameLen, username);
    safeCopy(entries[idx].password, kPasswordLen, password);
    selected = idx;
    return saveVault();
}

void listEntries() {
    if(!unlocked) {
        Serial.println(F("ERR locked"));
        return;
    }
    Serial.printf("Entries: %u\n", entryCount);
    for(uint8_t i = 0; i < entryCount; ++i) {
        Serial.printf("%u | %s | %s | ", i, entries[i].account, entries[i].username);
        for(size_t j = 0; j < strnlen(entries[i].password, kPasswordLen); ++j) Serial.print('*');
        Serial.println();
    }
}

void typePassword(uint8_t idx) {
    if(!unlocked || idx >= entryCount) {
        Serial.println(F("ERR invalid or locked"));
        return;
    }
    drawStatus(F("Typing in 3 seconds"), entries[idx].account, F("Focus target field now."));
    delay(3000);
    Keyboard.print(entries[idx].password);
    Serial.printf("OK typed password for index %u (%s)\n", idx, entries[idx].account);
    typeArmed = false;
    drawVault();
}

void deleteEntry(uint8_t idx) {
    if(!unlocked || idx >= entryCount) {
        Serial.println(F("ERR invalid or locked"));
        return;
    }
    VaultEntry removed = entries[idx];
    for(uint8_t i = idx; i + 1 < entryCount; ++i) entries[i] = entries[i + 1];
    entryCount--;
    secureZero(&entries[entryCount], sizeof(VaultEntry));
    if(saveVault()) {
        Serial.println(F("OK deleted"));
        if(selected >= entryCount) selected = entryCount ? entryCount - 1 : 0;
    } else {
        if(entryCount < kMaxEntries) {
            for(int i = entryCount; i > idx; --i) entries[i] = entries[i - 1];
            entries[idx] = removed;
            entryCount++;
        }
        Serial.println(F("ERR save failed; delete rolled back"));
    }
    secureZero(&removed, sizeof(removed));
    drawVault();
}

void processCommand(String line) {
    line.trim();
    if(!line.length()) return;
    int space = line.indexOf(' ');
    String cmd = space < 0 ? line : line.substring(0, space);
    String arg = space < 0 ? "" : line.substring(space + 1);
    cmd.toUpperCase();
    arg.trim();

    if(cmd == "HELP" || cmd == "?") {
        printHelp();
    } else if(cmd == "STATUS") {
        Serial.printf("version=%s hasVault=%s unlocked=%s entries=%u\n", kVersion, hasVault ? "yes" : "no", unlocked ? "yes" : "no", entryCount);
    } else if(cmd == "PIN") {
        if(hasVault) {
            Serial.println(F("ERR vault already exists; use UNLOCK or CLEAR confirm"));
        } else if(createVault(arg)) {
            Serial.println(F("OK vault created and unlocked"));
        } else {
            Serial.println(F("ERR PIN setup failed; min 4 chars"));
        }
    } else if(cmd == "UNLOCK") {
        if(loadVaultWithPin(arg)) {
            unlocked = true;
            Serial.printf("OK unlocked entries=%u\n", entryCount);
        } else {
            Serial.println(F("ERR unlock failed"));
        }
    } else if(cmd == "LOCK") {
        unlocked = false;
        keyReady = false;
        typeArmed = false;
        clearEntries();
        secureZero(vaultKey, sizeof(vaultKey));
        Serial.println(F("OK locked"));
    } else if(cmd == "ADD") {
        int a = arg.indexOf('|');
        int b = arg.indexOf('|', a + 1);
        if(a < 1 || b < a + 1) {
            Serial.println(F("ERR use ADD account|username|password"));
        } else {
            String account = arg.substring(0, a);
            String username = arg.substring(a + 1, b);
            String password = arg.substring(b + 1);
            sanitize(account); sanitize(username); sanitize(password);
            Serial.println(upsertEntry(account, username, password) ? F("OK saved") : F("ERR save failed"));
        }
    } else if(cmd == "GEN") {
        int a = arg.indexOf('|');
        int b = arg.indexOf('|', a + 1);
        if(a < 1 || b < a + 1) {
            Serial.println(F("ERR use GEN account|username|preset"));
        } else {
            String account = arg.substring(0, a);
            String username = arg.substring(a + 1, b);
            String preset = arg.substring(b + 1);
            sanitize(account); sanitize(username); sanitize(preset);
            String pass = generatedPassword(preset);
            Serial.println(upsertEntry(account, username, pass) ? F("OK generated and saved") : F("ERR save failed"));
        }
    } else if(cmd == "LIST") {
        listEntries();
    } else if(cmd == "SHOW") {
        int idx = arg.toInt();
        if(unlocked && idx >= 0 && idx < entryCount) Serial.printf("%u password: %s\n", idx, entries[idx].password);
        else Serial.println(F("ERR invalid or locked"));
    } else if(cmd == "TYPE") {
        int idx = arg.toInt();
        if(idx >= 0 && idx < entryCount) typePassword(static_cast<uint8_t>(idx));
        else Serial.println(F("ERR invalid index"));
    } else if(cmd == "DEL") {
        int idx = arg.toInt();
        if(idx >= 0 && idx < entryCount) deleteEntry(static_cast<uint8_t>(idx));
        else Serial.println(F("ERR invalid index"));
    } else if(cmd == "CLEAR") {
        if(arg == "confirm") {
            prefs.remove("vault");
            unlocked = false;
            keyReady = false;
            hasVault = false;
            clearEntries();
            secureZero(vaultKey, sizeof(vaultKey));
            Serial.println(F("OK vault wiped"));
        } else {
            Serial.println(F("ERR use CLEAR confirm"));
        }
    } else {
        Serial.println(F("ERR unknown command; use HELP"));
    }
    drawVault();
}

void pollSerial() {
    while(Serial.available()) {
        char c = Serial.read();
        if(c == '\r') continue;
        if(c == '\n') {
            processCommand(serialLine);
            serialLine = "";
        } else if(serialLine.length() < 240) {
            serialLine += c;
        }
    }
}

void pollEncoder() {
    int a = digitalRead(kEncoderA);
    if(a != lastA && a == LOW && unlocked && entryCount > 0) {
        int b = digitalRead(kEncoderB);
        if(b != a) selected = (selected + 1) % entryCount;
        else selected = selected == 0 ? entryCount - 1 : selected - 1;
        typeArmed = false;
        drawVault();
    }
    lastA = a;

    int button = digitalRead(kEncoderButton);
    uint32_t now = millis();
    if(button != lastButton && now - lastButtonChange > 60) {
        lastButtonChange = now;
        if(lastButton == HIGH && button == LOW && unlocked && entryCount > 0) {
            if(typeArmed && now - armedAt < kArmTimeoutMs) {
                typePassword(selected);
            } else {
                typeArmed = true;
                armedAt = now;
                Serial.printf("ARMED index %u; press encoder again within 5 seconds to HID type\n", selected);
                drawVault();
            }
        }
        lastButton = button;
    }
    if(typeArmed && millis() - armedAt > kArmTimeoutMs) {
        typeArmed = false;
        drawVault();
    }
}

} // namespace

void setup() {
    pinMode(kPowerEnable, OUTPUT);
    digitalWrite(kPowerEnable, HIGH);
    pinMode(kBacklight, OUTPUT);
    digitalWrite(kBacklight, HIGH);
    pinMode(kEncoderA, INPUT_PULLUP);
    pinMode(kEncoderB, INPUT_PULLUP);
    pinMode(kEncoderButton, INPUT_PULLUP);
    lastA = digitalRead(kEncoderA);
    lastButton = digitalRead(kEncoderButton);

    Serial.begin(115200);
    Keyboard.begin();
    USB.begin();

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    prefs.begin("ck42x-pv", false);
    hasVault = prefs.isKey("vault") && prefs.getBytesLength("vault") > 0;

    delay(250);
    Serial.printf("CK42X T-Embed PassVault v%s\n", kVersion);
    Serial.println(F("Use HELP for commands."));
    drawVault();
}

void loop() {
    pollSerial();
    pollEncoder();
    delay(5);
}
