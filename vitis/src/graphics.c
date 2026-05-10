// HDMI rendering

#include "graphics.h"
#include "config.h"
#include "graphic_fonts.h"

#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"

#include "display_ctrl.h"
#include "vga_modes.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Frame buffers, one is displayed while the other is drawn and swap between them
static u32 frameA[HDMI_NUM_PIXELS] __attribute__((aligned(0x20)));
static u32 frameB[HDMI_NUM_PIXELS] __attribute__((aligned(0x20)));

// Pointers to and index of active buffer to draw to
static u32 *pActiveFB[2] = { frameA, frameB };
static int activeFBIndex = 0;

static DisplayCtrl dispCtrl; // Display driver struct
static int graphicsReady = 0; // Flag to show graphics initialised successfully

// Macros for displayed colours
#define RGB(r,g,b)    (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define COLOUR_BG          RGB(0xFF,0xFF,0xFF)
#define COLOUR_GRID        RGB(0x60,0x60,0x60)
#define COLOUR_FILLED      RGB(0x00,0x00,0x00)
#define COLOUR_EMPTY       RGB(0xFF,0xFF,0xFF)
#define COLOUR_UNSOLVED    RGB(0xB0,0xB0,0xB0)
#define COLOUR_TEXT        RGB(0x00,0x00,0x00)

#define CLUE_FONT_DIGIT_ADVANCE_PIXELS   9
#define CLUE_FONT_DIGIT_HEIGHT_PIXELS    11
#define CLUE_FONT_DIGIT_BASELINE_PIXELS  10
#define CLUE_GRID_GAP_PIXELS             4
#define CLUE_BLOCK_GAP_PIXELS            4
#define CLUE_LINE_HEIGHT_PIXELS          18

// Struct for puzzle layout
typedef struct {
    int x, y;             // Top left of the grid
    int cellSize;         // Cell pixel size
    int gridWidth;        // Number of columns
    int gridHeight;       // Number of rows
    int rowClueWidth;     // Width for row clues
    int colClueHeight;    // Height for column clues
} PuzzleLayout;

static inline void SetPixel(u32 *buf, int x, int y, u32 pixelColour)
{
    if (x >= 0 && y >= 0 && x < HDMI_WIDTH && y < HDMI_HEIGHT)
    {
        buf[y * HDMI_WIDTH + x] = pixelColour;
    }
}

static void FillRectangle(u32 *buf, int x, int y, int w, int h, u32 colour)
{
    if (x < 0 || y < 0) { return; }

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x1 > HDMI_WIDTH || y1 > HDMI_HEIGHT) { return; }

    for (int i = y0; i < y1; ++i)
    {
        u32 *pRow = &buf[i * HDMI_WIDTH];

        for (int j = x0; j < x1; ++j)
        {
            pRow[j] = colour;
        }
    }
}

static void DrawChar(u32 *buf, int x, int y, char ch, u32 charColour, const GFXfont *pGfxFont,
                     uint8_t *outCharWidth, uint8_t *outCharHeight)
{
    const GFXglyph *glyph = &pGfxFont->glyph[ch - pGfxFont->first];
    const uint8_t *bitmap = &pGfxFont->bitmap[glyph->bitmapOffset];

    const uint8_t glyphHeight = glyph->height;
    const uint8_t glyphWidth = glyph->width;

    uint16_t bitIndex = 0;

    for (int row = 0; row < glyphHeight; ++row)
    {
        for (int col = 0; col < glyphWidth; ++col)
        {
            uint8_t byte = bitmap[bitIndex / 8];
            uint8_t mask = 0x80 >> (bitIndex % 8);

            if (byte & mask)
            {
                SetPixel(buf,
                            x + glyph->xOffset + col,
                            y + glyph->yOffset + row,
                            charColour);
            }

            bitIndex++;
        }
    }

    *outCharHeight = glyphHeight;
    *outCharWidth = glyph->xAdvance;
}

static void DrawString(u32 *buf, int x, int y, const char *s, u32 stringColour, const GFXfont *pGfxFont)
{
    int charX = x;

    for (; *s; ++s)
    {
        uint8_t charWidth, charHeight;

        DrawChar(buf, charX, y, *s, stringColour, pGfxFont, &charWidth, &charHeight);
        charX += charWidth;
    }
}

int GraphicsInit(void)
{
    int displayInitStatus = DisplayInitialize(&dispCtrl,
                                              XPAR_HDMI_AXI_VDMA_0_BASEADDR,
                                              XPAR_XVTC_0_BASEADDR,
                                              XPAR_HDMI_AXI_DYNCLK_0_BASEADDR,
                                              (void **)pActiveFB, FRAME_STRIDE);

    if (displayInitStatus != XST_SUCCESS) { return 0; }

    DisplaySetMode(&dispCtrl, &VMODE_1440x900);
    DisplayStart(&dispCtrl);

    // Fill both frames with background colour
    FillRectangle(frameA, 0, 0, HDMI_WIDTH, HDMI_HEIGHT, COLOUR_BG);
    FillRectangle(frameB, 0, 0, HDMI_WIDTH, HDMI_HEIGHT, COLOUR_BG);
    Xil_DCacheFlush();

    graphicsReady = 1;
    return 1;
}

static void SwapFrames(void)
{
    DisplayChangeFrame(&dispCtrl, activeFBIndex);
    DisplayWaitForSync(&dispCtrl);
    activeFBIndex = 1 - activeFBIndex;
}

static PuzzleLayout SetupPuzzleLayout(const PuzzleState *pPuzzleState)
{
    PuzzleLayout puzzleLayout;

    puzzleLayout.gridWidth = pPuzzleState->width;
    puzzleLayout.gridHeight = pPuzzleState->height;

    int maxRowClueStringSize = 1, maxColClueStringSize = 1;
    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        if (pPuzzleState->rowClues[row].blockCount > maxRowClueStringSize)
        {
            maxRowClueStringSize = pPuzzleState->rowClues[row].blockCount;
        }
    }
    for (int col = 0; col < pPuzzleState->width; ++col)
    {
        if (pPuzzleState->colClues[col].blockCount > maxColClueStringSize)
        {
            maxColClueStringSize = pPuzzleState->colClues[col].blockCount;
        }
    }

    puzzleLayout.rowClueWidth  = maxRowClueStringSize * 20 + 8; // 20 pixel width for each char
    puzzleLayout.colClueHeight = maxColClueStringSize * 18 + 8; // 18 pixel height for each char

    const int screenMargin = 50;

    // Calculate available width and height for puzzle grid
    int availableWidth  = HDMI_WIDTH - 2 * screenMargin - puzzleLayout.rowClueWidth;
    int availableHeight = HDMI_HEIGHT - 2 * screenMargin - puzzleLayout.colClueHeight;

    // Take the min of calculated cell width and height
    int cellWidth  = availableWidth / pPuzzleState->width;
    int cellHeight = availableHeight / pPuzzleState->height;
    int cellSize  = cellWidth < cellHeight ? cellWidth : cellHeight;
    puzzleLayout.cellSize = cellSize;
    
    puzzleLayout.x = screenMargin + puzzleLayout.rowClueWidth;
    puzzleLayout.y = screenMargin + puzzleLayout.colClueHeight;

    return puzzleLayout;
}

static void DrawPuzzleGrid(u32 *buf, const PuzzleLayout *pPuzzleLayout)
{
    int gridX    = pPuzzleLayout->x;
    int gridY    = pPuzzleLayout->y;
    int cellSize = pPuzzleLayout->cellSize;
    int gridWidth = pPuzzleLayout->gridWidth;
    int gridHeight = pPuzzleLayout->gridHeight;

    FillRectangle(buf, gridX, gridY, gridWidth * cellSize, gridHeight * cellSize, COLOUR_UNSOLVED);

    for (int i = 0; i <= gridWidth; ++i) 
    {
        FillRectangle(buf, gridX + i * cellSize, gridY, 1, gridHeight * cellSize + 1, COLOUR_GRID);
    }
    for (int i = 0; i <= gridHeight; i++)
    {
        FillRectangle(buf, gridX, gridY + i * cellSize, gridWidth * cellSize + 1, 1, COLOUR_GRID);
    }
}

static void DrawPuzzleCells(u32 *buf, const PuzzleLayout *pPuzzleLayout, const PuzzleState *pPuzzleState)
{
    int cellSize = pPuzzleLayout->cellSize;

    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        for (int col = 0; col < pPuzzleState->width; ++col)
        {
            u32 bit = (u32)1 << col;

            int x = pPuzzleLayout->x + col * cellSize + 1;
            int y = pPuzzleLayout->y + row * cellSize + 1;

            int drawCellSize = cellSize - 1;

            u32 cellColour = COLOUR_UNSOLVED;
            if (pPuzzleState->rowCellKnowns[row] & bit)
            {
                cellColour = (pPuzzleState->rowCellStates[row] & bit) ? COLOUR_FILLED : COLOUR_EMPTY;
            }
            
            FillRectangle(buf, x, y, drawCellSize, drawCellSize, cellColour);
        }
    }
}

static void DrawRowClues(u32 *buf, const PuzzleLayout *pPuzzleLayout, const PuzzleState *pPuzzleState)
{
    int cellSize = pPuzzleLayout->cellSize;

    char stringBuf[8];
    for (int row = 0; row < pPuzzleState->height; ++row)
    {
        const ClueLine *pClueLine = &pPuzzleState->rowClues[row];

        int clueStringX1 = pPuzzleLayout->x - CLUE_GRID_GAP_PIXELS;
        int clueStringY0 = pPuzzleLayout->y + row * cellSize +
                           (cellSize - CLUE_FONT_DIGIT_HEIGHT_PIXELS) / 2 +
                           CLUE_FONT_DIGIT_BASELINE_PIXELS; // Centre clue string to row

        for (int block = pClueLine->blockCount - 1; block >= 0; --block)
        {
            int clueStringLength = snprintf(stringBuf, sizeof(stringBuf), "%d", pClueLine->blocks[block]);

            clueStringX1 -= CLUE_FONT_DIGIT_ADVANCE_PIXELS * clueStringLength;
            DrawString(buf, clueStringX1, clueStringY0, stringBuf, COLOUR_TEXT, &FreeSans8pt7b_Stats);
            clueStringX1 -= CLUE_BLOCK_GAP_PIXELS;
        }
    }
}

static void DrawColClues(u32 *buf, const PuzzleLayout *pPuzzleLayout, const PuzzleState *pPuzzleState)
{
    int cellSize = pPuzzleLayout->cellSize;

    char stringBuf[8];
    for (int col = 0; col < pPuzzleState->width; ++col)
    {
        const ClueLine *pClueLine = &pPuzzleState->colClues[col];

        int clueStringXCentre = pPuzzleLayout->x + col * cellSize + cellSize / 2; // Centre clue string to column
        int clueStringY0      = pPuzzleLayout->y - CLUE_GRID_GAP_PIXELS -
                                CLUE_FONT_DIGIT_HEIGHT_PIXELS +
                                CLUE_FONT_DIGIT_BASELINE_PIXELS;

        for (int block = pClueLine->blockCount - 1; block >= 0; --block)
        {
            int clueStringLength = snprintf(stringBuf, sizeof(stringBuf), "%d", pClueLine->blocks[block]);

            DrawString(buf, clueStringXCentre - (CLUE_FONT_DIGIT_ADVANCE_PIXELS * clueStringLength) / 2,
                       clueStringY0, stringBuf, COLOUR_TEXT, &FreeSans8pt7b_Stats);
            clueStringY0 -= CLUE_LINE_HEIGHT_PIXELS;
        }
    }
}

static void DrawPuzzle(u32 *buf, const PuzzleLayout *pPuzzleLayout, const PuzzleState *pPuzzleState)
{
    DrawPuzzleGrid(buf, pPuzzleLayout);
    DrawPuzzleCells(buf, pPuzzleLayout, pPuzzleState);
    DrawRowClues(buf, pPuzzleLayout, pPuzzleState);
    DrawColClues(buf, pPuzzleLayout, pPuzzleState);
}

void GraphicsRender(const PuzzleState *pPuzzleState)
{
    if (!graphicsReady) { return; }

    // Clear drawing buffer
    u32 *buf = pActiveFB[activeFBIndex];
    FillRectangle(buf, 0, 0, HDMI_WIDTH, HDMI_HEIGHT, COLOUR_BG);

    if (pPuzzleState && pPuzzleState->width > 0 && pPuzzleState->height > 0)
    {
        PuzzleLayout puzzleLayout = SetupPuzzleLayout(pPuzzleState);

        DrawPuzzle(buf, &puzzleLayout, pPuzzleState);

        // Draw info text for puzzle
        char infoString[256];
        snprintf(infoString, sizeof(infoString), "Size: %dx%d, Difficulty: 0x%02X, Seed: %u",
                 pPuzzleState->width, pPuzzleState->height, pPuzzleState->difficulty, (unsigned int)pPuzzleState->seed);
        DrawString(buf, 10, 10, infoString, COLOUR_TEXT, &FreeSans8pt7b_Stats);

        if (pPuzzleState->solverFailed)
        {
            int skullX = puzzleLayout.x + puzzleLayout.gridWidth * puzzleLayout.cellSize + 50;
            int skullY = puzzleLayout.y + (puzzleLayout.gridHeight * puzzleLayout.cellSize) / 2;

            // 'A' is the char for the skull
            DrawString(buf, skullX, skullY, "A", COLOUR_TEXT, &Fantasy_Clipart50pt7b_A);

            int failTextX = skullX + Fantasy_Clipart50pt7b_AGlyphs[0].width + 10;

            DrawString(buf, failTextX, skullY, "FAIL", COLOUR_TEXT, &PentaGrams_Malefissent50pt7b_FAIL);
        }
    }

    Xil_DCacheFlush();
    SwapFrames();
}
