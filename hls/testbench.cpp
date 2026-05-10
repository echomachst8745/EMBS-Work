// HLS test bench

#include "nonogram_line_accel.h"
#include <cstdio>
#include <cstring>

static uint32 ram[JOB_WORD_COUNT];

struct TestCase {
    const char *testName;
    uint8_t lineLength;
    uint8_t blockCount;
    uint8_t blocks[CLUE_MAX_BLOCKS];
    uint32 knownMask;
    uint32 stateMask;
    uint32 expectedForceFilled;
    uint32 expectedPossibleFilled;
    uint32 expectedValidCount;
    uint32 expectedStatus;
};

static int RunTestCase(const TestCase &test)
{
    memset(ram, 0, sizeof(ram));
    ram[JOB_LINE_LENGTH]  = test.lineLength;
    ram[JOB_BLOCK_COUNT]  = test.blockCount;
    ram[JOB_KNOWN_BITMAP] = test.knownMask;
    ram[JOB_STATE_BITMAP] = test.stateMask;

    for (int i = 0; i < test.blockCount && i < CLUE_MAX_BLOCKS; ++i)
    {
        ram[JOB_BLOCKS + i] = test.blocks[i];
    }

    uint32 returnStatus = toplevel(ram,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr);

    bool passed = (ram[JOB_FORCE_FILLED] == test.expectedForceFilled) &&
                  (ram[JOB_POSSIBLE_FILLED] == test.expectedPossibleFilled) &&
                  (ram[JOB_VALID_COUNT] == test.expectedValidCount) &&
                  (ram[JOB_STATUS] == test.expectedStatus) &&
                  (returnStatus == test.expectedStatus);

    printf("Test: (%s): %s\r\n",
           test.testName,
           passed ? "PASS" : "FAIL");

    return passed ? 0 : 1;
}

int main()
{
    const TestCase tests[] = {
        // No clue means the line must be empty
        {"Empty clue", 5, 0, {0}, 0, 0, 0b00000, 0b00000, 1, 1},

        // A 7-cell block in a 10-cell line has four placements
        // Bits 3..6 are filled in all of them
        {"Single block overlap", 10, 1, {7}, 0, 0, 0b0001111000, 0b1111111111, 4, 1},

        // Clue blocks of "2 2" in a 5 cell line only has 1 placement
        {"Exact multi-block fit", 5, 2, {2, 2}, 0, 0, 0b11011, 0b11011, 1, 1},

        // Clue blocks of "2 2" leaves bit 3 empty, so this contradicts it
        {"Known filled conflict", 5, 2, {2, 2}, 0b00100, 0b00100, 0, 0, 0, 0},

        // The mandatory clue cells plus separator do not fit in the line
        {"Clue too large", 5, 2, {3, 3}, 0, 0, 0, 0, 0, 0},

        // Special case for a no-clue line with a filled cell already known
        {"Empty clue conflict", 8, 0, {0}, 0b0001000, 0b0001000, 0, 0, 0, 0},

        // 32-bit mask path. A length 5 clue block can start at 0..27
        {"32-bit edge", 32, 1, {5}, 0, 0, 0x00000000, 0xFFFFFFFF, 28, 1},
    };

    int errorCount = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    {
        errorCount += RunTestCase(tests[i]);
    }

    return errorCount == 0 ? 0 : 1;
}
