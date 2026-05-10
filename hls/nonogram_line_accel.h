// HLS

#ifndef NONOGRAM_LINE_ACCEL_H_
#define NONOGRAM_LINE_ACCEL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef unsigned int  uint32;
typedef int           int32;

// Max line length and clue bocks
#define LINE_MAX_CELLS     32
#define CLUE_MAX_BLOCKS    16

// Total job descriptor word count
#define JOB_WORD_COUNT    32

// Offsets for job descriptor sections
#define JOB_LINE_LENGTH    0
#define JOB_BLOCK_COUNT    1
#define JOB_KNOWN_BITMAP   2
#define JOB_STATE_BITMAP   4
#define JOB_BLOCKS         6

// Offsets for output fields
#define JOB_FORCE_FILLED       26
#define JOB_POSSIBLE_FILLED    28
#define JOB_VALID_COUNT        30
#define JOB_STATUS             31

// Returns the JOB_STATUS value, 1 for success and 0 otherwise
uint32 toplevel(uint32 *ram,
                uint32 *arg1,
                uint32 *arg2,
                uint32 *arg3,
                uint32 *arg4);

#endif // NONOGRAM_LINE_ACCEL_H_
