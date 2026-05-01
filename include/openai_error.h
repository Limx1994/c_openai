#ifndef OPENAI_ERROR_H
#define OPENAI_ERROR_H

typedef enum {
    OPENAI_OK = 0,
    OPENAI_ERR_INVALID_PARAM = -1,
    OPENAI_ERR_MEMORY = -2,
    OPENAI_ERR_NETWORK = -3,
    OPENAI_ERR_TIMEOUT = -4,
    OPENAI_ERR_PARSE = -5,
    OPENAI_ERR_API = -6,
    OPENAI_ERR_AUTH = -7,
    OPENAI_ERR_RATE_LIMIT = -8,
    OPENAI_ERR_SERVER = -9,
    OPENAI_ERR_UNKNOWN = -99
} OpenAI_ErrorCode;

const char* openai_error_str(OpenAI_ErrorCode code);

#endif /* OPENAI_ERROR_H */