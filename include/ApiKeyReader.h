#ifndef ALICE_API_KEY_READER_H
#define ALICE_API_KEY_READER_H

#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>

namespace Alice
{
    struct ApiKeyReader
    {
        static bool GetGoogleKey(char* outBuffer, size_t bufferSize)
        {
            const char* paths[] = { "API_KEYS.txt", "../../API_KEYS.txt", "../API_KEYS.txt" };
            std::ifstream file;
            for (const char* path : paths)
            {
                file.open(path);
                if (file.is_open()) break;
            }

            if (!file.is_open())
            {
                return false;
            }

            std::string line;
            const char* targets[] = { "GOOGLE_MAP_TILES_API_KEY=", "GOOGLE_API_KEY=" };

            while (std::getline(file, line))
            {
                for (const char* targetKey : targets)
                {
                    size_t targetLen = strlen(targetKey);
                    if (line.compare(0, targetLen, targetKey) == 0)
                    {
                        std::string value = line.substr(targetLen);
                        // Trim potential whitespace or carriage returns from Windows
                        size_t end = value.find_last_not_of(" \r\n\t");
                        if (std::string::npos != end)
                        {
                            value = value.substr(0, end + 1);
                        }

                        if (value.length() < bufferSize)
                        {
                            strncpy(outBuffer, value.c_str(), bufferSize);
                            outBuffer[bufferSize - 1] = '\0';
                            return true;
                        }
                    }
                }
            }

            return false;
        }

        static bool GetCesiumToken(char* outBuffer, size_t bufferSize)
        {
            if (const char* envToken = std::getenv("CESIUM_ION_TOKEN"))
            {
                if (strlen(envToken) < bufferSize)
                {
                    strncpy(outBuffer, envToken, bufferSize);
                    outBuffer[bufferSize - 1] = '\0';
                    return true;
                }
            }

            const char* paths[] = { "API_KEYS.txt", "../../API_KEYS.txt", "../API_KEYS.txt" };
            std::ifstream file;
            for (const char* path : paths)
            {
                file.open(path);
                if (file.is_open()) break;
            }

            if (!file.is_open()) return false;

            std::string line;
            bool foundKey = false;

            while (std::getline(file, line))
            {
                if (line.find("CESIUM_ION_KEYS") != std::string::npos)
                {
                    foundKey = true;
                    // If the token is on the same line (after the '=')
                    size_t eqPos = line.find('=');
                    if (eqPos != std::string::npos && eqPos + 1 < line.length()) {
                        std::string val = line.substr(eqPos + 1);
                        size_t firstQuote = val.find('"');
                        if (firstQuote != std::string::npos) {
                            size_t secondQuote = val.find('"', firstQuote + 1);
                            if (secondQuote != std::string::npos) {
                                std::string token = val.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                                if (token.length() > 10 && token.length() < bufferSize) {
                                    strncpy(outBuffer, token.c_str(), bufferSize);
                                    outBuffer[bufferSize - 1] = '\0';
                                    return true;
                                }
                            }
                        }
                    }
                }
                else if (foundKey)
                {
                    // The token is on the next line
                    size_t firstQuote = line.find('"');
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find('"', firstQuote + 1);
                        if (secondQuote != std::string::npos) {
                            std::string token = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                            if (token.length() > 10 && token.length() < bufferSize) {
                                strncpy(outBuffer, token.c_str(), bufferSize);
                                outBuffer[bufferSize - 1] = '\0';
                                return true;
                            }
                        }
                    }
                }
            }

            return false;
        }
    };
}

#endif // ALICE_API_KEY_READER_H
