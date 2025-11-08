#ifndef UTIL_H
#define UTIL_H

#include <Arduino.h>

class Utils {
public:
    static String getTimestamp();
    static String formatUptime(unsigned long uptime);
    static String formatBytes(size_t bytes);
    static String macToString(const uint8_t* mac);
    static void printHeapInfo();
    static bool isValidIP(const String& ip);
    static String urlEncode(const String& str);
    static String generateRandomString(uint8_t length);

private:
    static const char* getResetReason(uint8_t reason);
};

#endif // UTIL_H