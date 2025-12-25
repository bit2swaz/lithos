/**
 * Lithos Storage Engine - Status Implementation
 * 
 * This file implements the Status wrapper functions for error propagation.
 * 
 * Implementation Strategy:
 * - We use malloc for dynamic error messages to allow detailed context.
 * - To avoid leaks, callers MUST call Status_Free() on non-OK statuses.
 * - For simple cases, we provide static strings to avoid allocation overhead.
 * 
 * Performance Notes:
 * - Status_OK is a compile-time constant (no allocation).
 * - Error path allocations are acceptable (errors are exceptional).
 * - We use snprintf for safe string concatenation (prevents buffer overflows).
 * 
 * Author: Aditya (@bit2swaz)
 */

#include "util/status.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Static error messages for common cases (avoid malloc for brevity) */
static const char* kNotFoundMsg = "Not Found";
static const char* kCorruptionMsg = "Corruption";
static const char* kIOErrorMsg = "IO Error";
static const char* kInvalidArgumentMsg = "Invalid Argument";

/*
 * Helper: Copy a string to the heap.
 * Returns NULL if input is NULL.
 */
static char* CopyString(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = (char*)malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

/*
 * Helper: Concatenate two strings with ": " separator.
 * Allocates new memory. Returns NULL on allocation failure.
 */
static char* ConcatStrings(const char* msg1, const char* msg2) {
    if (msg1 == NULL) {
        return CopyString(msg2);
    }
    if (msg2 == NULL) {
        return CopyString(msg1);
    }
    
    // Calculate required size: len1 + ": " + len2 + '\0'
    size_t len1 = strlen(msg1);
    size_t len2 = strlen(msg2);
    size_t total = len1 + 2 + len2 + 1;
    
    char* result = (char*)malloc(total);
    if (result != NULL) {
        snprintf(result, total, "%s: %s", msg1, msg2);
    }
    return result;
}

/* ========== Public API Implementation ========== */

Status Status_OK(void) {
    Status s;
    s.code = LITHOS_OK;
    s.state = NULL;
    return s;
}

Status Status_NotFound(const char* msg) {
    Status s;
    s.code = LITHOS_NOT_FOUND;
    
    // If no message provided, use static string (no allocation)
    if (msg == NULL) {
        s.state = kNotFoundMsg;
    } else {
        s.state = CopyString(msg);
        // If malloc fails, fall back to static message
        if (s.state == NULL) {
            s.state = kNotFoundMsg;
        }
    }
    
    return s;
}

Status Status_Corruption(const char* msg, const char* msg2) {
    Status s;
    s.code = LITHOS_CORRUPTION;
    
    // If no messages, use static string
    if (msg == NULL && msg2 == NULL) {
        s.state = kCorruptionMsg;
    } else {
        s.state = ConcatStrings(msg, msg2);
        // If malloc fails, fall back to static message
        if (s.state == NULL) {
            s.state = kCorruptionMsg;
        }
    }
    
    return s;
}

Status Status_IOError(const char* msg, const char* msg2) {
    Status s;
    s.code = LITHOS_IO_ERROR;
    
    if (msg == NULL && msg2 == NULL) {
        s.state = kIOErrorMsg;
    } else {
        s.state = ConcatStrings(msg, msg2);
        if (s.state == NULL) {
            s.state = kIOErrorMsg;
        }
    }
    
    return s;
}

Status Status_InvalidArgument(const char* msg) {
    Status s;
    s.code = LITHOS_INVALID_ARGUMENT;
    
    if (msg == NULL) {
        s.state = kInvalidArgumentMsg;
    } else {
        s.state = CopyString(msg);
        if (s.state == NULL) {
            s.state = kInvalidArgumentMsg;
        }
    }
    
    return s;
}

bool Status_IsOK(Status s) {
    return s.code == LITHOS_OK;
}

bool Status_IsNotFound(Status s) {
    return s.code == LITHOS_NOT_FOUND;
}

bool Status_IsCorruption(Status s) {
    return s.code == LITHOS_CORRUPTION;
}

bool Status_IsIOError(Status s) {
    return s.code == LITHOS_IO_ERROR;
}

const char* Status_ToString(Status s) {
    // If OK, return static "OK" string
    if (Status_IsOK(s)) {
        return "OK";
    }
    
    // If we have a custom message, return it
    if (s.state != NULL) {
        return s.state;
    }
    
    // Otherwise, return generic message based on code
    switch (s.code) {
        case LITHOS_NOT_FOUND:
            return kNotFoundMsg;
        case LITHOS_CORRUPTION:
            return kCorruptionMsg;
        case LITHOS_IO_ERROR:
            return kIOErrorMsg;
        case LITHOS_INVALID_ARGUMENT:
            return kInvalidArgumentMsg;
        default:
            return "Unknown Error";
    }
}

void Status_Free(Status s) {
    // Only free if:
    // 1. Status is not OK
    // 2. State is not NULL
    // 3. State is not pointing to a static string
    if (!Status_IsOK(s) && s.state != NULL) {
        // Check if state points to a static string
        if (s.state != kNotFoundMsg &&
            s.state != kCorruptionMsg &&
            s.state != kIOErrorMsg &&
            s.state != kInvalidArgumentMsg) {
            // Safe to free (it's a heap-allocated string)
            free((void*)s.state);
        }
    }
}

Status Status_Copy(Status s) {
    // If OK, just return OK (no state to copy)
    if (Status_IsOK(s)) {
        return Status_OK();
    }
    
    // If state is a static string, just copy the struct
    if (s.state == kNotFoundMsg ||
        s.state == kCorruptionMsg ||
        s.state == kIOErrorMsg ||
        s.state == kInvalidArgumentMsg) {
        return s;
    }
    
    // Otherwise, deep copy the message
    Status copy;
    copy.code = s.code;
    copy.state = CopyString(s.state);
    
    // If malloc fails, fall back to static message
    if (copy.state == NULL) {
        switch (s.code) {
            case LITHOS_NOT_FOUND:
                copy.state = kNotFoundMsg;
                break;
            case LITHOS_CORRUPTION:
                copy.state = kCorruptionMsg;
                break;
            case LITHOS_IO_ERROR:
                copy.state = kIOErrorMsg;
                break;
            case LITHOS_INVALID_ARGUMENT:
                copy.state = kInvalidArgumentMsg;
                break;
            default:
                copy.state = NULL;
        }
    }
    
    return copy;
}
