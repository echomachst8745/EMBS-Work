// Build time configuration

#ifndef CONFIG_H_
#define CONFIG_H_

// MAC address for student network
#define EMBS_MAC_USER_BYTE    0x17

// Server
#define NONOGRAM_SERVER_IP_A    192
#define NONOGRAM_SERVER_IP_B    168
#define NONOGRAM_SERVER_IP_C    10
#define NONOGRAM_SERVER_IP_D    1
#define NONOGRAM_SERVER_PORT    51050

// Puzzle limits
#define NONOGRAM_MAX_DIM       32
#define NONOGRAM_MAX_BLOCKS    16

// HDMI
#define HDMI_WIDTH         1440
#define HDMI_HEIGHT        900
#define HDMI_NUM_PIXELS    (HDMI_WIDTH * HDMI_HEIGHT)
#define FRAME_STRIDE       (HDMI_WIDTH * sizeof(u32))

// FreeRTOS stack sizes
#define STACKSIZE_GRAPHICS    4096
#define STACKSIZE_SOLVER      4096
#define STACKSIZE_NETWORK     2048
#define STACKSIZE_UI          1024

// Task Priorities (higher number = higher priority)
#define PRIORITY_NETWORK    (DEFAULT_THREAD_PRIO + 1)
#define PRIORITY_SOLVER     (DEFAULT_THREAD_PRIO + 0)
#define PRIORITY_GRAPHICS   (DEFAULT_THREAD_PRIO + 0)
#define PRIORITY_UI         (DEFAULT_THREAD_PRIO + 0)

// Backtracking max stack size and max solve time
#define SOLVER_MAX_BACKTRACK    400
#define SOLVER_MAX_TIME_MS      55000

#endif
