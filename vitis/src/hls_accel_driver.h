// HLS line-accelerator driver

#ifndef HLS_ACCEL_DRIVER_H_
#define HLS_ACCEL_DRIVER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t forceFilledMask;       // bits forced filled in every valid arrangement
    uint32_t possibleFilledMask;    // bits filled in some valid arrangement
    uint32_t validPlacementCount;   // valid arrangements, 0 = contradiction
    uint32_t status;                // 1 = ok, 0 = contradiction/error
} LineAccelResult;

// Returns 1 when the IP is initialized and 0 otherwise
int HLSInit(void);

// Returns 1 when the line is solved and 0 if there's a contradiction
int HLSRunLine(uint8_t length,
                const uint8_t *pBlocks,
                uint8_t blockCount,
                uint32_t knownMask,
                uint32_t stateMask,
                LineAccelResult *pResult);

#ifdef __cplusplus
}
#endif

#endif
