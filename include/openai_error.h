/**
 * @file openai_error.h
 * @brief OpenAI API Error Codes
 */

#ifndef OPENAI_ERROR_H
#define OPENAI_ERROR_H

/**
 * @brief OpenAI error codes
 */
typedef enum {
    OPENAI_OK = 0,                 /**< Success */
    OPENAI_ERR_INVALID_PARAM = -1,   /**< Invalid parameter */
    OPENAI_ERR_MEMORY = -2,          /**< Memory allocation failed */
    OPENAI_ERR_NETWORK = -3,         /**< Network error */
    OPENAI_ERR_TIMEOUT = -4,         /**< Request timeout */
    OPENAI_ERR_PARSE = -5,           /**< JSON parse error */
    OPENAI_ERR_API = -6,             /**< API returned error */
    OPENAI_ERR_AUTH = -7,            /**< Authentication failed */
    OPENAI_ERR_RATE_LIMIT = -8,       /**< Rate limit exceeded */
    OPENAI_ERR_SERVER = -9,          /**< Server error */
    OPENAI_ERR_UNKNOWN = -99,         /**< Unknown error */
    OPENAI_ERR_BUFFER_EMPTY = -100,  /**< Buffer empty (streaming) */
    OPENAI_ERR_EOF = -101             /**< End of stream (streaming) */
} OpenAI_ErrorCode;

/**
 * @brief Get error code string
 * @param code Error code
 * @return Human-readable error string
 */
const char* openai_error_str(OpenAI_ErrorCode code);

#endif /* OPENAI_ERROR_H */
