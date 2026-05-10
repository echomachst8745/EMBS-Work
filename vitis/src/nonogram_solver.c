// Nonogram solver

#include "nonogram_solver.h"
#include "config.h"
#include "hls_accel_driver.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#define RECHECK_QUEUE_COLUMN_FLAG    0x80
#define RECHECK_QUEUE_INDEX_MASK     0x3F

#define GUESS_TRIED_FILLED    0x01
#define GUESS_TRIED_EMPTY     0x02

// Recheck-line queue entries store a row/column flag plus the line index
typedef struct {
    uint64_t rowSet;
    uint64_t columnSet;
    uint8_t  fifo[NONOGRAM_MAX_DIM * 4];
    uint8_t  head;
    uint8_t  tail;
} RecheckLineQueue;

// Full grid snapshot and the cell guessed at that depth
typedef struct {
    uint32_t rowState[NONOGRAM_MAX_DIM];
    uint32_t rowKnown[NONOGRAM_MAX_DIM];
    uint32_t colState[NONOGRAM_MAX_DIM];
    uint32_t colKnown[NONOGRAM_MAX_DIM];
    uint32_t rowCount[NONOGRAM_MAX_DIM];
    uint32_t colCount[NONOGRAM_MAX_DIM];
    uint8_t  guessRow;
    uint8_t  guessColumn;
    uint8_t  triedMask;
} BacktrackData;

static BacktrackData backtrackStack[SOLVER_MAX_BACKTRACK];

static inline uint32_t GetCellBit(uint8_t cellIndex)
{
    return 1u << cellIndex;
}

static inline uint32_t GetLineMask(uint8_t lineLength)
{
    return (lineLength == 32) ? 0xFFFFFFFFu : ((1u << lineLength) - 1u);
}

static void RecheckQueueInit(RecheckLineQueue *pQueue)
{
    memset(pQueue, 0, sizeof(*pQueue));
}

static int RecheckQueueIsEmpty(const RecheckLineQueue *pQueue)
{
    return pQueue->head == pQueue->tail;
}

static void RecheckQueuePushRow(RecheckLineQueue *pQueue, uint8_t row)
{
    uint64_t rowBit = (uint64_t)1 << row;

    if (pQueue->rowSet & rowBit) { return; }

    pQueue->rowSet = pQueue->rowSet | rowBit;
    pQueue->fifo[pQueue->tail] = row;
    pQueue->tail = (uint8_t)((pQueue->tail + 1) % sizeof(pQueue->fifo));
}

static void RecheckQueuePushColumn(RecheckLineQueue *pQueue, uint8_t column)
{
    uint64_t columnBit = (uint64_t)1 << column;

    if (pQueue->columnSet & columnBit) { return; }

    pQueue->columnSet = pQueue->columnSet | columnBit;
    pQueue->fifo[pQueue->tail] = (uint8_t)(RECHECK_QUEUE_COLUMN_FLAG | column);
    pQueue->tail = (uint8_t)((pQueue->tail + 1) % sizeof(pQueue->fifo));
}

static uint8_t RecheckQueuePop(RecheckLineQueue *pQueue)
{
    uint8_t queueValue = pQueue->fifo[pQueue->head];

    pQueue->head = (uint8_t)((pQueue->head + 1) % sizeof(pQueue->fifo));

    if (queueValue & RECHECK_QUEUE_COLUMN_FLAG)
    {
        pQueue->columnSet =
            pQueue->columnSet & ~((uint64_t)1 << (queueValue & RECHECK_QUEUE_INDEX_MASK));
    }
    else
    {
        pQueue->rowSet =
            pQueue->rowSet & ~((uint64_t)1 << (queueValue & RECHECK_QUEUE_INDEX_MASK));
    }

    return queueValue;
}

static int ApplyRowResult(PuzzleState *pPuzzleState,
                           uint8_t row,
                           uint32_t forceFilledMask,
                           uint32_t possibleFilledMask,
                           RecheckLineQueue *pQueue)
{
    const uint32_t lineMask = GetLineMask(pPuzzleState->width);
    uint32_t newFilledMask = forceFilledMask & ~pPuzzleState->rowCellKnowns[row] & lineMask;
    uint32_t newEmptyMask  = (~possibleFilledMask) & ~pPuzzleState->rowCellKnowns[row] & lineMask;

    // Existing known cells must agree with the accelerator result
    if ((pPuzzleState->rowCellKnowns[row] &
         pPuzzleState->rowCellStates[row] &
         ~forceFilledMask &
         lineMask) != 0)
    {
        return 1;
    }

    if ((pPuzzleState->rowCellKnowns[row] &
         ~pPuzzleState->rowCellStates[row] &
         possibleFilledMask &
         lineMask) != 0)
    {
        return 1;
    }

    // Update known cells with newly proven row cells, mirror them into columns, then recheck those columns
    pPuzzleState->rowCellStates[row] = pPuzzleState->rowCellStates[row] | newFilledMask;
    pPuzzleState->rowCellKnowns[row] = pPuzzleState->rowCellKnowns[row] | newFilledMask | newEmptyMask;

    uint32_t changedMask = newFilledMask | newEmptyMask;
    while (changedMask)
    {
        uint8_t column = (uint8_t)__builtin_ctz(changedMask);
        changedMask = changedMask & (changedMask - 1);

        if (newFilledMask & GetCellBit(column))
        {
            pPuzzleState->colCellStates[column] = pPuzzleState->colCellStates[column] | GetCellBit(row);
        }

        pPuzzleState->colCellKnowns[column] = pPuzzleState->colCellKnowns[column] | GetCellBit(row);
        RecheckQueuePushColumn(pQueue, column);
    }

    return 0;
}

static int ApplyColumnResult(PuzzleState *pPuzzleState,
                              uint8_t column,
                              uint32_t forceFilledMask,
                              uint32_t possibleFilledMask,
                              RecheckLineQueue *pQueue)
{
    const uint32_t lineMask = GetLineMask(pPuzzleState->height);
    uint32_t newFilledMask = forceFilledMask & ~pPuzzleState->colCellKnowns[column] & lineMask;
    uint32_t newEmptyMask  = (~possibleFilledMask) & ~pPuzzleState->colCellKnowns[column] & lineMask;

    if ((pPuzzleState->colCellKnowns[column] &
         pPuzzleState->colCellStates[column] &
         ~forceFilledMask &
         lineMask) != 0)
    {
        return 1;
    }

    if ((pPuzzleState->colCellKnowns[column] &
         ~pPuzzleState->colCellStates[column] &
         possibleFilledMask &
         lineMask) != 0)
    {
        return 1;
    }

    // Update known cells with newly proven column cells, mirror them into rows, then recheck those rows
    pPuzzleState->colCellStates[column] = pPuzzleState->colCellStates[column] | newFilledMask;
    pPuzzleState->colCellKnowns[column] = pPuzzleState->colCellKnowns[column] | newFilledMask | newEmptyMask;

    uint32_t changedMask = newFilledMask | newEmptyMask;
    while (changedMask)
    {
        uint8_t row = (uint8_t)__builtin_ctz(changedMask);
        changedMask = changedMask & (changedMask - 1);

        if (newFilledMask & GetCellBit(row))
        {
            pPuzzleState->rowCellStates[row] = pPuzzleState->rowCellStates[row] | GetCellBit(column);
        }

        pPuzzleState->rowCellKnowns[row] = pPuzzleState->rowCellKnowns[row] | GetCellBit(column);
        RecheckQueuePushRow(pQueue, row);
    }

    return 0;
}

// Propagates rows/columns that need to be rechecked until no line can add more known cells.
// Each queued line is accelerated in hardware, then its result may queue
// crossing lines for another round of propagation
static int PropagateRecheckQueue(PuzzleState *pPuzzleState, RecheckLineQueue *pQueue)
{
    LineAccelResult result;

    while (!RecheckQueueIsEmpty(pQueue))
    {
        uint8_t queueValue = RecheckQueuePop(pQueue);
        int isColumn = (queueValue & RECHECK_QUEUE_COLUMN_FLAG) != 0;
        uint8_t lineIndex = queueValue & RECHECK_QUEUE_INDEX_MASK;

        if (isColumn)
        {
            const ClueLine *pClueLine = &pPuzzleState->colClues[lineIndex];

            // Run the HLS line solver for this independent column
            if (!HLSRunLine(pPuzzleState->height,
                            pClueLine->blocks,
                            pClueLine->blockCount,
                            pPuzzleState->colCellKnowns[lineIndex],
                            pPuzzleState->colCellStates[lineIndex],
                            &result))
            {
                pPuzzleState->colValidPlacementCount[lineIndex] = 0;
                return 1;
            }

            pPuzzleState->colValidPlacementCount[lineIndex] =
                result.validPlacementCount ? result.validPlacementCount : 1;

            if (ApplyColumnResult(pPuzzleState,
                                  lineIndex,
                                  result.forceFilledMask,
                                  result.possibleFilledMask,
                                  pQueue))
            {
                return 1;
            }
        }
        else
        {
            const ClueLine *pClueLine = &pPuzzleState->rowClues[lineIndex];

            // Run the HLS line solver for this independent row
            if (!HLSRunLine(pPuzzleState->width,
                            pClueLine->blocks,
                            pClueLine->blockCount,
                            pPuzzleState->rowCellKnowns[lineIndex],
                            pPuzzleState->rowCellStates[lineIndex],
                            &result))
            {
                pPuzzleState->rowValidPlacementCount[lineIndex] = 0;
                return 1;
            }

            pPuzzleState->rowValidPlacementCount[lineIndex] =
                result.validPlacementCount ? result.validPlacementCount : 1;

            if (ApplyRowResult(pPuzzleState,
                               lineIndex,
                               result.forceFilledMask,
                               result.possibleFilledMask,
                               pQueue))
            {
                return 1;
            }
        }
    }

    return 0;
}

static int PropagateAllLines(PuzzleState *pPuzzleState)
{
    RecheckLineQueue queue;

    RecheckQueueInit(&queue);

    for (uint8_t row = 0; row < pPuzzleState->height; ++row)
    {
        RecheckQueuePushRow(&queue, row);
    }

    for (uint8_t column = 0; column < pPuzzleState->width; ++column)
    {
        RecheckQueuePushColumn(&queue, column);
    }

    return PropagateRecheckQueue(pPuzzleState, &queue);
}

static int PropagateFromCell(PuzzleState *pPuzzleState, uint8_t row, uint8_t column)
{
    RecheckLineQueue queue;

    RecheckQueueInit(&queue);
    RecheckQueuePushRow(&queue, row);
    RecheckQueuePushColumn(&queue, column);

    return PropagateRecheckQueue(pPuzzleState, &queue);
}

static void SnapshotPuzzleState(const PuzzleState *pPuzzleState, BacktrackData *pBacktrack)
{
    memcpy(pBacktrack->rowState, pPuzzleState->rowCellStates, sizeof(pPuzzleState->rowCellStates));
    memcpy(pBacktrack->rowKnown, pPuzzleState->rowCellKnowns, sizeof(pPuzzleState->rowCellKnowns));
    memcpy(pBacktrack->colState, pPuzzleState->colCellStates, sizeof(pPuzzleState->colCellStates));
    memcpy(pBacktrack->colKnown, pPuzzleState->colCellKnowns, sizeof(pPuzzleState->colCellKnowns));
    memcpy(pBacktrack->rowCount, pPuzzleState->rowValidPlacementCount, sizeof(pPuzzleState->rowValidPlacementCount));
    memcpy(pBacktrack->colCount, pPuzzleState->colValidPlacementCount, sizeof(pPuzzleState->colValidPlacementCount));
}

static void RestorePuzzleState(PuzzleState *pPuzzleState, const BacktrackData *pBacktrack)
{
    memcpy(pPuzzleState->rowCellStates, pBacktrack->rowState, sizeof(pPuzzleState->rowCellStates));
    memcpy(pPuzzleState->rowCellKnowns, pBacktrack->rowKnown, sizeof(pPuzzleState->rowCellKnowns));
    memcpy(pPuzzleState->colCellStates, pBacktrack->colState, sizeof(pPuzzleState->colCellStates));
    memcpy(pPuzzleState->colCellKnowns, pBacktrack->colKnown, sizeof(pPuzzleState->colCellKnowns));
    memcpy(pPuzzleState->rowValidPlacementCount, pBacktrack->rowCount, sizeof(pPuzzleState->rowValidPlacementCount));
    memcpy(pPuzzleState->colValidPlacementCount, pBacktrack->colCount, sizeof(pPuzzleState->colValidPlacementCount));

    pPuzzleState->hasContradiction = 0;
}

// Branch on the unknown cell with the smallest remaining row/column product
static int PickGuessCell(const PuzzleState *pPuzzleState,
                          uint8_t *pGuessRow,
                          uint8_t *pGuessColumn)
{
    uint8_t rowKnownCells[NONOGRAM_MAX_DIM];
    uint8_t columnKnownCells[NONOGRAM_MAX_DIM];

    const uint32_t rowMask = GetLineMask(pPuzzleState->width);
    const uint32_t columnMask = GetLineMask(pPuzzleState->height);

    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        rowKnownCells[row] =
            (uint8_t)__builtin_popcount(pPuzzleState->rowCellKnowns[row] & rowMask);
    }

    for (int column = 0; column < pPuzzleState->width; ++column)
    {
        columnKnownCells[column] =
            (uint8_t)__builtin_popcount(pPuzzleState->colCellKnowns[column] & columnMask);
    }

    uint64_t bestScore = UINT64_MAX;
    int bestKnownCells = -1;
    int bestRow = -1;
    int bestColumn = -1;

    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        uint32_t unknownMask = ~pPuzzleState->rowCellKnowns[row] & rowMask;

        while (unknownMask)
        {
            int column = __builtin_ctz(unknownMask);
            unknownMask = unknownMask & (unknownMask - 1);

            uint32_t rowCount = pPuzzleState->rowValidPlacementCount[row];
            uint32_t columnCount = pPuzzleState->colValidPlacementCount[column];

            if (rowCount == 0 || columnCount == 0)
            {
                *pGuessRow = (uint8_t)row;
                *pGuessColumn = (uint8_t)column;
                return 1;
            }

            uint64_t score = (uint64_t)rowCount * (uint64_t)columnCount;
            int knownCells = rowKnownCells[row] + columnKnownCells[column];

            if (score < bestScore ||
                (score == bestScore && knownCells > bestKnownCells))
            {
                bestScore = score;
                bestKnownCells = knownCells;
                bestRow = row;
                bestColumn = column;
            }
        }
    }

    if (bestRow < 0) { return 0; }

    *pGuessRow = (uint8_t)bestRow;
    *pGuessColumn = (uint8_t)bestColumn;

    return 1;
}

static void ApplyGuess(PuzzleState *pPuzzleState,
                       uint8_t row,
                       uint8_t column,
                       int filled)
{
    pPuzzleState->rowCellKnowns[row] = pPuzzleState->rowCellKnowns[row] | GetCellBit(column);
    pPuzzleState->colCellKnowns[column] = pPuzzleState->colCellKnowns[column] | GetCellBit(row);

    if (filled)
    {
        pPuzzleState->rowCellStates[row] = pPuzzleState->rowCellStates[row] | GetCellBit(column);
        pPuzzleState->colCellStates[column] = pPuzzleState->colCellStates[column] | GetCellBit(row);
    }
    else
    {
        pPuzzleState->rowCellStates[row] = pPuzzleState->rowCellStates[row] & ~GetCellBit(column);
        pPuzzleState->colCellStates[column] = pPuzzleState->colCellStates[column] & ~GetCellBit(row);
    }
}

static int IsGridComplete(const PuzzleState *pPuzzleState)
{
    const uint32_t lineMask = GetLineMask(pPuzzleState->width);

    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        if ((pPuzzleState->rowCellKnowns[row] & lineMask) != lineMask) { return 0; }
    }

    return 1;
}

static int LineMatchesClue(uint32_t state,
                            uint32_t known,
                            uint8_t length,
                            const ClueLine *pClueLine)
{
    uint32_t lineMask = GetLineMask(length);

    if ((known & lineMask) != lineMask) { return 0; }

    uint8_t foundBlocks[NONOGRAM_MAX_BLOCKS];
    uint8_t foundBlockCount = 0;
    uint8_t runLength = 0;

    for (uint8_t cellIndex = 0; cellIndex < length; ++cellIndex)
    {
        if (state & GetCellBit(cellIndex))
        {
            runLength++;
        }
        else if (runLength)
        {
            if (foundBlockCount >= NONOGRAM_MAX_BLOCKS) { return 0; }

            foundBlocks[foundBlockCount++] = runLength;
            runLength = 0;
        }
    }

    if (runLength)
    {
        if (foundBlockCount >= NONOGRAM_MAX_BLOCKS) { return 0; }

        foundBlocks[foundBlockCount++] = runLength;
    }

    if (foundBlockCount != pClueLine->blockCount) { return 0; }

    for (uint8_t blockIndex = 0; blockIndex < foundBlockCount; ++blockIndex)
    {
        if (foundBlocks[blockIndex] != pClueLine->blocks[blockIndex]) { return 0; }
    }

    return 1;
}

int SolverLoadClues(PuzzleState *pPuzzleState,
                    uint32_t seed,
                    uint8_t difficulty,
                    uint8_t width,
                    uint8_t height,
                    const uint8_t *pStream,
                    uint16_t streamLength)
{
    if (width > NONOGRAM_MAX_DIM || height > NONOGRAM_MAX_DIM) { return 0; }

    memset(pPuzzleState, 0, sizeof(*pPuzzleState));

    pPuzzleState->seed       = seed;
    pPuzzleState->difficulty = difficulty;
    pPuzzleState->width      = width;
    pPuzzleState->height     = height;

    uint16_t streamOffset = 0;

    for (int row = 0; row < height; ++row)
    {
        if (streamOffset >= streamLength) { return 0; }

        uint8_t blockCount = pStream[streamOffset++];
        if (blockCount > NONOGRAM_MAX_BLOCKS) { return 0; }

        pPuzzleState->rowClues[row].blockCount = blockCount;

        for (uint8_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        {
            if (streamOffset >= streamLength) { return 0; }

            pPuzzleState->rowClues[row].blocks[blockIndex] = pStream[streamOffset++];
        }
    }

    for (int column = 0; column < width; ++column)
    {
        if (streamOffset >= streamLength) { return 0; }

        uint8_t blockCount = pStream[streamOffset++];
        if (blockCount > NONOGRAM_MAX_BLOCKS) { return 0; }

        pPuzzleState->colClues[column].blockCount = blockCount;

        for (uint8_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        {
            if (streamOffset >= streamLength) { return 0; }

            pPuzzleState->colClues[column].blocks[blockIndex] = pStream[streamOffset++];
        }
    }

    return streamOffset == streamLength ? 1 : 0;
}

int SolverSolve(PuzzleState *pPuzzleState, uint32_t maxTimeMs)
{
    TickType_t startTick = xTaskGetTickCount();
    const TickType_t budgetTicks = maxTimeMs / portTICK_RATE_MS;

    // Run row/column propagation with HLS from the clues
    if (PropagateAllLines(pPuzzleState))
    {
        pPuzzleState->hasContradiction = 1;
        pPuzzleState->solverFailed = 1;
        return 0;
    }

    // If propagation solved the whole grid, validate and finish
    if (IsGridComplete(pPuzzleState))
    {
        pPuzzleState->solved = SolverValidate(pPuzzleState);
        return pPuzzleState->solved;
    }

    int depth = 0;

    // Search the remaining unknown cells with depth-first guesses
    while (1)
    {
        if ((xTaskGetTickCount() - startTick) >= budgetTicks)
        {
            pPuzzleState->solverFailed = 1;

            return 0;
        }
        if (depth >= SOLVER_MAX_BACKTRACK)
        {
            pPuzzleState->solverFailed = 1;
            
            return 0;
        }

        uint8_t guessRow;
        uint8_t guessColumn;

        int needsBacktrack = 0;

        // Pick the unknown cell with the tightest row/column constraints
        if (!PickGuessCell(pPuzzleState, &guessRow, &guessColumn))
        {
            if (SolverValidate(pPuzzleState))
            {
                pPuzzleState->solved = 1;
                return 1;
            }

            needsBacktrack = 1;
        }
        else
        {
            BacktrackData *pBacktrack = &backtrackStack[depth];

            // Save the grid, try this cell filled first, and propagate the result.
            SnapshotPuzzleState(pPuzzleState, pBacktrack);
            pBacktrack->guessRow    = guessRow;
            pBacktrack->guessColumn = guessColumn;
            pBacktrack->triedMask   = GUESS_TRIED_FILLED;

            ApplyGuess(pPuzzleState, guessRow, guessColumn, 1);

            if (!PropagateFromCell(pPuzzleState, guessRow, guessColumn))
            {
                if (IsGridComplete(pPuzzleState))
                {
                    if (SolverValidate(pPuzzleState))
                    {
                        pPuzzleState->solved = 1;
                        return 1;
                    }
                }
                else
                {
                    depth++;
                    continue;
                }
            }

            needsBacktrack = 1;
        }

        if (needsBacktrack)
        {
            // Backtrack after a contradiction and try the next branch
            while (depth >= 0)
            {
                BacktrackData *pBacktrackData = &backtrackStack[depth];
                int tryNextFilled;

                if ((pBacktrackData->triedMask & GUESS_TRIED_FILLED) == 0)
                {
                    tryNextFilled = 1;
                    pBacktrackData->triedMask =
                        pBacktrackData->triedMask | GUESS_TRIED_FILLED;
                }
                else if ((pBacktrackData->triedMask & GUESS_TRIED_EMPTY) == 0)
                {
                    tryNextFilled = 0;
                    pBacktrackData->triedMask =
                        pBacktrackData->triedMask | GUESS_TRIED_EMPTY;
                }
                else
                {
                    depth--;
                    continue;
                }

                // Restore the saved grid before trying the alternate value.
                RestorePuzzleState(pPuzzleState, pBacktrackData);
                ApplyGuess(pPuzzleState,
                        pBacktrackData->guessRow,
                        pBacktrackData->guessColumn,
                        tryNextFilled);

                if (!PropagateFromCell(pPuzzleState,
                                    pBacktrackData->guessRow,
                                    pBacktrackData->guessColumn))
                {
                    if (IsGridComplete(pPuzzleState))
                    {
                        if (SolverValidate(pPuzzleState))
                        {
                            pPuzzleState->solved = 1;
                            return 1;
                        }
                    }
                    else
                    {
                        depth++;
                        break;
                    }
                }

                depth--;
            }

            if (depth < 0)
            {
                pPuzzleState->hasContradiction = 1;
                pPuzzleState->solverFailed = 1;

                return 0;
            }
        }
    }
}

int SolverValidate(const PuzzleState *pPuzzleState)
{
    for (uint8_t row = 0; row < pPuzzleState->height; ++row)
    {
        if (!LineMatchesClue(pPuzzleState->rowCellStates[row],
                             pPuzzleState->rowCellKnowns[row],
                             pPuzzleState->width,
                             &pPuzzleState->rowClues[row]))
        {
            return 0;
        }
    }

    for (uint8_t column = 0; column < pPuzzleState->width; ++column)
    {
        if (!LineMatchesClue(pPuzzleState->colCellStates[column],
                             pPuzzleState->colCellKnowns[column],
                             pPuzzleState->height,
                             &pPuzzleState->colClues[column]))
        {
            return 0;
        }
    }

    return 1;
}

uint16_t SolverPackSolution(const PuzzleState *pPuzzleState, uint8_t *pOutput)
{
    uint8_t rowByteCount = (uint8_t)((pPuzzleState->width + 7) / 8);

    for (uint8_t row = 0; row < pPuzzleState->height; ++row)
    {
        for (uint8_t byteIndex = 0; byteIndex < rowByteCount; ++byteIndex)
        {
            uint8_t outputByte = 0;

            for (uint8_t bitIndex = 0; bitIndex < 8; ++bitIndex)
            {
                uint8_t column = (uint8_t)(byteIndex * 8 + bitIndex);

                if (column >= pPuzzleState->width) { break; }

                if (pPuzzleState->rowCellStates[row] & GetCellBit(column))
                {
                    outputByte = outputByte | (uint8_t)(0x80u >> bitIndex);
                }
            }

            pOutput[(uint16_t)row * rowByteCount + byteIndex] = outputByte;
        }
    }

    return (uint16_t)rowByteCount * pPuzzleState->height;
}