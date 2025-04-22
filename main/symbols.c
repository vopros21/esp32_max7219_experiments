#include <stdint.h>

static const uint64_t digits[] = {
    0x3c66666e76663c00, // 0
    0x7e1818181c181800, // 1
    0x7e060c3060663c00, // 2
    0x3c66603860663c00, // 3
    0x30307e3234383000, // 4
    0x3c6660603e067e00, // 5
    0x3c66663e06663c00, // 6
    0x1818183030667e00, // 7
    0x3c66663c66663c00, // 8
    0x3c66607c66663c00 // 9
    
};

static const uint64_t symbols[] = {
    0x383838fe7c381000, // arrow
    0x10387cfe38383800, // arrow
    0x10307efe7e301000, // arrow
    0x1018fcfefc181000, // arrow
    0x10387cfefeee4400, // heart
    0x105438ee38541000, // sun
};

// letters
static const uint64_t capital_letters[] = {
    0xc3c3c3c3dbe7c3c3, // M
    0x3c18181818001800, // i
    0x66361E0E1E360600, // k
    0x3c023e221c000000 // e
};