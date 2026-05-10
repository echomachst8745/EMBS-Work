// HlS
//
// Tests all possible clue placements in a line and returns cells
// that are either filled in every placement or cells that are filled
// in some placement, and the number of valid placements. If there is a
// contradiction based on the given information, a status flag is set to 0
// or 1 if there was no contradiction

#include "nonogram_line_accel.h"
#include <stdint.h>

// Generates a bit mask for the line based on the size of the line
static inline uint32 LineMask(uint8_t lineLength)
{
#pragma HLS INLINE
    return (lineLength == 32) ? 0xFFFFFFFFU : ((1U << lineLength) - 1U);
}

// Reads the puzzle data from RAM and fills a job descriptor with the information
static void ReadPuzzleData(uint32 *ram, uint32 pJobDesc[JOB_WORD_COUNT])
{
#pragma HLS INLINE
read_puzzle_loop:
    for (int i = 0; i < JOB_WORD_COUNT; ++i)
    {
        // Pipeline descriptor reads so the AXI master can accept one word per cycle
#pragma HLS PIPELINE II=1
        pJobDesc[i] = ram[i];
    }
}

// Writes the HLS job result back into RAM
static void WriteResult(uint32 *ram,
                        uint32 forceFilledMask, uint32 possibleFilledMask,
                        uint32 validPlacementCount, uint32 status)
{
#pragma HLS INLINE
    ram[JOB_FORCE_FILLED]    = forceFilledMask;
    ram[JOB_POSSIBLE_FILLED] = possibleFilledMask;
    ram[JOB_VALID_COUNT]     = validPlacementCount;
    ram[JOB_STATUS]          = status;
}

static inline uint32 BuildCandidatePlacementMask(const uint32 pBlockMasks[CLUE_MAX_BLOCKS],
                                                   const uint8_t pBlockStartIndexes[CLUE_MAX_BLOCKS])
{
#pragma HLS INLINE
    uint32 candidatePlacementMask = 0;

build_candidate_loop:
    for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
    {
#pragma HLS UNROLL
        if (pBlockMasks[i] != 0)
        {
            candidatePlacementMask |= pBlockMasks[i] << pBlockStartIndexes[i];
        }
    }

    return candidatePlacementMask;
}

// Returns the index of the MSB in a uint16_t
// So 0b0000000000001000 = 3
static inline uint8_t HighestSetIndex(uint16_t bits)
{
#pragma HLS INLINE
    uint8_t index = 0;

    for (int i = 0; i < 16; ++i)
    {
#pragma HLS UNROLL
        if (bits & (uint16_t)(1U << i))
        {
            index = (uint8_t)i;
        }
    }

    return index;
}

uint32 toplevel(uint32 *ram, uint32 *arg1, uint32 *arg2,
                uint32 *arg3, uint32 *arg4)
{
#pragma HLS INTERFACE m_axi      port=ram   offset=slave bundle=MAXI
#pragma HLS INTERFACE s_axilite  port=arg1  bundle=AXILiteS register
#pragma HLS INTERFACE s_axilite  port=arg2  bundle=AXILiteS register
#pragma HLS INTERFACE s_axilite  port=arg3  bundle=AXILiteS register
#pragma HLS INTERFACE s_axilite  port=arg4  bundle=AXILiteS register
#pragma HLS INTERFACE s_axilite  port=return bundle=AXILiteS register

    uint32 jobDesc[JOB_WORD_COUNT];
#pragma HLS ARRAY_PARTITION variable=jobDesc complete dim=1

    ReadPuzzleData(ram, jobDesc);

    const uint8_t lineLength     = jobDesc[JOB_LINE_LENGTH];
    const uint8_t clueBlockCount = jobDesc[JOB_BLOCK_COUNT];

    const uint32 lineMask    = LineMask(lineLength);
    const uint32 knownMask   = jobDesc[JOB_KNOWN_BITMAP] & lineMask;
    const uint32 stateMask   = jobDesc[JOB_STATE_BITMAP] & lineMask;
    const uint32 knownFilled = knownMask & stateMask;
    const uint32 knownEmpty  = knownMask & ~stateMask;

    if (clueBlockCount == 0)
    {
        // There are no clues for the line so it must be empty.
        // If we have known cells that are filled though thats a contradiction
        uint32 status = (knownFilled == 0) ? 1U : 0U;
        uint32 validPlacementCount = (status != 0) ? 1U : 0U;

        WriteResult(ram, 0, 0, validPlacementCount, status);

        return status;
    }

    // Partition arrays
    uint8_t clueBlockLengths[CLUE_MAX_BLOCKS];
    uint32  clueBlockMasks[CLUE_MAX_BLOCKS];
    uint8_t clueMinStartIndexes[CLUE_MAX_BLOCKS];
    uint8_t gapsBeforeClueBlocks[CLUE_MAX_BLOCKS];
#pragma HLS ARRAY_PARTITION variable=clueBlockLengths complete dim=1
#pragma HLS ARRAY_PARTITION variable=clueBlockMasks complete dim=1
#pragma HLS ARRAY_PARTITION variable=clueMinStartIndexes complete dim=1
#pragma HLS ARRAY_PARTITION variable=gapsBeforeClueBlocks complete dim=1

    uint8_t minCellsNeededForLine = 0;

prepare_blocks_loop:
    for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
    {
        // Fixed maximum clue count lets HLS unroll this setup logic
#pragma HLS UNROLL
        uint8_t clueLength = (i < clueBlockCount)
                              ? (uint8_t)(jobDesc[JOB_BLOCKS + i] & 0b111111U)
                              : (uint8_t)0;

        clueBlockLengths[i] = clueLength;
        clueBlockMasks[i] = (i < clueBlockCount) ? LineMask(clueLength) : 0U;
        gapsBeforeClueBlocks[i] = 0;

        if (i < clueBlockCount)
        {
            minCellsNeededForLine += clueLength;
            if (i > 0) { minCellsNeededForLine += 1; }
        }
    }

    if (minCellsNeededForLine > lineLength)
    {
        WriteResult(ram, 0, 0, 0, 0);

        return 0;
    }

    uint8_t clueBlockMinStartIndex = 0;

build_clue_min_start_loop:
    for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
    {
#pragma HLS UNROLL
        clueMinStartIndexes[i] = clueBlockMinStartIndex;

        if (i < clueBlockCount)
        {
            clueBlockMinStartIndex = clueBlockMinStartIndex + clueBlockLengths[i] + 1;
        }
    }

    const uint8_t lineGapSpare = (uint8_t)(lineLength - minCellsNeededForLine);

    uint32 forceFilledAccumulate     = 0xFFFFFFFFU;
    uint32 possibleFilledAccumulate  = 0;
    uint32 validPlacementCount = 0;
    int done = 0;

main_loop:
    while (!done)
    {
#pragma HLS LOOP_TRIPCOUNT min=1 max=1400000 avg=10000

        uint8_t clueBlockStartIndexes[CLUE_MAX_BLOCKS];
        uint8_t usedExtraSpaceBeforeClueBlock[CLUE_MAX_BLOCKS];
#pragma HLS ARRAY_PARTITION variable=clueBlockStartIndexes  complete dim=1
#pragma HLS ARRAY_PARTITION variable=usedExtraSpaceBeforeClueBlock complete dim=1

        uint8_t runningGap = 0;

build_start_loop:
        for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
        {
#pragma HLS UNROLL
            if (i < clueBlockCount) { runningGap += gapsBeforeClueBlocks[i]; }

            clueBlockStartIndexes[i] = clueMinStartIndexes[i] + runningGap;
            usedExtraSpaceBeforeClueBlock[i] = runningGap;
        }

        uint32 candidate = BuildCandidatePlacementMask(clueBlockMasks, clueBlockStartIndexes) & lineMask;

        // Bit masks test all cells of the candidate against known cells at once
        if (((candidate & knownEmpty) == 0) &&
            ((candidate & knownFilled) == knownFilled))
        {
            forceFilledAccumulate &= candidate;
            possibleFilledAccumulate  |= candidate;
            if (validPlacementCount != 0xFFFFFFFFU) { validPlacementCount++; }
        }

        uint16_t canIncrement = 0;

find_increment_loop:
        for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
        {
#pragma HLS UNROLL
            if (i < clueBlockCount && usedExtraSpaceBeforeClueBlock[i] < lineGapSpare)
            {
                canIncrement |= (uint16_t)(1U << i);
            }
        }

        int hasNext = canIncrement == 0 ? 0 : 1;
        uint8_t incrementIndex = HighestSetIndex(canIncrement);

update_gap_loop:
        for (int i = 0; i < CLUE_MAX_BLOCKS; ++i)
        {
#pragma HLS UNROLL
            if (i < clueBlockCount) {
                if (hasNext && i == incrementIndex)
                {
                    gapsBeforeClueBlocks[i] = gapsBeforeClueBlocks[i] + 1;
                }
                else if (hasNext && i > incrementIndex)
                {
                    gapsBeforeClueBlocks[i] = 0;
                }
            }
        }

        done = 1 - hasNext;
    }

    uint32 status = (validPlacementCount == 0) ? 0U : 1U;
    uint32 forceFilledMask     = (status != 0) ? (forceFilledAccumulate & lineMask) : 0U;
    uint32 possibleFilledMask  = (status != 0) ? (possibleFilledAccumulate  & lineMask) : 0U;

    WriteResult(ram, forceFilledMask, possibleFilledMask, validPlacementCount, status);

    return status;
}
