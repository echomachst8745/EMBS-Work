// HDMI rendering of the puzzle,

#ifndef GRAPHICS_H_
#define GRAPHICS_H_

#include "nonogram_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 if the graphics initialised successfully, otherwise 0
int GraphicsInit(void);

void GraphicsRender(const PuzzleState *pPuzzleState);

#ifdef __cplusplus
}
#endif

#endif
