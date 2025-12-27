/*
 * Filter Blocks (Bloom Filters)
 * =============================
 *
 * One Bloom filter is built for every 2KB of SSTable data. The filter block is
 * just all filters concatenated, followed by an offset array and metadata:
 *
 *   [filter 0][filter 1]...[filter N-1]
 *   [offset(filter 0) uint32]...[offset(filter N-1) uint32]
 *   [offset_of_offset_array uint32]
 *   [base_lg byte]   // log2(filter spacing), default 11 (2KB)
 *
 * On reads, we map a data block offset → filter index → Bloom query. A false
 * result lets us skip the data block entirely.
 */

#ifndef LITHOS_CORE_TABLE_FILTER_BLOCK_H_
#define LITHOS_CORE_TABLE_FILTER_BLOCK_H_

#include "lithos/filter_policy.h"
#include "util/slice.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct FilterBlockBuilder FilterBlockBuilder;
typedef struct FilterBlockReader FilterBlockReader;

/*
 * FilterBlockBuilder - Builds filter blocks for writing
 */

/*
 * FilterBlockBuilder_Create - Create a new filter block builder.
 *
 * Parameters:
 *   policy - The filter policy to use (e.g., Bloom filter)
 *
 * Returns: New builder instance, or NULL on allocation failure.
 */
FilterBlockBuilder *
FilterBlockBuilder_Create(const Lithos_FilterPolicy *policy);

/*
 * FilterBlockBuilder_Destroy - Free a filter block builder.
 */
void FilterBlockBuilder_Destroy(FilterBlockBuilder *builder);

/*
 * FilterBlockBuilder_StartBlock - Signal start of a new data block.
 *
 * Parameters:
 *   builder      - The builder instance
 *   block_offset - File offset where the new data block starts
 *
 * If we've crossed a 2KB boundary, generates a filter for accumulated keys.
 */
void FilterBlockBuilder_StartBlock(FilterBlockBuilder *builder,
                                   uint64_t block_offset);

/*
 * FilterBlockBuilder_AddKey - Add a key to the current filter.
 *
 * Parameters:
 *   builder - The builder instance
 *   key     - The key to add
 */
void FilterBlockBuilder_AddKey(FilterBlockBuilder *builder, Lithos_Slice key);

/*
 * FilterBlockBuilder_Finish - Finalize the filter block.
 *
 * Generates the final filter and returns the complete filter block data.
 *
 * Parameters:
 *   builder - The builder instance
 *
 * Returns: Slice containing the complete filter block.
 *          The data remains valid until the builder is destroyed.
 */
Lithos_Slice FilterBlockBuilder_Finish(FilterBlockBuilder *builder);

/*
 * FilterBlockReader - Reads filter blocks for querying
 */

/*
 * FilterBlockReader_Create - Create a filter block reader.
 *
 * Parameters:
 *   policy   - The filter policy used to create the filters
 *   contents - The filter block data (from file)
 *
 * Returns: New reader instance, or NULL on error.
 *
 * Note: The reader does NOT take ownership of contents.data.
 */
FilterBlockReader *FilterBlockReader_Create(const Lithos_FilterPolicy *policy,
                                            Lithos_Slice contents);

/*
 * FilterBlockReader_Destroy - Free a filter block reader.
 */
void FilterBlockReader_Destroy(FilterBlockReader *reader);

/*
 * FilterBlockReader_KeyMayMatch - Check if a key might exist in a block.
 *
 * Parameters:
 *   reader       - The reader instance
 *   block_offset - File offset of the data block to check
 *   key          - The key to check
 *
 * Returns:
 *   true  - Key MIGHT be in the block (read the block to confirm)
 *   false - Key is DEFINITELY NOT in the block (skip the block)
 */
bool FilterBlockReader_KeyMayMatch(FilterBlockReader *reader,
                                   uint64_t block_offset, Lithos_Slice key);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_CORE_TABLE_FILTER_BLOCK_H_ */
