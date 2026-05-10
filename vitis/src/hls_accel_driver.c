// HLS line-accelerator driver

#include "hls_accel_driver.h"
#include "config.h"

#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xtoplevel.h"

#include "FreeRTOS.h"
#include "task.h"

// Job descriptor word offsets shared with the HLS IP
#define JOB_LINE_LENGTH      0
#define JOB_BLOCK_COUNT      1
#define JOB_KNOWN_BITMAP     2
#define JOB_STATE_BITMAP     4
#define JOB_BLOCKS           6

// Result word offsets shared with the HLS IP
#define JOB_FORCE_FILLED       26
#define JOB_POSSIBLE_FILLED    28
#define JOB_VALID_COUNT        30
#define JOB_STATUS             31

#define JOB_WORD_COUNT    32

static uint32_t jobDesc[JOB_WORD_COUNT] __attribute__((aligned(0x20)));

static XToplevel topLevelInstance;
static int hlsReady = 0;

static void ClearJobDescriptor(void)
{
    for (int i = 0; i < JOB_WORD_COUNT; ++i)
    {
        jobDesc[i] = 0;
    }
}

static void FillJobDescriptor(uint8_t lineLength,
                              const uint8_t *pBlocks,
                              uint8_t blockCount,
                              uint32_t knownMask,
                              uint32_t stateMask)
{
    jobDesc[JOB_LINE_LENGTH]  = lineLength;
    jobDesc[JOB_BLOCK_COUNT]  = blockCount;
    jobDesc[JOB_KNOWN_BITMAP] = knownMask;
    jobDesc[JOB_STATE_BITMAP] = stateMask;

    for (uint8_t i = 0; i < blockCount && i < NONOGRAM_MAX_BLOCKS; ++i)
    {
        jobDesc[JOB_BLOCKS + i] = pBlocks[i];
    }
}

static void WaitForJobDone(void)
{
    uint32_t pollCount = 0;

    while (!XToplevel_IsDone(&topLevelInstance))
    {
        if (((++pollCount % 1024U) == 0) &&
            xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            // Yield while the hardware IP is busy so other tasks may run
            taskYIELD();
        }
    }
}

static void ReadJobResult(LineAccelResult *pResult)
{
    pResult->forceFilledMask     = jobDesc[JOB_FORCE_FILLED];
    pResult->possibleFilledMask  = jobDesc[JOB_POSSIBLE_FILLED];
    pResult->validPlacementCount = jobDesc[JOB_VALID_COUNT];
    pResult->status              = (jobDesc[JOB_STATUS] == 1U && pResult->validPlacementCount > 0U) ? 1U : 0U;
}

int HLSInit(void)
{
    int initStatus;

    // Initialise the HLS IP and point it at the shared job descriptor RAM
    initStatus = XToplevel_Initialize(&topLevelInstance,
                                      XPAR_TOPLEVEL_0_BASEADDR);

    if (initStatus != XST_SUCCESS) { return 0; }

    XToplevel_Set_ram(&topLevelInstance, (UINTPTR)jobDesc);
    hlsReady = 1;

    return 1;
}

int HLSRunLine(uint8_t length,
                const uint8_t *pBlocks,
                uint8_t blockCount,
                uint32_t knownMask,
                uint32_t stateMask,
                LineAccelResult *pResult)
{
    if (!hlsReady) { return 0; }

    // Fill the job descriptor with a line
    ClearJobDescriptor();
    FillJobDescriptor(length, pBlocks, blockCount, knownMask, stateMask);

    // Start the hardware search, then read back its output
    Xil_DCacheFlushRange((INTPTR)jobDesc, sizeof(jobDesc));
    XToplevel_Start(&topLevelInstance);
    WaitForJobDone();
    Xil_DCacheInvalidateRange((INTPTR)jobDesc, sizeof(jobDesc));

    ReadJobResult(pResult);

    return pResult->status == 1u && pResult->validPlacementCount > 0u ? 1 : 0;
}
