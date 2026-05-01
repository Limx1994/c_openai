#include "openai_error.h"

const char* openai_error_str(OpenAI_ErrorCode code) {
    switch (code) {
        case OPENAI_OK:
            return "Success";
        case OPENAI_ERR_INVALID_PARAM:
            return "Invalid parameter";
        case OPENAI_ERR_MEMORY:
            return "Memory allocation failed";
        case OPENAI_ERR_NETWORK:
            return "Network error";
        case OPENAI_ERR_TIMEOUT:
            return "Request timeout";
        case OPENAI_ERR_PARSE:
            return "JSON parse error";
        case OPENAI_ERR_API:
            return "API error";
        case OPENAI_ERR_AUTH:
            return "Authentication failed";
        case OPENAI_ERR_RATE_LIMIT:
            return "Rate limit exceeded";
        case OPENAI_ERR_SERVER:
            return "Server error";
        default:
            return "Unknown error";
    }
}