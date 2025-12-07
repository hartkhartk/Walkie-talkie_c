/**
 * @file usb_cdc.c
 * @brief מימוש מודול USB CDC ו-Mass Storage
 */

#include "hal/usb_cdc.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "esp_log.h"
    #include "driver/gpio.h"
    
    #if defined(CONFIG_TINYUSB_ENABLED) || defined(ESP32S3)
        #include "tinyusb.h"
        #include "tusb_cdc_acm.h"
        #include "tusb_msc_storage.h"
        #define USB_SUPPORTED 1
    #else
        #define USB_SUPPORTED 0
    #endif
    
    static const char* TAG = "USB";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
    #include <unistd.h>
    #define USB_SUPPORTED 0
    #define LOG_INFO(fmt, ...) printf("[USB] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[USB ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
#endif

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static usb_mode_t g_mode = USB_MODE_NONE;
static usb_state_t g_state = USB_STATE_DISCONNECTED;

static usb_cdc_rx_callback_t g_rx_callback = NULL;
static usb_state_callback_t g_state_callback = NULL;

static uint8_t g_rx_buffer[USB_CDC_BUFFER_SIZE];
static size_t g_rx_head = 0;
static size_t g_rx_tail = 0;

static char g_serial_number[32] = "";

static uint32_t g_bytes_sent = 0;
static uint32_t g_bytes_received = 0;

// =============================================================================
// Platform-Specific Implementation
// =============================================================================

#if USB_SUPPORTED && defined(ESP32)

// TinyUSB callbacks
static void cdc_rx_callback(int itf, cdcacm_event_t* event) {
    (void)itf;
    
    if (event->type == CDC_EVENT_RX) {
        uint8_t buf[64];
        size_t rx_size = 0;
        
        // Read from TinyUSB
        tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
        
        if (rx_size > 0) {
            g_bytes_received += rx_size;
            
            // Store in ring buffer
            for (size_t i = 0; i < rx_size; i++) {
                size_t next = (g_rx_head + 1) % USB_CDC_BUFFER_SIZE;
                if (next != g_rx_tail) {
                    g_rx_buffer[g_rx_head] = buf[i];
                    g_rx_head = next;
                }
            }
            
            // Call user callback
            if (g_rx_callback) {
                g_rx_callback(buf, rx_size);
            }
        }
    }
}

static void cdc_line_state_callback(int itf, cdcacm_event_t* event) {
    (void)itf;
    
    if (event->line_state_changed_data.dtr && event->line_state_changed_data.rts) {
        g_state = USB_STATE_CONNECTED;
        LOG_INFO("CDC connected");
    } else {
        g_state = USB_STATE_DISCONNECTED;
        LOG_INFO("CDC disconnected");
    }
    
    if (g_state_callback) {
        g_state_callback(g_state);
    }
}

#endif

// =============================================================================
// Initialization
// =============================================================================

bool usb_init(usb_mode_t mode) {
    if (g_initialized) {
        return true;
    }
    
    LOG_INFO("Initializing USB (mode: %d)...", mode);
    
    // Generate serial number from device ID
    if (g_serial_number[0] == '\0') {
#ifdef ESP32
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        snprintf(g_serial_number, sizeof(g_serial_number), 
                 "%s%02X%02X%02X%02X%02X%02X",
                 USB_SERIAL_PREFIX,
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
        snprintf(g_serial_number, sizeof(g_serial_number), "%sSIMULATOR", USB_SERIAL_PREFIX);
#endif
    }
    
#if USB_SUPPORTED && defined(ESP32)
    
    // Configure TinyUSB
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  // Use default
        .string_descriptor = NULL,  // Use default
        .external_phy = false,
    };
    
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        LOG_ERROR("TinyUSB install failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Initialize CDC
    if (mode == USB_MODE_CDC || mode == USB_MODE_CDC_MSC) {
        tinyusb_config_cdcacm_t acm_cfg = {
            .usb_dev = TINYUSB_USBDEV_0,
            .cdc_port = TINYUSB_CDC_ACM_0,
            .rx_unread_buf_sz = USB_CDC_BUFFER_SIZE,
            .callback_rx = cdc_rx_callback,
            .callback_rx_wanted_char = NULL,
            .callback_line_state_changed = cdc_line_state_callback,
            .callback_line_coding_changed = NULL
        };
        
        ret = tusb_cdc_acm_init(&acm_cfg);
        if (ret != ESP_OK) {
            LOG_ERROR("CDC init failed: %s", esp_err_to_name(ret));
            return false;
        }
        
        LOG_INFO("CDC initialized");
    }
    
    // Initialize MSC
    if (mode == USB_MODE_MSC || mode == USB_MODE_CDC_MSC) {
        // MSC initialization depends on storage backend
        // This would be configured based on SD card or SPIFFS
        LOG_INFO("MSC initialization (placeholder)");
    }
    
#else
    LOG_INFO("USB not supported on this platform, using simulation");
#endif
    
    g_mode = mode;
    g_initialized = true;
    
    LOG_INFO("USB initialized, serial: %s", g_serial_number);
    return true;
}

void usb_deinit(void) {
    if (!g_initialized) {
        return;
    }
    
#if USB_SUPPORTED && defined(ESP32)
    // Deinitialize TinyUSB components
#endif
    
    g_initialized = false;
    g_mode = USB_MODE_NONE;
    g_state = USB_STATE_DISCONNECTED;
    
    LOG_INFO("USB deinitialized");
}

bool usb_is_initialized(void) {
    return g_initialized;
}

bool usb_set_mode(usb_mode_t mode) {
    if (!g_initialized) {
        return false;
    }
    
    if (mode == g_mode) {
        return true;
    }
    
    // Would need to reconfigure USB
    LOG_INFO("Changing USB mode to %d", mode);
    g_mode = mode;
    
    return true;
}

usb_mode_t usb_get_mode(void) {
    return g_mode;
}

void usb_get_info(usb_info_t* info) {
    if (!info) return;
    
    memset(info, 0, sizeof(usb_info_t));
    info->mode = g_mode;
    info->state = g_state;
    info->cdc_connected = usb_cdc_is_connected();
    info->msc_connected = usb_msc_is_connected();
    info->bytes_sent = g_bytes_sent;
    info->bytes_received = g_bytes_received;
    strncpy(info->serial_number, g_serial_number, sizeof(info->serial_number) - 1);
}

// =============================================================================
// CDC Functions
// =============================================================================

bool usb_cdc_is_connected(void) {
    if (!g_initialized || g_mode == USB_MODE_NONE || g_mode == USB_MODE_MSC) {
        return false;
    }
    return g_state == USB_STATE_CONNECTED || g_state == USB_STATE_CONFIGURED;
}

int32_t usb_cdc_write(const uint8_t* data, size_t length) {
    if (!usb_cdc_is_connected() || !data || length == 0) {
        return 0;
    }
    
#if USB_SUPPORTED && defined(ESP32)
    size_t written = 0;
    esp_err_t ret = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, length);
    if (ret == ESP_OK) {
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        written = length;
    }
    g_bytes_sent += written;
    return written;
#else
    // Simulator: write to stdout
    fwrite(data, 1, length, stdout);
    fflush(stdout);
    g_bytes_sent += length;
    return length;
#endif
}

int32_t usb_cdc_print(const char* str) {
    if (!str) return 0;
    return usb_cdc_write((const uint8_t*)str, strlen(str));
}

int32_t usb_cdc_printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0) {
        return usb_cdc_write((const uint8_t*)buffer, len);
    }
    return 0;
}

int32_t usb_cdc_read(uint8_t* buffer, size_t max_length) {
    if (!buffer || max_length == 0) {
        return 0;
    }
    
    size_t count = 0;
    while (count < max_length && g_rx_tail != g_rx_head) {
        buffer[count++] = g_rx_buffer[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % USB_CDC_BUFFER_SIZE;
    }
    
    return count;
}

int32_t usb_cdc_readline(char* buffer, size_t max_length) {
    if (!buffer || max_length == 0) {
        return 0;
    }
    
    size_t count = 0;
    while (count < max_length - 1 && g_rx_tail != g_rx_head) {
        char c = g_rx_buffer[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % USB_CDC_BUFFER_SIZE;
        
        buffer[count++] = c;
        
        if (c == '\n') {
            break;
        }
    }
    
    buffer[count] = '\0';
    return count;
}

int32_t usb_cdc_available(void) {
    if (g_rx_head >= g_rx_tail) {
        return g_rx_head - g_rx_tail;
    }
    return USB_CDC_BUFFER_SIZE - g_rx_tail + g_rx_head;
}

void usb_cdc_flush_rx(void) {
    g_rx_head = 0;
    g_rx_tail = 0;
}

void usb_cdc_flush_tx(void) {
#if USB_SUPPORTED && defined(ESP32)
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
#endif
}

void usb_cdc_set_rx_callback(usb_cdc_rx_callback_t callback) {
    g_rx_callback = callback;
}

// =============================================================================
// MSC Functions
// =============================================================================

bool usb_msc_is_connected(void) {
    if (!g_initialized || g_mode == USB_MODE_NONE || g_mode == USB_MODE_CDC) {
        return false;
    }
    // MSC connection state would come from TinyUSB
    return false;
}

bool usb_msc_enable(void) {
    if (!g_initialized) {
        return false;
    }
    
    LOG_INFO("Enabling MSC mode");
    
#if USB_SUPPORTED && defined(ESP32)
    // Configure MSC with SD card backend
    // This requires tinyusb_msc component
#endif
    
    return true;
}

void usb_msc_disable(void) {
    LOG_INFO("Disabling MSC mode");
}

bool usb_msc_is_writing(void) {
    return false;  // Would check TinyUSB MSC state
}

void usb_msc_sync(void) {
#if USB_SUPPORTED && defined(ESP32)
    // Sync filesystem
#endif
}

// =============================================================================
// State & Callbacks
// =============================================================================

usb_state_t usb_get_state(void) {
    return g_state;
}

void usb_set_state_callback(usb_state_callback_t callback) {
    g_state_callback = callback;
}

bool usb_cable_connected(void) {
#ifdef ESP32
    // Check VBUS detection pin if available
    #ifdef PIN_USB_DETECT
        return gpio_get_level(PIN_USB_DETECT) == 1;
    #else
        return g_state != USB_STATE_DISCONNECTED;
    #endif
#else
    return true;  // Simulator always connected
#endif
}

// =============================================================================
// Serial Number
// =============================================================================

void usb_get_serial_number(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    strncpy(buffer, g_serial_number, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

void usb_set_serial_number(const char* serial) {
    if (!serial) return;
    strncpy(g_serial_number, serial, sizeof(g_serial_number) - 1);
    g_serial_number[sizeof(g_serial_number) - 1] = '\0';
}

// =============================================================================
// Command Processing
// =============================================================================

bool usb_process_command(const char* cmd, char* response, size_t response_size) {
    if (!cmd || !response || response_size == 0) {
        return false;
    }
    
    response[0] = '\0';
    
    // Parse command
    if (strncmp(cmd, "INFO", 4) == 0) {
        snprintf(response, response_size,
                 "{\n"
                 "  \"device\": \"%s\",\n"
                 "  \"version\": \"%s\",\n"
                 "  \"serial\": \"%s\"\n"
                 "}\n",
                 DEVICE_NAME, FIRMWARE_VERSION, g_serial_number);
        return true;
    }
    
    if (strncmp(cmd, "STATUS", 6) == 0) {
        snprintf(response, response_size,
                 "{\n"
                 "  \"usb_mode\": %d,\n"
                 "  \"usb_state\": %d,\n"
                 "  \"bytes_tx\": %u,\n"
                 "  \"bytes_rx\": %u\n"
                 "}\n",
                 g_mode, g_state, g_bytes_sent, g_bytes_received);
        return true;
    }
    
    if (strncmp(cmd, "REBOOT", 6) == 0) {
        snprintf(response, response_size, "OK: Rebooting...\n");
#ifdef ESP32
        // esp_restart();  // Uncomment to enable
#endif
        return true;
    }
    
    if (strncmp(cmd, "HELP", 4) == 0) {
        snprintf(response, response_size,
                 "Available commands:\n"
                 "  INFO    - Device information\n"
                 "  STATUS  - Current status\n"
                 "  REBOOT  - Restart device\n"
                 "  HELP    - This help\n");
        return true;
    }
    
    snprintf(response, response_size, "ERROR: Unknown command. Type HELP for list.\n");
    return false;
}

void usb_command_loop(void) {
    if (!usb_cdc_is_connected()) {
        return;
    }
    
    if (usb_cdc_available() > 0) {
        char cmd[128];
        int len = usb_cdc_readline(cmd, sizeof(cmd));
        
        if (len > 0) {
            // Remove trailing newline
            while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r')) {
                cmd[--len] = '\0';
            }
            
            if (len > 0) {
                char response[512];
                usb_process_command(cmd, response, sizeof(response));
                usb_cdc_print(response);
            }
        }
    }
}

// =============================================================================
// Update Loop
// =============================================================================

void usb_update(void) {
    if (!g_initialized) {
        return;
    }
    
    // Process any pending data
    usb_command_loop();
    
    // Update state if needed
#if USB_SUPPORTED && defined(ESP32)
    // TinyUSB handles most of this internally
#endif
}

