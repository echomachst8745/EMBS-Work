// Encode and decode EMBS Nonogram server messages

#ifndef SERVER_PROTOCOL_H_
#define SERVER_PROTOCOL_H_

#include "config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Message IDs
#define MESSAGE_REQUEST_INFO_ID       0x01
#define MESSAGE_PUZZLE_INFO_ID        0x02
#define MESSAGE_REQUEST_CHUNK_ID      0x03
#define MESSAGE_CHUNK_DATA_ID         0x04
#define MESSAGE_SUBMIT_SOLUTION_ID    0x05
#define MESSAGE_RESULT_ID             0x06
#define MESSAGE_ERROR_ID              0xFF

// Puzzle difficulty tiers
#define TIER_CUSTOM    0b00
#define TIER_EASY      0b01
#define TIER_MEDIUM    0b10
#define TIER_HARD      0b11

uint8_t SizeIndexToGrid(uint8_t sizeIndex);

// Builders return total length in bytes written
uint16_t BuildRequestInfo  (uint8_t *buf, uint32_t seed, uint8_t difficulty);
uint16_t BuildRequestChunk (uint8_t *buf, uint32_t seed, uint8_t difficulty,
                            uint8_t chunkId);
uint16_t BuildSubmitSolution(uint8_t *buf, uint32_t seed, uint8_t difficulty,
                             const uint8_t *bitmap, uint16_t bitmapLength);

typedef struct {
    uint32_t seed;
    uint8_t  difficulty;
    uint8_t  width;
    uint8_t  height;
    uint8_t  chunkCount;
    uint16_t clueByteLength;
} PuzzleInfoMessage;

typedef struct {
    uint32_t seed;
    uint8_t  difficulty;
    uint8_t  chunkId;
    uint8_t  chunkCount;
    uint16_t offset;
    uint16_t dataLength;
    const uint8_t *pData;
} ChunkDataMessage;

typedef struct {
    uint32_t seed;
    uint8_t  difficulty;
    uint8_t  status;        // 0 = incorrect, 1 = correct, 2 = error
    uint32_t solveTimeMs;
} ResultMessage;

// Parsers return 1 on success and 0 otherwise
int ParsePuzzleInfo (const uint8_t *buf, uint16_t bufferLength, PuzzleInfoMessage *pMessage);
int ParseChunkData  (const uint8_t *buf, uint16_t bufferLength, ChunkDataMessage *pMessage);
int ParseResult     (const uint8_t *buf, uint16_t bufferLength, ResultMessage *pMessage);
int ParseError      (const uint8_t *buf, uint16_t bufferLength, char *pOutputText, uint16_t outputMaxLength);

#ifdef __cplusplus
}
#endif

#endif
