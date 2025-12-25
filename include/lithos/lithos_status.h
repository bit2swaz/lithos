/**
 * Lithos Storage Engine - Public Status Codes
 * 
 * This header defines the error codes returned by all Lithos API functions.
 * We separate this into its own header to avoid circular dependencies in the
 * internal implementation (e.g., util modules that need status codes but 
 * shouldn't depend on the full public API).
 * 
 * Design Philosophy:
 * - Status codes are lightweight enums (4 bytes).
 * - The internal Status struct (in src/util/status.h) wraps this with 
 *   contextual error messages.
 * - This follows the Go/Rust "Result" pattern but adapted for C.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_STATUS_H
#define LITHOS_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Status codes for all Lithos operations.
 * 
 * These codes are returned by internal functions and exposed through
 * the public API. They represent broad categories of errors; specific
 * details are carried in the Status struct's message field.
 */
typedef enum {
    /**
     * LITHOS_OK (0): Operation completed successfully.
     * This is the only "success" code. All others are failures.
     */
    LITHOS_OK = 0,
    
    /**
     * LITHOS_NOT_FOUND (1): Requested key does not exist.
     * Not treated as an error in many contexts (e.g., conditional updates).
     */
    LITHOS_NOT_FOUND = 1,
    
    /**
     * LITHOS_CORRUPTION (2): Data integrity violation detected.
     * Indicates checksum mismatches, invalid file formats, or inconsistent
     * metadata. Recovery typically requires rebuilding from backups.
     */
    LITHOS_CORRUPTION = 2,
    
    /**
     * LITHOS_IO_ERROR (3): System-level I/O failure.
     * Examples: Permission denied, disk full, network timeout (for future
     * distributed extensions). Check errno for OS-specific details.
     */
    LITHOS_IO_ERROR = 3,
    
    /**
     * LITHOS_INVALID_ARGUMENT (4): Invalid user input.
     * Examples: NULL pointer, negative size, invalid option value.
     * This is a programming error, not a runtime condition.
     */
    LITHOS_INVALID_ARGUMENT = 4
} lithos_status_code;

#ifdef __cplusplus
}
#endif

#endif  // LITHOS_STATUS_H
