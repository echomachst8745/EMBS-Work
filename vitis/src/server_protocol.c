// Encode and decode EMBS Nonogram server messages

#include "server_protocol.h"
#include <string.h>

static void BufferFillU32BigEndian(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)value;
}

static uint16_t GetU16BigEndian(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static uint32_t GetU32BigEndian(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

static const uint8_t SERVER_PROTOCOL_SIZE_INDEX_TO_GRID_TABLE[16] = {
    5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32
};

uint8_t SizeIndexToGrid(uint8_t sizeIndex)
{
    if (sizeIndex >= 16) { return 0; }

    return SERVER_PROTOCOL_SIZE_INDEX_TO_GRID_TABLE[sizeIndex];
}

uint16_t BuildRequestInfo(uint8_t *buf, uint32_t seed, uint8_t difficulty)
{
    buf[0] = MESSAGE_REQUEST_INFO_ID;
    BufferFillU32BigEndian(&buf[1], seed);
    buf[5] = difficulty;

    return 6;
}

uint16_t BuildRequestChunk(uint8_t *buf, uint32_t seed, uint8_t difficulty, uint8_t chunkId)
{
    buf[0] = MESSAGE_REQUEST_CHUNK_ID;
    BufferFillU32BigEndian(&buf[1], seed);
    buf[5] = difficulty;
    buf[6] = chunkId;

    return 7;
}

uint16_t BuildSubmitSolution(uint8_t *buf, uint32_t seed, uint8_t difficulty,
                             const uint8_t *pBitmap, uint16_t bitmapLength)
{
    buf[0] = MESSAGE_SUBMIT_SOLUTION_ID;
    BufferFillU32BigEndian(&buf[1], seed);
    buf[5] = difficulty;

    if (bitmapLength)
    {
        memcpy(&buf[6], pBitmap, bitmapLength);
    }

    return (uint16_t)(6 + bitmapLength);
}

int ParsePuzzleInfo(const uint8_t *buf, uint16_t bufferLength,
                    PuzzleInfoMessage *pMessage)
{
    if (bufferLength < 11 || buf[0] != MESSAGE_PUZZLE_INFO_ID) { return 0; }

    pMessage->seed           = GetU32BigEndian(&buf[1]);
    pMessage->difficulty     = buf[5];
    pMessage->width          = buf[6];
    pMessage->height         = buf[7];
    pMessage->chunkCount     = buf[8];
    pMessage->clueByteLength = GetU16BigEndian(&buf[9]);

    return 1;
}

int ParseChunkData(const uint8_t *buf, uint16_t bufferLength,
                   ChunkDataMessage *pMessage)
{
    if (bufferLength < 12 || buf[0] != MESSAGE_CHUNK_DATA_ID) { return 0; }

    pMessage->seed        = GetU32BigEndian(&buf[1]);
    pMessage->difficulty  = buf[5];
    pMessage->chunkId     = buf[6];
    pMessage->chunkCount  = buf[7];
    pMessage->offset      = GetU16BigEndian(&buf[8]);
    pMessage->dataLength  = GetU16BigEndian(&buf[10]);

    if ((uint16_t)(12 + pMessage->dataLength) > bufferLength) { return 0; }

    pMessage->pData = &buf[12];

    return 1;
}

int ParseResult(const uint8_t *buf, uint16_t bufferLength,
                ResultMessage *pMessage)
{
    if (bufferLength < 11 || buf[0] != MESSAGE_RESULT_ID) { return 0; }

    pMessage->seed        = GetU32BigEndian(&buf[1]);
    pMessage->difficulty  = buf[5];
    pMessage->status      = buf[6];
    pMessage->solveTimeMs = GetU32BigEndian(&buf[7]);

    return 1;
}

int ParseError(const uint8_t *buf, uint16_t bufferLength,
               char *pOutputText, uint16_t outputMaxLength)
{
    if (bufferLength < 8 || buf[0] != MESSAGE_ERROR_ID) { return 0; }

    uint8_t textLength = buf[7];
    if ((uint16_t)(8 + textLength) > bufferLength) { return 0; }
    if (outputMaxLength == 0) { return 1; }

    uint16_t copyLength = textLength;
    if (copyLength > (uint16_t)(outputMaxLength - 1))
    {
        copyLength = (uint16_t)(outputMaxLength - 1);
    }

    memcpy(pOutputText, &buf[8], copyLength);
    pOutputText[copyLength] = '\0';

    return 1;
}
