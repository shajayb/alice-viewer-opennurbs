#ifndef ALICE_API_KEY_READER_H
#define ALICE_API_KEY_READER_H

#include <fstream>
#include <string>
#include <cstring>

namespace Alice
{
    struct ApiKeyReader
    {
        static bool GetGoogleKey(char* outBuffer, size_t bufferSize)
        {
            std::ifstream file("API_KEYS.txt");
            if (!file.is_open())
            {
                return false;
            }

            std::string line;
            const char* targetKey = "GOOGLE_API_KEY=";
            size_t targetLen = strlen(targetKey);

            while (std::getline(file, line))
            {
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

            return false;
        }
    };
}

#endif // ALICE_API_KEY_READER_H
