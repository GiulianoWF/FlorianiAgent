#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <sstream>

using json = nlohmann::json;

// SSE line parser — extracts content tokens from streamed chunks
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* buf = static_cast<std::string*>(userdata);

    buf->append(ptr, total);

    // Process complete lines
    std::istringstream stream(*buf);
    std::string line;
    std::string remaining;
    bool has_remaining = false;

    while (std::getline(stream, line)) {
        if (stream.eof() && buf->back() != '\n') {
            remaining = line;
            has_remaining = true;
            break;
        }

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("data: ", 0) != 0) continue;

        std::string data = line.substr(6);
        if (data == "[DONE]") {
            std::cout << "\n";
            continue;
        }

        try {
            auto j = json::parse(data);
            if (j.contains("choices") && !j["choices"].empty()) {
                auto& delta = j["choices"][0]["delta"];
                if (delta.contains("content") && delta["content"].is_string()) {
                    std::cout << delta["content"].get<std::string>() << std::flush;
                }
            }
        } catch (const json::parse_error&) {}
    }

    *buf = has_remaining ? remaining : "";
    return total;
}

static std::string send_query(const std::string& base_url, const std::string& message, bool stream) {
    json payload = {
        {"messages", json::array({
            {{"role", "user"}, {"content", message}}
        })},
        {"stream", stream}
    };
    std::string body = payload.dump();

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl_easy_init failed\n";
        return "";
    }

    std::string url = base_url + "/v1/chat/completions";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response_buf;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    std::cout << "\033[1;34m> " << message << "\033[0m\n";

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";
        return "";
    }

    return response_buf;
}

int main(int argc, char* argv[]) {
    std::string base_url = "http://localhost:8080";
    bool stream = true;
    bool interactive = false;

    // Collect all non-flag args as the query
    std::string query;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--url" && i + 1 < argc) {
            base_url = argv[++i];
        } else if (arg == "--no-stream") {
            stream = false;
        } else if (arg == "-i" || arg == "--interactive") {
            interactive = true;
        } else if (arg == "--help") {
            std::cerr << "Usage: " << argv[0] << " [OPTIONS] [QUERY...]\n"
                      << "  --url URL        Agent URL (default: http://localhost:8080)\n"
                      << "  --no-stream      Use non-streaming mode\n"
                      << "  -i, --interactive  Interactive mode (read from stdin)\n"
                      << "  --help           Show this help\n"
                      << "\nExamples:\n"
                      << "  " << argv[0] << " \"What time is it?\"\n"
                      << "  " << argv[0] << " -i\n"
                      << "  " << argv[0] << " --no-stream \"Hello\"\n";
            return 0;
        } else {
            if (!query.empty()) query += " ";
            query += arg;
        }
    }

    if (interactive) {
        std::cout << "floriani-agent mock client (interactive mode)\n";
        std::cout << "Type your queries, Ctrl+D to exit.\n\n";

        std::string line;
        while (true) {
            std::cout << "\033[1;32m$ \033[0m" << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;
            send_query(base_url, line, stream);
            std::cout << "\n";
        }
    } else if (!query.empty()) {
        send_query(base_url, query, stream);
    } else {
        // Default test queries
        std::cout << "Running default test queries...\n\n";
        send_query(base_url, "Hello! Who are you?", stream);
        std::cout << "\n---\n\n";
        send_query(base_url, "What is 2 + 2?", stream);
    }

    return 0;
}
