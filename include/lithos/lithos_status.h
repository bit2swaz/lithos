/*
 * Status Codes: Error Handling for the Entire Lithos API
 * =====================================================
 * Defines the error codes returned by all Lithos operations, providing
 * a consistent way to handle failures across the entire system.
 *
 * Big Picture: Status Codes = "Typed Error Handling Without Exceptions"
 * ====================================================================
 * C doesn't have exceptions, so we need a way to propagate errors. Status
 * codes provide typed error information that can be checked at any level.
 * This follows the "Result" pattern from Rust/Go but adapted for C.
 *
 * Where it fits: Every public and internal API function returns a status
 * code. The Status struct (in util/status.h) adds contextual messages.
 *
 * Key Concepts:
 * - Typed errors: Specific codes for different failure modes (corruption, I/O,
 * etc.).
 * - Non-throwing: C-style error handling without stack unwinding.
 * - Contextual messages: Status struct provides detailed error information.
 * - Recovery guidance: Different codes suggest different recovery strategies.
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

#endif // LITHOS_STATUS_H
