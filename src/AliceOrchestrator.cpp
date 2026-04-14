#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- UTILS ---
std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

std::string readApiKey(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open API_KEYS.txt. Ensure it exists in the CWD.");
    }
    std::string key;
    if (std::getline(file, key)) {
        return trim(key);
    }
    throw std::runtime_error("API_KEYS.txt is empty.");
}

std::string readFileContents(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// --- BASE64 ---
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

std::string base64_encode(const std::vector<unsigned char>& data) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (auto byte : data) {
        char_array_3[i++] = byte;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

// --- CURL WRAPPER (RAII) ---
class CurlHandle {
public:
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() { if (handle) curl_easy_cleanup(handle); }
    CURL* get() { return handle; }
private:
    CURL* handle;
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// --- STATE MACHINE ---
enum class OrchestratorState {
    EXECUTE,
    CAPTURE,
    REVIEW,
    RESOLUTION
};

int main(int argc, char** argv) 
{
    std::string apiKey;
    try {
        apiKey = readApiKey("API_KEYS.txt");
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::cout << "[ORCHESTRATOR] Initializing AliceOrchestrator..." << std::endl;

    OrchestratorState state = OrchestratorState::EXECUTE;
    bool running = true;
    json lastReport;
    std::string encodedImage;
    int iterationCount = 0;

    // Default prompt file
    std::string promptFile = "prompt.txt";
    if (argc > 1) {
        if (fs::exists(argv[1]) && fs::is_regular_file(argv[1])) {
            promptFile = argv[1];
        }
    }
    
    std::string EXECUTOR_CMD = "gemini --yolo " + promptFile;

    while (running) 
    {
        switch (state) 
        {
            case OrchestratorState::EXECUTE:
            {
                iterationCount++;
                if (iterationCount > 10) {
                    std::cerr << "[ERROR] Max iterations (10) reached. Hard fail." << std::endl;
                    running = false;
                    break;
                }

                std::cout << "[STATE: EXECUTE] (Iteration: " << iterationCount << ") Triggering Executor via: " << EXECUTOR_CMD << std::endl;
                std::system(EXECUTOR_CMD.c_str());
                
                // Read report from file-based handoff
                std::ifstream reportFile("executor_report.json");
                if (reportFile.is_open()) {
                    try {
                        reportFile >> lastReport;
                    } catch (const std::exception& e) {
                        std::cerr << "[ERROR] Failed to parse executor_report.json: " << e.what() << std::endl;
                        running = false;
                        break;
                    }
                    reportFile.close();
                } else {
                    std::cerr << "[ERROR] executor_report.json not found after execution." << std::endl;
                    running = false;
                    break;
                }

                if (lastReport.empty()) {
                    std::cerr << "[ERROR] Failed to parse report from Executor." << std::endl;
                    running = false;
                    break;
                }
                state = OrchestratorState::CAPTURE;
                break;
            }

            case OrchestratorState::CAPTURE:
            {
                std::cout << "[STATE: CAPTURE] Initiating Headless Capture..." << std::endl;
                // Capture image regardless of build status for debugging if possible, 
                // but usually only works if SUCCESS.
                if (lastReport["build_status"] == "SUCCESS") {
                    std::system("build\\bin\\AliceViewer.exe --headless-capture");

                    std::ifstream file("framebuffer.png", std::ios::binary);
                    if (file) {
                        std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
                        encodedImage = base64_encode(buffer);
                        std::cout << "[ORCHESTRATOR] Image captured and encoded. Size: " << encodedImage.size() << " bytes." << std::endl;
                    } else {
                        std::cerr << "[ERROR] framebuffer.png not found after capture attempt." << std::endl;
                        encodedImage = ""; // Reset
                    }
                } else {
                    encodedImage = ""; // No image for failed builds
                }
                state = OrchestratorState::REVIEW;
                break;
            }

            case OrchestratorState::REVIEW:
            {
                std::cout << "[STATE: REVIEW] Preparing Multi-Agent Reflection Payload..." << std::endl;
                
                // Gather Code
                std::string modifiedCode;
                if (lastReport.contains("files_modified") && lastReport["files_modified"].is_array()) {
                    for (auto& f : lastReport["files_modified"]) {
                        std::string path = f.get<std::string>();
                        if (path.find(".h") != std::string::npos || path.find(".cpp") != std::string::npos) {
                            modifiedCode += "--- FILE: " + path + " ---\n";
                            modifiedCode += readFileContents(path) + "\n\n";
                        }
                    }
                }

                // Gather Logs
                std::string logs = readFileContents("executor_console.log");
                
                // Gather Original Brief
                std::string originalBrief = readFileContents(promptFile);

                // Construct Prompt
                std::string reviewerPrompt = 
                    "SYSTEM PROMPT: You are the Senior Architect Reviewer. Evaluate the provided data against the Original Brief.\n"
                    "ORIGINAL BRIEF: " + originalBrief + "\n"
                    "EXECUTOR CLAIMS: " + lastReport.dump(2) + "\n"
                    "MODIFIED CODE: \n" + modifiedCode + "\n"
                    "EXECUTOR LOGS: \n" + logs + "\n"
                    "TASK: Review the visual framebuffer.png, the C++ code, and the claims. \n"
                    "If the goals and visually meaningful results are MET, output ONLY: <STATUS: PASSED_PHASE_3>.\n"
                    "If NOT MET, devise a NEW precise, step-by-step prompt to send back to the Executor CLI to fix the code. Start your response with '[CLI AGENT PROMPT]'.";

                CurlHandle curl;
                if (curl.get()) {
                    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-pro:generateContent?key=" + apiKey;
                    
                    json payload = {
                        {"contents", {{
                            {"parts", {
                                {{"text", reviewerPrompt}}
                            }}
                        }}}
                    };

                    // Add image if available
                    if (!encodedImage.empty()) {
                        payload["contents"][0]["parts"].push_back({
                            {"inline_data", {
                                {"mime_type", "image/png"},
                                {"data", encodedImage}
                            }}
                        });
                    }

                    std::string jsonStr = payload.dump();
                    std::string responseBuffer;

                    struct curl_slist* headers = nullptr;
                    headers = curl_slist_append(headers, "Content-Type: application/json");

                    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
                    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, jsonStr.c_str());
                    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
                    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
                    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBuffer);

                    CURLcode res = curl_easy_perform(curl.get());
                    if (res != CURLE_OK) {
                        std::cerr << "[CURL ERROR] " << curl_easy_strerror(res) << std::endl;
                        running = false;
                    } else {
                        // Parse response to find text
                        try {
                            json respJson = json::parse(responseBuffer);
                            if (respJson.contains("candidates") && !respJson["candidates"].empty()) {
                                std::string text = respJson["candidates"][0]["content"]["parts"][0]["text"];
                                std::cout << "[GEMINI RESPONSE] " << text << std::endl;

                                if (text.find("<STATUS: PASSED_PHASE_3>") != std::string::npos) {
                                    state = OrchestratorState::RESOLUTION;
                                } else {
                                    // Extract new prompt
                                    size_t pos = text.find("[CLI AGENT PROMPT]");
                                    if (pos != std::string::npos) {
                                        std::string newPrompt = text.substr(pos);
                                        std::ofstream ofs(promptFile);
                                        ofs << newPrompt;
                                        ofs.close();
                                        std::cout << "[ORCHESTRATOR] New prompt written to " << promptFile << ". Looping back to EXECUTE." << std::endl;
                                        state = OrchestratorState::EXECUTE;
                                    } else {
                                        std::cerr << "[ERROR] Reviewer did not return PASSED or a new CLI AGENT PROMPT." << std::endl;
                                        running = false;
                                    }
                                }
                            } else {
                                std::cerr << "[ERROR] Unexpected API response: " << responseBuffer << std::endl;
                                running = false;
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[ERROR] Failed to parse Gemini response: " << e.what() << std::endl;
                            running = false;
                        }
                    }
                    curl_slist_free_all(headers);
                } else {
                    running = false;
                }
                break;
            }

            case OrchestratorState::RESOLUTION:
            {
                std::cout << "[STATE: RESOLUTION] Goal achieved." << std::endl;
                running = false;
                break;
            }
        }
    }

    std::cout << "[ORCHESTRATOR] Shutdown." << std::endl;
    curl_global_cleanup();
    return 0;
}
