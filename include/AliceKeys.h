#ifndef ALICE_KEYS_H
#define ALICE_KEYS_H

// -----------------------------------------------------------------------------
// AliceKeys.h
// -----------------------------------------------------------------------------
// Canonical, zero-dependency loader for secrets stored in `API_KEYS.txt`.
//
// Format of API_KEYS.txt (one key per line, `#` starts a comment):
//     CESIUM_ION_TOKEN=eyJhbGciOi...<your token>
//     GOOGLE_MAP_TILES_API_KEY=...
//
// `API_KEYS.txt` is .gitignored. Never commit it.
//
// Usage:
//     char token[2048];
//     if (!Alice::Keys::Get("CESIUM_ION_TOKEN", token, sizeof(token))) {
//         fprintf(stderr, "[KEYS] missing CESIUM_ION_TOKEN in API_KEYS.txt\n");
//         return 1;
//     }
// -----------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>

namespace Alice
{
    struct Keys
    {
        // Resolve likely locations for API_KEYS.txt relative to the CWD.
        // The build directory is typically `./build/`, so we also probe parents.
        static bool OpenKeysFile(std::ifstream& file)
        {
            const char* paths[] =
            {
                "API_KEYS.txt",
                "../API_KEYS.txt",
                "../../API_KEYS.txt",
                "../../../API_KEYS.txt"
            };
            for (const char* path : paths)
            {
                file.open(path);
                if (file.is_open()) return true;
            }
            return false;
        }

        // Read the value of `keyName` from API_KEYS.txt into outBuffer.
        // Returns true on success, false if the key is missing or the buffer is
        // too small. Trailing whitespace / CR on Windows is stripped.
        static bool Get(const char* keyName, char* outBuffer, size_t bufferSize)
        {
            if (!keyName || !outBuffer || bufferSize == 0) return false;
            outBuffer[0] = '\0';

            std::ifstream file;
            if (!OpenKeysFile(file)) return false;

            std::string prefix(keyName);
            prefix.push_back('=');

            std::string line;
            while (std::getline(file, line))
            {
                // Skip comments and blanks.
                size_t firstNonSpace = line.find_first_not_of(" \t");
                if (firstNonSpace == std::string::npos) continue;
                if (line[firstNonSpace] == '#') continue;

                if (line.compare(0, prefix.size(), prefix) != 0) continue;

                std::string value = line.substr(prefix.size());
                size_t end = value.find_last_not_of(" \r\n\t");
                if (end != std::string::npos) value = value.substr(0, end + 1);

                if (value.empty()) return false;
                if (value.size() >= bufferSize) return false;

                std::strncpy(outBuffer, value.c_str(), bufferSize);
                outBuffer[bufferSize - 1] = '\0';
                return true;
            }
            return false;
        }

        // Convenience: return the Cesium Ion token or an empty string on failure.
        // Caller should check the empty case and bail.
        static bool GetCesiumToken(char* outBuffer, size_t bufferSize)
        {
            return Get("CESIUM_ION_TOKEN", outBuffer, bufferSize);
        }
    };
}

#endif // ALICE_KEYS_H
