// Nonogram solver

#ifndef NONOGRAM_SOLVER_H_
#define NONOGRAM_SOLVER_H_

#include "config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t blocks[NONOGRAM_MAX_BLOCKS];
    uint8_t blockCount;
} ClueLine;

typedef struct {
    uint32_t seed;
    uint8_t  difficulty;
    uint8_t  width;
    uint8_t  height;

    ClueLine rowClues[NONOGRAM_MAX_DIM];
    ClueLine colClues[NONOGRAM_MAX_DIM];

    // State bits (1 = filled) and known bits (1 = cell solved)
    uint32_t rowCellStates[NONOGRAM_MAX_DIM];
    uint32_t rowCellKnowns[NONOGRAM_MAX_DIM];
    uint32_t colCellStates[NONOGRAM_MAX_DIM];
    uint32_t colCellKnowns[NONOGRAM_MAX_DIM];

    uint32_t rowValidPlacementCount[NONOGRAM_MAX_DIM];
    uint32_t colValidPlacementCount[NONOGRAM_MAX_DIM];

    int hasContradiction;
    int solverFailed;
    int solved;
} PuzzleState;

// Returns 1 when the stream is valid
int SolverLoadClues(PuzzleState *pPuzzleState,
                    uint32_t seed,
                    uint8_t difficulty,
                    uint8_t width,
                    uint8_t height,
                    const uint8_t *pStream,
                    uint16_t streamLength);

// Returns 1 when the puzzle is solved in time and 0 otherwise
int SolverSolve(PuzzleState *pPuzzleState, uint32_t maxTimeMs);

// Returns 1 when the puzzle state is valid and 0 otherwise
int SolverValidate(const PuzzleState *pPuzzleState);

// Packs rows into bitmap format and returns the bitmap size
uint16_t SolverPackSolution(const PuzzleState *pPuzzleState, uint8_t *pOutput);

#ifdef __cplusplus
}
#endif

#endif
