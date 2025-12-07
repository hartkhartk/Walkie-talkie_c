/**
 * @file display.c
 * @brief מימוש HAL עבור מסך
 */

#include "hal/display.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// Display Buffer
// =============================================================================

// Frame buffer for 128x64 monochrome display
static uint8_t frame_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static bool display_dirty = false;
static uint8_t brightness_level = 100;

// =============================================================================
// Font Data (6x8 basic font)
// =============================================================================

// Simple 6x8 ASCII font (characters 32-127)
static const uint8_t font_6x8[][6] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62, 0x00}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50, 0x00}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02, 0x00}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46, 0x00}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39, 0x00}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03, 0x00}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14, 0x00}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06, 0x00}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E, 0x00}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01, 0x00}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32, 0x00}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F, 0x00}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31, 0x00}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F, 0x00}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03, 0x00}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43, 0x00}, // Z
    // ... more characters can be added
};

// =============================================================================
// Icon Data (8x8 icons)
// =============================================================================

static const uint8_t icons_8x8[][8] = {
    // ICON_NONE
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ICON_BATTERY_FULL
    {0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x18},
    // ICON_BATTERY_MED
    {0x7E, 0xC3, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x18},
    // ICON_BATTERY_LOW
    {0x7E, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0x7E, 0x18},
    // ICON_BATTERY_CHARGING
    {0x7E, 0xC3, 0xDB, 0xDB, 0xDB, 0xC3, 0x7E, 0x18},
    // ICON_SIGNAL_FULL
    {0x80, 0xE0, 0xF8, 0xFE, 0xFE, 0xF8, 0xE0, 0x80},
    // ICON_SIGNAL_MED
    {0x00, 0x00, 0xF8, 0xFE, 0xFE, 0xF8, 0x00, 0x00},
    // ICON_SIGNAL_LOW
    {0x00, 0x00, 0x00, 0xFE, 0xFE, 0x00, 0x00, 0x00},
    // ICON_SIGNAL_NONE
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ICON_LOCKED
    {0x3C, 0x42, 0x42, 0xFF, 0xFF, 0xFF, 0xFF, 0x00},
    // ICON_UNLOCKED
    {0x3C, 0x02, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00},
    // ICON_VISIBLE
    {0x3C, 0x42, 0x81, 0xA5, 0xA5, 0x81, 0x42, 0x3C},
    // ICON_HIDDEN
    {0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00},
    // ICON_MICROPHONE
    {0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x7E, 0x18, 0x18},
    // ICON_MICROPHONE_MUTED
    {0x18, 0x3C, 0x3C, 0x3C, 0x99, 0x7E, 0x18, 0x18},
    // ICON_SPEAKER
    {0x08, 0x1C, 0x7F, 0x7F, 0x7F, 0x1C, 0x08, 0x00},
    // ICON_SPEAKER_MUTED
    {0x08, 0x1C, 0x7F, 0x7F, 0x7F, 0x1C, 0x88, 0x00},
    // ICON_RECORDING
    {0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C},
    // ICON_CALL
    {0xE7, 0xE7, 0xE7, 0x00, 0x00, 0xE7, 0xE7, 0xE7},
    // ICON_FREQUENCY
    {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA},
    // ICON_ARROW_UP
    {0x00, 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x00},
    // ICON_ARROW_DOWN
    {0x00, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00},
    // ICON_ARROW_LEFT
    {0x00, 0x10, 0x38, 0x7C, 0x38, 0x10, 0x00, 0x00},
    // ICON_ARROW_RIGHT
    {0x00, 0x08, 0x1C, 0x3E, 0x1C, 0x08, 0x00, 0x00},
    // ICON_CHECK
    {0x00, 0x00, 0x40, 0x20, 0x12, 0x0C, 0x00, 0x00},
    // ICON_CROSS
    {0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00},
    // ICON_STAR
    {0x08, 0x08, 0x2A, 0x1C, 0x1C, 0x2A, 0x08, 0x08},
};

// =============================================================================
// Platform-Specific Display Functions
// =============================================================================

#ifdef ESP32
#include "driver/spi_master.h"
static spi_device_handle_t spi_handle;

static void display_send_cmd(uint8_t cmd) {
    gpio_set_level(PIN_DISPLAY_DC, 0);
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd
    };
    spi_device_transmit(spi_handle, &trans);
}

static void display_send_data(const uint8_t* data, size_t len) {
    gpio_set_level(PIN_DISPLAY_DC, 1);
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data
    };
    spi_device_transmit(spi_handle, &trans);
}
#else
// Simulator stubs
static void (*sim_update_callback)(const uint8_t* buffer, int width, int height) = NULL;

void sim_set_display_callback(void (*callback)(const uint8_t*, int, int)) {
    sim_update_callback = callback;
}
#endif

// =============================================================================
// Low-Level Drawing Functions
// =============================================================================

static void set_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    
    uint16_t byte_idx = x + (y / 8) * DISPLAY_WIDTH;
    uint8_t bit_idx = y % 8;
    
    if (on) {
        frame_buffer[byte_idx] |= (1 << bit_idx);
    } else {
        frame_buffer[byte_idx] &= ~(1 << bit_idx);
    }
    display_dirty = true;
}

static bool get_pixel(uint8_t x, uint8_t y) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return false;
    
    uint16_t byte_idx = x + (y / 8) * DISPLAY_WIDTH;
    uint8_t bit_idx = y % 8;
    
    return (frame_buffer[byte_idx] & (1 << bit_idx)) != 0;
}

// =============================================================================
// Public API Implementation
// =============================================================================

void display_init(void) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    
#ifdef ESP32
    // Initialize SPI
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_DISPLAY_MOSI,
        .sclk_io_num = PIN_DISPLAY_SCK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
    spi_bus_initialize(HSPI_HOST, &bus_cfg, 1);
    
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 10000000,
        .mode = 0,
        .spics_io_num = PIN_DISPLAY_CS,
        .queue_size = 1
    };
    spi_bus_add_device(HSPI_HOST, &dev_cfg, &spi_handle);
    
    // Initialize GPIO for DC and RST
    gpio_set_direction(PIN_DISPLAY_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_DISPLAY_RST, GPIO_MODE_OUTPUT);
    
    // Reset display
    gpio_set_level(PIN_DISPLAY_RST, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_DISPLAY_RST, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // SSD1306 initialization sequence
    display_send_cmd(0xAE);  // Display off
    display_send_cmd(0xD5);  // Set clock
    display_send_cmd(0x80);
    display_send_cmd(0xA8);  // Set multiplex
    display_send_cmd(0x3F);  // 64 rows
    display_send_cmd(0xD3);  // Display offset
    display_send_cmd(0x00);
    display_send_cmd(0x40);  // Start line
    display_send_cmd(0x8D);  // Charge pump
    display_send_cmd(0x14);
    display_send_cmd(0x20);  // Memory mode
    display_send_cmd(0x00);  // Horizontal
    display_send_cmd(0xA1);  // Segment remap
    display_send_cmd(0xC8);  // COM scan direction
    display_send_cmd(0xDA);  // COM pins
    display_send_cmd(0x12);
    display_send_cmd(0x81);  // Contrast
    display_send_cmd(0xCF);
    display_send_cmd(0xD9);  // Precharge
    display_send_cmd(0xF1);
    display_send_cmd(0xDB);  // VCOMH
    display_send_cmd(0x40);
    display_send_cmd(0xA4);  // Display from RAM
    display_send_cmd(0xA6);  // Normal display
    display_send_cmd(0xAF);  // Display on
#endif
}

void display_clear(void) {
    memset(frame_buffer, 0, sizeof(frame_buffer));
    display_dirty = true;
}

void display_clear_region(display_region_t region) {
    for (uint8_t y = region.y; y < region.y + region.height && y < DISPLAY_HEIGHT; y++) {
        for (uint8_t x = region.x; x < region.x + region.width && x < DISPLAY_WIDTH; x++) {
            set_pixel(x, y, false);
        }
    }
}

void display_update(void) {
    if (!display_dirty) return;
    
#ifdef ESP32
    // Set column and page address
    display_send_cmd(0x21);  // Column address
    display_send_cmd(0);     // Start
    display_send_cmd(127);   // End
    display_send_cmd(0x22);  // Page address
    display_send_cmd(0);     // Start
    display_send_cmd(7);     // End
    
    // Send frame buffer
    display_send_data(frame_buffer, sizeof(frame_buffer));
#else
    // Simulator callback
    if (sim_update_callback) {
        sim_update_callback(frame_buffer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
#endif
    
    display_dirty = false;
}

void display_print(uint8_t x, uint8_t y, const char* text, font_size_t font) {
    if (!text) return;
    
    uint8_t char_width = 6;
    uint8_t char_height = 8;
    
    if (font == FONT_MEDIUM) {
        char_width = 8;
        char_height = 12;
    } else if (font == FONT_LARGE) {
        char_width = 12;
        char_height = 16;
    }
    
    while (*text && x < DISPLAY_WIDTH) {
        char c = *text;
        if (c >= 32 && c < 32 + (int)(sizeof(font_6x8)/sizeof(font_6x8[0]))) {
            const uint8_t* char_data = font_6x8[c - 32];
            
            for (uint8_t col = 0; col < 6 && (x + col) < DISPLAY_WIDTH; col++) {
                uint8_t column_data = char_data[col];
                for (uint8_t row = 0; row < 8 && (y + row) < DISPLAY_HEIGHT; row++) {
                    set_pixel(x + col, y + row, (column_data >> row) & 1);
                }
            }
        }
        x += char_width;
        text++;
    }
}

void display_print_aligned(uint8_t y, const char* text, font_size_t font, text_align_t align) {
    if (!text) return;
    
    uint8_t char_width = 6;
    size_t text_len = strlen(text);
    uint8_t text_width = text_len * char_width;
    
    uint8_t x = 0;
    if (align == ALIGN_CENTER) {
        x = (DISPLAY_WIDTH - text_width) / 2;
    } else if (align == ALIGN_RIGHT) {
        x = DISPLAY_WIDTH - text_width;
    }
    
    display_print(x, y, text, font);
}

void display_print_line(uint8_t line_num, const char* text, bool is_selected) {
    if (line_num >= DISPLAY_LINES) return;
    
    uint8_t y = line_num * 8;
    
    if (is_selected) {
        // Draw inverted (white on black)
        display_rect(0, y, DISPLAY_WIDTH, 8, true);
        // Print with inverted pixels
        if (text) {
            uint8_t x = 0;
            while (*text && x < DISPLAY_WIDTH) {
                char c = *text;
                if (c >= 32 && c < 32 + (int)(sizeof(font_6x8)/sizeof(font_6x8[0]))) {
                    const uint8_t* char_data = font_6x8[c - 32];
                    for (uint8_t col = 0; col < 6 && (x + col) < DISPLAY_WIDTH; col++) {
                        uint8_t column_data = ~char_data[col]; // Invert
                        for (uint8_t row = 0; row < 8 && (y + row) < DISPLAY_HEIGHT; row++) {
                            set_pixel(x + col, y + row, (column_data >> row) & 1);
                        }
                    }
                }
                x += 6;
                text++;
            }
        }
    } else {
        // Normal print
        display_print(0, y, text, FONT_SMALL);
    }
}

void display_icon(uint8_t x, uint8_t y, icon_t icon) {
    if (icon >= ICON_COUNT) return;
    
    const uint8_t* icon_data = icons_8x8[icon];
    for (uint8_t col = 0; col < 8 && (x + col) < DISPLAY_WIDTH; col++) {
        uint8_t column_data = icon_data[col];
        for (uint8_t row = 0; row < 8 && (y + row) < DISPLAY_HEIGHT; row++) {
            set_pixel(x + col, y + row, (column_data >> row) & 1);
        }
    }
}

void display_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    // Bresenham's line algorithm
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        set_pixel(x1, y1, true);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

void display_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool filled) {
    if (filled) {
        for (uint8_t j = y; j < y + height && j < DISPLAY_HEIGHT; j++) {
            for (uint8_t i = x; i < x + width && i < DISPLAY_WIDTH; i++) {
                set_pixel(i, j, true);
            }
        }
    } else {
        display_line(x, y, x + width - 1, y);
        display_line(x, y + height - 1, x + width - 1, y + height - 1);
        display_line(x, y, x, y + height - 1);
        display_line(x + width - 1, y, x + width - 1, y + height - 1);
    }
}

void display_progress_bar(uint8_t x, uint8_t y, uint8_t width, uint8_t progress) {
    if (progress > 100) progress = 100;
    
    // Draw border
    display_rect(x, y, width, 6, false);
    
    // Fill progress
    uint8_t fill_width = ((width - 2) * progress) / 100;
    display_rect(x + 1, y + 1, fill_width, 4, true);
}

void display_status_bar(uint8_t battery_level, uint8_t signal_level, 
                        bool is_recording, bool is_visible) {
    display_clear_region(REGION_STATUS_BAR);
    
    // Battery icon on right
    icon_t battery_icon;
    if (battery_level > 66) battery_icon = ICON_BATTERY_FULL;
    else if (battery_level > 33) battery_icon = ICON_BATTERY_MED;
    else battery_icon = ICON_BATTERY_LOW;
    display_icon(DISPLAY_WIDTH - 9, 0, battery_icon);
    
    // Signal icon
    icon_t signal_icon;
    if (signal_level > 66) signal_icon = ICON_SIGNAL_FULL;
    else if (signal_level > 33) signal_icon = ICON_SIGNAL_MED;
    else if (signal_level > 0) signal_icon = ICON_SIGNAL_LOW;
    else signal_icon = ICON_SIGNAL_NONE;
    display_icon(DISPLAY_WIDTH - 18, 0, signal_icon);
    
    // Recording indicator on left
    if (is_recording) {
        display_icon(0, 0, ICON_RECORDING);
    }
    
    // Visibility indicator
    display_icon(10, 0, is_visible ? ICON_VISIBLE : ICON_HIDDEN);
}

void display_message(const char* title, const char* message) {
    display_clear();
    display_print_aligned(20, title, FONT_MEDIUM, ALIGN_CENTER);
    display_print_aligned(35, message, FONT_SMALL, ALIGN_CENTER);
    display_update();
}

void display_confirm_dialog(const char* title, const char* message,
                           const char* green_text, const char* red_text) {
    display_clear_region(REGION_MAIN);
    
    display_print_aligned(12, title, FONT_MEDIUM, ALIGN_CENTER);
    display_print_aligned(28, message, FONT_SMALL, ALIGN_CENTER);
    
    // Footer with button hints
    display_clear_region(REGION_FOOTER);
    char footer[30];
    snprintf(footer, sizeof(footer), "[%s]  [%s]", green_text, red_text);
    display_print_aligned(56, footer, FONT_SMALL, ALIGN_CENTER);
}

void display_input_field(const char* label, const char* value, 
                         uint8_t cursor_pos, uint8_t max_length) {
    display_clear_region(REGION_MAIN);
    
    // Label
    display_print_aligned(12, label, FONT_SMALL, ALIGN_CENTER);
    
    // Input box
    uint8_t box_width = max_length * 6 + 4;
    uint8_t box_x = (DISPLAY_WIDTH - box_width) / 2;
    display_rect(box_x, 25, box_width, 12, false);
    
    // Value
    if (value) {
        display_print(box_x + 2, 27, value, FONT_SMALL);
    }
    
    // Cursor
    uint8_t cursor_x = box_x + 2 + cursor_pos * 6;
    display_line(cursor_x, 26, cursor_x, 35);
}

void display_list(const char** items, uint8_t item_count, 
                  uint8_t selected_index, uint8_t scroll_offset) {
    display_clear_region(REGION_MAIN);
    
    uint8_t visible_lines = 5;  // Lines 1-5 of main area
    
    for (uint8_t i = 0; i < visible_lines && (scroll_offset + i) < item_count; i++) {
        uint8_t item_idx = scroll_offset + i;
        uint8_t line = i + 1;  // Skip status bar
        display_print_line(line, items[item_idx], item_idx == selected_index);
    }
    
    // Scroll indicators
    if (scroll_offset > 0) {
        display_icon(DISPLAY_WIDTH - 8, 8, ICON_ARROW_UP);
    }
    if (scroll_offset + visible_lines < item_count) {
        display_icon(DISPLAY_WIDTH - 8, 40, ICON_ARROW_DOWN);
    }
}

void display_backlight(bool on) {
    brightness_level = on ? 100 : 0;
    display_set_brightness(brightness_level);
}

void display_set_brightness(uint8_t level) {
    brightness_level = level;
#ifdef ESP32
    display_send_cmd(0x81);  // Set contrast
    display_send_cmd((level * 255) / 100);
#endif
}

void display_sleep(void) {
#ifdef ESP32
    display_send_cmd(0xAE);  // Display off
#endif
}

void display_wake(void) {
#ifdef ESP32
    display_send_cmd(0xAF);  // Display on
#endif
}

