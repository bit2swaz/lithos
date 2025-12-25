/**
 * Lithos Storage Engine - Internal Status Wrapper
 * 
 * This module provides a richer Status type for internal use, wrapping
 * the public lithos_status_code enum with detailed error messages.
 * 
 * Why a struct wrapper?
 * In C++, we'd use exceptions or std::expected. In Go, we'd return (T, error).
 * In C, we use a struct that carries both a code and a message. This allows:
 * - Propagating context up the call stack ("SSTable corrupted at offset 1024")
 * - Avoiding errno (which is thread-local but gets clobbered)
 * - Consistent error handling across sync/async paths
 * 
 * Memory Management:
 * - OK statuses have NULL state (zero allocation).
 * - Error messages are stored as malloc'd strings OR static strings.
 * - Callers must call Status_Free() on non-OK statuses to avoid leaks.
 * - We provide a Status_Move() to transfer ownership semantically.
 * 
 * Concurrency:
 * - Status objects are NOT thread-safe (they're value types).
 * - Each thread should create its own Status for returns.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_UTIL_STATUS_H
#define LITHOS_UTIL_STATUS_H

#include "lithos/lithos_status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Status - A code + message pair for error propagation.
 * 
 * Layout (16 bytes on 64-bit):
 * - code (4 bytes): The lithos_status_code enum.
 * - state (8 bytes): Pointer to error message string.
 * 
 * Invariants:
 * - If code == LITHOS_OK, state MUST be NULL.
 * - If code != LITHOS_OK, state MAY be NULL (generic error) or point to
 *   a heap/static string describing the error.
 */
typedef struct {
    lithos_status_code code;
    const char* state;  // NULL if OK, else error message
} Status;

/**
 * Status_OK - Create a success status.
 * 
 * Returns: Status with code=LITHOS_OK, state=NULL.
 * No allocation; no cleanup needed.
 */
Status Status_OK(void);

/**
 * Status_NotFound - Create a "key not found" error.
 * 
 * @param msg: Optional message (can be NULL for generic "Not Found").
 *             If non-NULL, the string is COPIED (malloc'd).
 * 
 * Use Case: Get("nonexistent_key") -> Status_NotFound("key=foo").
 * 
 * Returns: Status with code=LITHOS_NOT_FOUND.
 * Caller must call Status_Free() on the returned value.
 */
Status Status_NotFound(const char* msg);

/**
 * Status_Corruption - Create a data corruption error.
 * 
 * @param msg: Primary error message (e.g., "SSTable footer invalid").
 * @param msg2: Optional secondary context (e.g., "file=data.sst").
 * 
 * Both strings are COPIED. If msg2 is provided, the final message is:
 * "msg: msg2"
 * 
 * Use Case: Detecting checksum mismatch in a data block.
 * 
 * Returns: Status with code=LITHOS_CORRUPTION.
 * Caller must call Status_Free() on the returned value.
 */
Status Status_Corruption(const char* msg, const char* msg2);

/**
 * Status_IOError - Create an I/O error.
 * 
 * @param msg: Error message (e.g., "Failed to open WAL").
 * @param msg2: Optional context (e.g., filename or errno description).
 * 
 * Returns: Status with code=LITHOS_IO_ERROR.
 * Caller must call Status_Free() on the returned value.
 */
Status Status_IOError(const char* msg, const char* msg2);

/**
 * Status_InvalidArgument - Create an invalid argument error.
 * 
 * @param msg: Error message (e.g., "Key cannot be NULL").
 * 
 * Use Case: Validating user input at API boundaries.
 * 
 * Returns: Status with code=LITHOS_INVALID_ARGUMENT.
 * Caller must call Status_Free() on the returned value.
 */
Status Status_InvalidArgument(const char* msg);

/**
 * Status_IsOK - Check if a status represents success.
 * 
 * @param s: The status to check.
 * 
 * Returns: true if s.code == LITHOS_OK, false otherwise.
 * 
 * Usage Pattern:
 *   Status s = DoWork();
 *   if (!Status_IsOK(s)) {
 *       fprintf(stderr, "Error: %s\n", Status_ToString(s));
 *       Status_Free(s);
 *       return s;
 *   }
 */
bool Status_IsOK(Status s);

/**
 * Status_IsNotFound - Check if a status is a "not found" error.
 * 
 * @param s: The status to check.
 * 
 * Returns: true if s.code == LITHOS_NOT_FOUND, false otherwise.
 * 
 * Use Case: Conditional logic where "not found" is expected.
 */
bool Status_IsNotFound(Status s);

/**
 * Status_IsCorruption - Check if a status is a corruption error.
 */
bool Status_IsCorruption(Status s);

/**
 * Status_IsIOError - Check if a status is an I/O error.
 */
bool Status_IsIOError(Status s);

/**
 * Status_ToString - Get a human-readable representation of a status.
 * 
 * @param s: The status to stringify.
 * 
 * Returns: A static string if OK, or s.state if non-NULL, or a generic
 *          error name. The returned pointer is valid until s is freed.
 * 
 * IMPORTANT: Do NOT free the returned pointer. It either points into
 *            s.state (which Status_Free handles) or to a static string.
 * 
 * Thread Safety: Safe to call concurrently on different Status objects.
 */
const char* Status_ToString(Status s);

/**
 * Status_Free - Deallocate a status's internal state.
 * 
 * @param s: The status to free. After this call, s is invalid.
 * 
 * Idempotency: Safe to call on OK statuses (no-op).
 * 
 * Pattern: Always call this on non-OK statuses before they go out of scope.
 * 
 *   Status s = Func();
 *   if (!Status_IsOK(s)) {
 *       Log(Status_ToString(s));
 *       Status_Free(s);
 *       return;
 *   }
 */
void Status_Free(Status s);

/**
 * Status_Copy - Deep copy a status.
 * 
 * @param s: The status to copy.
 * 
 * Returns: A new Status with its own copy of the message.
 * 
 * Use Case: Storing a status in a struct or returning from a function
 *           where the original status will be freed.
 */
Status Status_Copy(Status s);

#ifdef __cplusplus
}
#endif

#endif  // LITHOS_UTIL_STATUS_H
