#include "util.h"

String Utils::getTimestamp() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buffer);
}

String Utils::formatUptime(unsigned long uptime) {
    unsigned long days = uptime / (1000 * 60 * 60 * 24);
    unsigned long hours = (uptime % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60);
    unsigned long minutes = (uptime % (1000 * 60 * 60)) / (1000 * 60);
    unsigned long seconds = (uptime % (1000 * 60)) / 1000;

    String result = "";
    if (days > 0) {
        result += String(days) + "d ";
    }
    if (hours > 0 || days > 0) {
        result += String(hours) + "h ";
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        result += String(minutes) + "m ";
    }
    result += String(seconds) + "s";

    return result;
}

String Utils::formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0, 2) + " KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes / (1024.0 * 1024.0), 2) + " MB";
    } else {
        return String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    }
}

String Utils::macToString(const uint8_t* mac) {
    char buf[18];
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void Utils::printHeapInfo() {
    Serial.println("=== Heap Information ===");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Heap fragmentation: %d%%\n", 100 - (ESP.getMaxAllocHeap() * 100 / ESP.getFreeHeap()));
    Serial.printf("Largest free block: %d bytes\n", ESP.getMaxAllocHeap());
    Serial.println("========================");
}

bool Utils::isValidIP(const String& ip) {
    IPAddress testIP;
    return testIP.fromString(ip);
}

String Utils::urlEncode(const String& str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;

    for (int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encodedString += '+';
        } else if (isalnum(c)) {
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

String Utils::generateRandomString(uint8_t length) {
    String randomString = "";
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    for (uint8_t i = 0; i < length; i++) {
        randomString += chars[random(0, strlen(chars))];
    }

    return randomString;
}

const char* Utils::getResetReason(uint8_t reason) {
    switch (reason) {
        case 1: return "POWERON_RESET";
        case 2: return "EXTERNAL_RESET";
        case 3: return "BROWNOUT_RESET";
        case 4: return "SDIO_RESET";
        case 5: return "DEEPSLEEP_RESET";
        case 6: return "SW_RESET";
        case 7: return "PANIC_RESET";
        case 8: return "INT_WDT_RESET";
        case 9: return "TASK_WDT_RESET";
        case 10: return "OTHER_WDT_RESET";
        case 11: return "SYS_WDT_RESET";
        case 12: return "CPU0_WDT_RESET";
        case 13: return "CPU1_WDT_RESET";
        default: return "UNKNOWN_RESET";
    }
}