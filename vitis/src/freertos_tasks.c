// FreeRTOS tasks

#include "freertos_tasks.h"
#include "config.h"
#include "nonogram_solver.h"
#include "server_protocol.h"
#include "graphics.h"
#include "hls_accel_driver.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "xil_printf.h"
#include "xuartps_hw.h"
#include "xparameters.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Solver command sent by the network task
typedef struct {
    uint32_t jobId;
} SolverJob;

// User selected puzzle parameters
typedef struct {
    uint8_t  tier;
    uint8_t  sizeIndex;
    uint32_t seed;
} PuzzleParams;

// Solver result data passed back to the network task
typedef struct {
    uint32_t jobId;
    uint32_t seed;
    uint8_t  difficulty;
    int      solverSucceeded;
    int      localSolutionValid;
    uint16_t bitmapLength;
    uint8_t  bitmap[NONOGRAM_MAX_DIM * (NONOGRAM_MAX_DIM / 8)];
} SubmitJob;

// Queues for communication between tasks
static QueueHandle_t     puzzleParamsQueue;
static QueueHandle_t     solverQueue;
static QueueHandle_t     submitQueue;

// Semaphores for redraws, completion, and puzzle state access
static SemaphoreHandle_t graphicsSemaphore;
static SemaphoreHandle_t doneSemaphore;
static SemaphoreHandle_t puzzleMutex;

// Shared puzzle state displayed and solved by different tasks
static PuzzleState puzzleState;
static int         networkSocket = -1;
static uint32_t    nextSolverJobId = 1;

static char UartGetChar(void)
{
    while (!XUartPs_IsReceiveData(STDIN_BASEADDRESS))
    {
        vTaskDelay(20 / portTICK_RATE_MS);
    }

    return (char)XUartPs_RecvByte(STDIN_BASEADDRESS);
}

static void UartReadLine(char *buf, int maxLength)
{
    int charCount = 0;

    while (charCount < maxLength - 1)
    {
        char inputChar = UartGetChar();

        if (inputChar == '\r' || inputChar == '\n')
        {
            xil_printf("\r\n");
            break;
        }

        if (inputChar == 0x08 || inputChar == 0x7F)
        {
            if (charCount > 0)
            {
                charCount--;
                xil_printf("\b \b");
            }
            continue;
        }

        // Ingore non-characters
        if (inputChar < 32 || inputChar > 126) { continue; }

        buf[charCount++] = inputChar;
        XUartPs_SendByte(STDOUT_BASEADDRESS, (u8)inputChar);
    }

    buf[charCount] = '\0';
}

// Returns 1 if a u32 was successfully parsed otherwise 0
static int ParseU32(const char *string, u32 *pOutU32)
{
    // Ignore spaces or tabs at the start of the string
    while (*string == ' ' || *string == '\t')
    {
        string++;
    }
    if (!*string) { return 0; }

    char *pEnd = NULL;
    u32 value = (u32)strtoimax(string, &pEnd, 10);

    if (pEnd == string) { return 0; }

    // Ignore trailing spaces or tabs
    while (*pEnd == ' ' || *pEnd == '\t')
    {
        pEnd++;
    }

    // If we dont reach the end of the string then parsing fails
    if (*pEnd != '\0') { return 0; }

    *pOutU32 = value;
    return 1;
}

// Prompt until the user gives a valid integer within a range
static u32 PromptU32(const char *promptString, u32 minValue, u32 maxValue)
{
    char line[256];

    while (1)
    {
        xil_printf("%s", promptString);
        UartReadLine(line, sizeof(line));

        u32 value;
        if (!ParseU32(line, &value) || value < minValue || value > maxValue)
        {
            printf("Invalid input, expected a value in range [%u-%u]\r\n", (unsigned int)minValue, (unsigned int)maxValue);
            continue;
        }

        return value;
    }
}

static uint8_t MakeDifficulty(uint8_t tier, uint8_t sizeIndex)
{
    return (uint8_t)(((tier & 0b11) << 4) | (sizeIndex & 0xF));
}

// Returns 1 if successful and 0 otherwise
static int NetworkSocketInit(void)
{
    networkSocket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (networkSocket < 0) { return 0; }

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_port        = htons(NONOGRAM_SERVER_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (lwip_bind(networkSocket, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        lwip_close(networkSocket);
        networkSocket = -1;
        return 0;
    }

    return 1;
}

static void NetworkSetReceiveTimeout(uint32_t timeoutMs)
{
    struct timeval receiveTimeout;
    receiveTimeout.tv_sec  = timeoutMs / 1000;
    receiveTimeout.tv_usec = (timeoutMs % 1000) * 1000;

    lwip_setsockopt(networkSocket, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
}

// Returns 1 if successfully sent and 0 otherwise
static int NetworkSend(const uint8_t *buf, uint16_t bufferLength)
{
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(NONOGRAM_SERVER_PORT);
    dest.sin_addr.s_addr = htonl(
        ((u32_t)NONOGRAM_SERVER_IP_A << 24) |
        ((u32_t)NONOGRAM_SERVER_IP_B << 16) |
        ((u32_t)NONOGRAM_SERVER_IP_C <<  8) |
        ((u32_t)NONOGRAM_SERVER_IP_D));

    return lwip_sendto(networkSocket, buf, bufferLength, 0,
                       (struct sockaddr *)&dest, sizeof(dest)) < 0 ? 0 : 1;
}

static int NetworkReceive(uint8_t *buf, int maxLength, uint32_t timeoutMs)
{
    NetworkSetReceiveTimeout(timeoutMs);

    struct sockaddr_in src;
    socklen_t srcLength = sizeof(src);

    return lwip_recvfrom(networkSocket, buf, maxLength, 0,
                         (struct sockaddr *)&src, &srcLength);
}

// Wait for a specific message id. Returns 1 if message successfully received and 0 otherwise
static int NetworkWaitForMessage(uint8_t expectedMessageId, uint8_t *buf, int maxLength,
                                 int *pOutputLength, uint32_t timeoutMs)
{
    TickType_t start = xTaskGetTickCount();

    while (1)
    {
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed = (uint32_t)((now - start) * portTICK_RATE_MS);

        if (elapsed >= timeoutMs) { return 0; }

        int receiveLength = NetworkReceive(buf, maxLength, timeoutMs - elapsed);
        if (receiveLength <= 0) { return 0; }

        if (buf[0] == expectedMessageId)
        {
            *pOutputLength = receiveLength;

            return 1;
        }
        if (buf[0] == MESSAGE_ERROR_ID)
        {
            char text[256] = {0};
            ParseError(buf, receiveLength, text, sizeof(text));
            xil_printf("Server error: %s\r\n", text);

            return 0;
        }
    }
}

static void RequestRedraw(void)
{
    xSemaphoreGive(graphicsSemaphore);
}

static void SignalDone(void)
{
    xSemaphoreGive(doneSemaphore);
}

static void DrainSubmitQueue(void)
{
    SubmitJob staleSubmitJob;

    while (xQueueReceive(submitQueue, &staleSubmitJob, 0) == pdTRUE)
    {
        xil_printf("Dropping stale solver job %lu\r\n",
                   (unsigned long)staleSubmitJob.jobId);
    }
}

static void ReceiveSubmitJob(uint32_t jobId, SubmitJob *pSubmitJob)
{
    while (1)
    {
        SubmitJob candidate;
        xQueueReceive(submitQueue, &candidate, portMAX_DELAY);

        if (candidate.jobId == jobId)
        {
            *pSubmitJob = candidate;
            
            return;
        }
    }
}

static void LoadPuzzleWithMutex(uint32_t seed, uint8_t difficulty,
                                uint8_t width, uint8_t height,
                                const uint8_t *pClues, uint16_t clueLength,
                                int *pLoaded)
{
    xSemaphoreTake(puzzleMutex, portMAX_DELAY);

    *pLoaded = SolverLoadClues(&puzzleState, seed, difficulty, width, height, pClues, clueLength);

    xSemaphoreGive(puzzleMutex);
}

// Input can block here while network, graphics, and solver tasks keep running
static void UserInterfaceTask()
{
    xil_printf("\r\n-=== Nonogram Solver ===-");

    while (1)
    {
        PuzzleParams puzzleParams;
        u32 inputValue;

        xil_printf("\r\nTier: 0=Custom, 1=Easy, 2=Medium, 3=Hard\r\n");
        xil_printf("Size index: 0=5x5, 1=6x6, 2=7x7, 3=8x8, 4=10x10, 5=12x12,\r\n");
        xil_printf("            6=14x14, 7=16x16, 8=18x18, 9=20x20, 10=22x22,\r\n");
        xil_printf("            11=24x24, 12=26x26, 13=28x28, 14=30x30, 15=32x32\r\n\r\n");

        inputValue = PromptU32("Tier (0-3): ", 0, 3);
        puzzleParams.tier = (uint8_t)inputValue;

        int minSizeIndex = 0;
        int maxSizeIndex = 15;
        if (puzzleParams.tier == (uint8_t)1)      { minSizeIndex = 0; maxSizeIndex = 4; }
        else if (puzzleParams.tier == (uint8_t)2) { minSizeIndex = 4; maxSizeIndex = 7; }
        else if (puzzleParams.tier == (uint8_t)3) { minSizeIndex = 7; maxSizeIndex = 9; }

        char sizePromptString[256];
        snprintf(sizePromptString, sizeof(sizePromptString), "Size index (%d-%d): ", minSizeIndex, maxSizeIndex);
        inputValue = PromptU32(sizePromptString, minSizeIndex, maxSizeIndex);
        puzzleParams.sizeIndex = (uint8_t)inputValue;

        inputValue = PromptU32("Seed (0-4294967295): ", 0, 4294967295U);
        puzzleParams.seed = inputValue;

        xQueueSend(puzzleParamsQueue, &puzzleParams, portMAX_DELAY);
        xSemaphoreTake(doneSemaphore, portMAX_DELAY);
    }
}

static uint8_t receiveBuffer[2048];
static uint8_t clueBuffer[1024];

static void RunPuzzleRound(const PuzzleParams *pPuzzleParams)
{
    uint8_t difficulty = MakeDifficulty(pPuzzleParams->tier, pPuzzleParams->sizeIndex);
    uint8_t transmitBuffer[16];
    int receiveLength = 0;

    // Request for the specified puzzle
    // REQUEST_INFO -> PUZZLE_INFO
    RequestRedraw();

    uint16_t transmitLength = BuildRequestInfo(transmitBuffer, pPuzzleParams->seed, difficulty);
    if (!NetworkSend(transmitBuffer, transmitLength))
    {
        xil_printf("Send REQUEST_INFO failed\r\n");
        return;
    }

    if (!NetworkWaitForMessage(MESSAGE_PUZZLE_INFO_ID, receiveBuffer, sizeof(receiveBuffer),
                              &receiveLength, 5000))
    {
        xil_printf("No PUZZLE_INFO received\r\n");
        return;
    }

    PuzzleInfoMessage puzzleInfo;
    if (!ParsePuzzleInfo(receiveBuffer, receiveLength, &puzzleInfo))
    {
        xil_printf("Error parsing PUZZLE_INFO\r\n");
        return;
    }

    printf("Puzzle: Size=%dx%d, Difficulty=0x%02X, Seed=%u\r\n",
           puzzleInfo.width, puzzleInfo.height, puzzleInfo.difficulty, (unsigned int)puzzleInfo.seed);

    // REQUEST_CHUNK -> CHUNK_DATA
    RequestRedraw();

    transmitLength = BuildRequestChunk(transmitBuffer, puzzleInfo.seed, puzzleInfo.difficulty, 0);
    if (!NetworkSend(transmitBuffer, transmitLength))
    {
        xil_printf("Send REQUEST_CHUNK failed\r\n");
        return;
    }

    if (!NetworkWaitForMessage(MESSAGE_CHUNK_DATA_ID, receiveBuffer, sizeof(receiveBuffer),
                              &receiveLength, 5000))
    {
        xil_printf("No CHUNK_DATA received\r\n");
        return;
    }

    ChunkDataMessage chunkData;
    if (!ParseChunkData(receiveBuffer, receiveLength, &chunkData) ||
        chunkData.dataLength > sizeof(clueBuffer))
    {
        xil_printf("Error parsing CHUNK_DATA\r\n");
        return;
    }

    memcpy(clueBuffer, chunkData.pData, chunkData.dataLength);

    // Solver
    int loaded = 0;
    LoadPuzzleWithMutex(puzzleInfo.seed, puzzleInfo.difficulty, puzzleInfo.width, puzzleInfo.height,
                        clueBuffer, chunkData.dataLength, &loaded);

    if (!loaded)
    {
        xil_printf("Error loading puzzle\r\n");
        return;
    }

    RequestRedraw();

    DrainSubmitQueue();

    SolverJob solverJob;
    solverJob.jobId = nextSolverJobId++;
    if (nextSolverJobId == 0) { nextSolverJobId = 1; }

    // Queue the solve job
    xQueueSend(solverQueue, &solverJob, portMAX_DELAY);

    SubmitJob submitJob;
    ReceiveSubmitJob(solverJob.jobId, &submitJob);

    if (!submitJob.solverSucceeded || !submitJob.localSolutionValid)
    {
        xil_printf("Solver failed or timed out\r\n");
        RequestRedraw();
        return;
    }
    RequestRedraw();

    // SUBMIT_SOLUTION -> RESULT
    RequestRedraw();

    static uint8_t submitBuffer[6 + NONOGRAM_MAX_DIM * (NONOGRAM_MAX_DIM / 8)];
    uint16_t submitLength = BuildSubmitSolution(submitBuffer, submitJob.seed, submitJob.difficulty,
                                                     submitJob.bitmap, submitJob.bitmapLength);
    if (!NetworkSend(submitBuffer, submitLength))
    {
        xil_printf("Submit failed\r\n");
        return;
    }

    if (!NetworkWaitForMessage(MESSAGE_RESULT_ID, receiveBuffer, sizeof(receiveBuffer),
                              &receiveLength, 10000))
    {
        xil_printf("No RESULT received\r\n");
        return;
    }

    ResultMessage resultMessage;
    if (!ParseResult(receiveBuffer, receiveLength, &resultMessage))
    {
        xil_printf("Error parsing result\r\n");
        return;
    }

    if (resultMessage.status == 1)
    {
        printf("Server responds CORRECT (%u ms)\r\n", (unsigned int)resultMessage.solveTimeMs);
    }
    else if (resultMessage.status == 0)
    {
        if (submitJob.localSolutionValid)
        {
            printf("Server responds INCORRECT (%u ms): Local solution was validated but server expected a different bitmap\r\n",
                   (unsigned int)resultMessage.solveTimeMs);
        }
        else if (!submitJob.solverSucceeded)
        {
            printf("Server responds INCORRECT (%u ms): Solver did not find a locally validated solution\r\n",
                   (unsigned int)resultMessage.solveTimeMs);
        }
        else
        {
            printf("Server responds INCORRECT (%u ms): Local validation was incorrect\r\n",
                   (unsigned int)resultMessage.solveTimeMs);
        }
    }

    RequestRedraw();
}

static void NetworkTask()
{
    if (!NetworkSocketInit())
    {
        xil_printf("Socket init failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        PuzzleParams puzzleParams;
        // FreeRTOS blocks this task until the UI has chosen a puzzle
        if (xQueueReceive(puzzleParamsQueue, &puzzleParams, portMAX_DELAY) != pdTRUE) { continue; }

        RunPuzzleRound(&puzzleParams);
        SignalDone();
    }
}

static void SolverTask()
{
    while (1)
    {
        SolverJob solverCommand;
        // Solver runs in its own task so searches dont freeze the UI or HDMI graphics
        if (xQueueReceive(solverQueue, &solverCommand, portMAX_DELAY) != pdTRUE) { continue; }

        RequestRedraw();

        TickType_t solveStart = xTaskGetTickCount();
        int solverSucceeded = SolverSolve(&puzzleState, SOLVER_MAX_TIME_MS);
        uint32_t solveTimeMs = (uint32_t)((xTaskGetTickCount() - solveStart) * portTICK_RATE_MS);

        RequestRedraw();

        SubmitJob submitJob;
        memset(&submitJob, 0, sizeof(submitJob));

        xSemaphoreTake(puzzleMutex, portMAX_DELAY);
        submitJob.jobId              = solverCommand.jobId;
        submitJob.seed               = puzzleState.seed;
        submitJob.difficulty         = puzzleState.difficulty;
        submitJob.solverSucceeded    = solverSucceeded;
        submitJob.localSolutionValid = SolverValidate(&puzzleState);
        submitJob.bitmapLength       = SolverPackSolution(&puzzleState, submitJob.bitmap);
        xSemaphoreGive(puzzleMutex);

        printf("\r\nSolver %s in %u ms\r\n",
                   solverSucceeded ? "OK" : "FAILED",
                   (unsigned int)solveTimeMs);

        xQueueSend(submitQueue, &submitJob, portMAX_DELAY);
    }
}

static void GraphicsTask()
{
    PuzzleState snapshot;

    while (1)
    {
        xSemaphoreTake(graphicsSemaphore, 1000 / portTICK_RATE_MS);

        xSemaphoreTake(puzzleMutex, portMAX_DELAY);
        memcpy(&snapshot, &puzzleState, sizeof(snapshot));
        xSemaphoreGive(puzzleMutex);

        GraphicsRender(&snapshot);
    }
}

void ApplicationTask()
{
    // Separate queues/semaphores let input, networking,
    // HLS and graphics be scheduled as seperately running tasks
    puzzleParamsQueue = xQueueCreate(2, sizeof(PuzzleParams));
    solverQueue       = xQueueCreate(2, sizeof(SolverJob));
    submitQueue       = xQueueCreate(2, sizeof(SubmitJob));
    graphicsSemaphore = xSemaphoreCreateBinary();
    doneSemaphore     = xSemaphoreCreateBinary();
    puzzleMutex       = xSemaphoreCreateMutex();

    if (!puzzleParamsQueue || !solverQueue || !submitQueue ||
        !graphicsSemaphore || !doneSemaphore || !puzzleMutex)
    {
        xil_printf("Queue and semaphore allocations failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    // Tasks for graphics rendering, solving, network communication and UI
    xTaskCreate(GraphicsTask,      "graphics", STACKSIZE_GRAPHICS, NULL, PRIORITY_GRAPHICS, NULL);
    xTaskCreate(SolverTask,        "solver",   STACKSIZE_SOLVER,   NULL, PRIORITY_SOLVER,   NULL);
    xTaskCreate(NetworkTask,       "network",  STACKSIZE_NETWORK,  NULL, PRIORITY_NETWORK,  NULL);
    xTaskCreate(UserInterfaceTask, "ui",       STACKSIZE_UI,       NULL, PRIORITY_UI,       NULL);

    vTaskDelete(NULL);
}
