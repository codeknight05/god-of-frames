#pragma once

#include "types.h"

#include <string>
#include <vector>

class GeminiClient {
public:
    explicit GeminiClient(std::string apiKey, std::string model = "gemini-1.5-flash");

    bool IsConfigured() const;
    std::vector<AiAction> Analyze(const TelemetrySnapshot& t) const;

private:
    std::string apiKey_;
    std::string model_;

    std::string BuildPrompt(const TelemetrySnapshot& t) const;
    std::string Generate(const std::string& prompt) const;
    std::string ExtractTextFromResponseJson(const std::string& json) const;
    std::vector<AiAction> ParseActions(const std::string& text) const;
};
