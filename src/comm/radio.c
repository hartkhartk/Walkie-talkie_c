/**
 * @file radio.c
 * @brief מימוש דרייבר LoRa SX1276/RFM95
 */

#include "comm/radio.h"
#include "config.h"
#include <string.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "driver/spi_master.h"
    #include "driver/gpio.h"
    #include "esp_timer.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    
    static const char* TAG = "RADIO";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
    #define DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
    #include <stdio.h>
    #include <time.h>
    #define LOG_INFO(fmt, ...) printf("[RADIO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[RADIO ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
    #define DELAY_MS(ms) usleep((ms) * 1000)
#endif

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static radio_state_t g_state = RADIO_STATE_IDLE;
static radio_config_t g_config;
static radio_stats_t g_stats;

static radio_rx_callback_t g_rx_callback = NULL;
static radio_tx_callback_t g_tx_callback = NULL;

static uint8_t g_rx_buffer[RADIO_MAX_PACKET_SIZE];
static uint8_t g_rx_length = 0;
static bool g_packet_available = false;

#ifdef ESP32
static spi_device_handle_t g_spi_handle;
static SemaphoreHandle_t g_mutex;
#endif

// =============================================================================
// SPI Communication (ESP32)
// =============================================================================

#ifdef ESP32
static void spi_init(void) {
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 23,  // MOSI
        .miso_io_num = 19,  // MISO  
        .sclk_io_num = 18,  // SCK
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RADIO_MAX_PACKET_SIZE + 1
    };
    spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    
    // Configure SPI device for SX1276
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 8000000,  // 8 MHz
        .mode = 0,                   // SPI mode 0
        .spics_io_num = PIN_RADIO_CS,
        .queue_size = 1,
        .pre_cb = NULL
    };
    spi_bus_add_device(VSPI_HOST, &dev_cfg, &g_spi_handle);
}

static uint8_t spi_read_register(uint8_t reg) {
    uint8_t tx_data[2] = {reg & 0x7F, 0x00};
    uint8_t rx_data[2];
    
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };
    
    spi_device_transmit(g_spi_handle, &trans);
    return rx_data[1];
}

static void spi_write_register(uint8_t reg, uint8_t value) {
    uint8_t tx_data[2] = {reg | 0x80, value};
    
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = NULL
    };
    
    spi_device_transmit(g_spi_handle, &trans);
}

static void spi_read_burst(uint8_t reg, uint8_t* buffer, uint8_t length) {
    uint8_t tx_data[RADIO_MAX_PACKET_SIZE + 1];
    uint8_t rx_data[RADIO_MAX_PACKET_SIZE + 1];
    
    memset(tx_data, 0, length + 1);
    tx_data[0] = reg & 0x7F;
    
    spi_transaction_t trans = {
        .length = (length + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };
    
    spi_device_transmit(g_spi_handle, &trans);
    memcpy(buffer, rx_data + 1, length);
}

static void spi_write_burst(uint8_t reg, const uint8_t* buffer, uint8_t length) {
    uint8_t tx_data[RADIO_MAX_PACKET_SIZE + 1];
    
    tx_data[0] = reg | 0x80;
    memcpy(tx_data + 1, buffer, length);
    
    spi_transaction_t trans = {
        .length = (length + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = NULL
    };
    
    spi_device_transmit(g_spi_handle, &trans);
}
#else
// Simulator stubs
static uint8_t spi_read_register(uint8_t reg) { (void)reg; return 0; }
static void spi_write_register(uint8_t reg, uint8_t value) { (void)reg; (void)value; }
static void spi_read_burst(uint8_t reg, uint8_t* buffer, uint8_t length) { 
    (void)reg; memset(buffer, 0, length); 
}
static void spi_write_burst(uint8_t reg, const uint8_t* buffer, uint8_t length) { 
    (void)reg; (void)buffer; (void)length; 
}
#endif

// =============================================================================
// Internal Helper Functions
// =============================================================================

static void set_mode(uint8_t mode) {
    spi_write_register(REG_OP_MODE, MODE_LONG_RANGE_MODE | mode);
}

static void set_idle(void) {
    set_mode(MODE_STDBY);
    g_state = RADIO_STATE_IDLE;
}

static void explicit_header_mode(void) {
    uint8_t reg = spi_read_register(REG_MODEM_CONFIG_1);
    spi_write_register(REG_MODEM_CONFIG_1, reg & 0xFE);
}

static void implicit_header_mode(void) {
    uint8_t reg = spi_read_register(REG_MODEM_CONFIG_1);
    spi_write_register(REG_MODEM_CONFIG_1, reg | 0x01);
}

// =============================================================================
// Public API Implementation
// =============================================================================

bool radio_init(void) {
    if (g_initialized) {
        return true;
    }
    
    LOG_INFO("Initializing LoRa radio...");
    
    memset(&g_stats, 0, sizeof(g_stats));
    memset(&g_config, 0, sizeof(g_config));
    
#ifdef ESP32
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        LOG_ERROR("Failed to create mutex");
        return false;
    }
    
    // Initialize GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_RADIO_RST) | (1ULL << PIN_RADIO_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // DIO0 as input for interrupts
    io_conf.pin_bit_mask = (1ULL << PIN_RADIO_DIO0);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    // Initialize SPI
    spi_init();
    
    // Reset radio
    gpio_set_level(PIN_RADIO_RST, 0);
    DELAY_MS(10);
    gpio_set_level(PIN_RADIO_RST, 1);
    DELAY_MS(10);
#endif
    
    // Check radio version
    uint8_t version = spi_read_register(REG_VERSION);
    LOG_INFO("Radio version: 0x%02X", version);
    
#ifdef ESP32
    if (version != 0x12) {
        LOG_ERROR("Invalid radio version (expected 0x12)");
        return false;
    }
#endif
    
    // Put in sleep mode to configure
    set_mode(MODE_SLEEP);
    DELAY_MS(10);
    
    // Set LoRa mode
    spi_write_register(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
    DELAY_MS(10);
    
    // Set FIFO base addresses
    spi_write_register(REG_FIFO_TX_BASE_ADDR, 0x00);
    spi_write_register(REG_FIFO_RX_BASE_ADDR, 0x00);
    
    // Set LNA boost
    spi_write_register(REG_LNA, spi_read_register(REG_LNA) | 0x03);
    
    // Set auto AGC
    spi_write_register(REG_MODEM_CONFIG_3, 0x04);
    
    // Apply default configuration
    radio_config_t default_cfg;
    radio_get_default_config(&default_cfg);
    radio_set_config(&default_cfg);
    
    // Go to standby
    set_idle();
    
    g_initialized = true;
    LOG_INFO("LoRa radio initialized successfully");
    
    return true;
}

bool radio_is_ready(void) {
    return g_initialized;
}

void radio_get_default_config(radio_config_t* config) {
    if (!config) return;
    
    config->frequency = RADIO_FREQUENCY;
    config->tx_power = RADIO_TX_POWER;
    config->bandwidth = RADIO_BANDWIDTH;
    config->spreading_factor = RADIO_SPREADING_FACTOR;
    config->coding_rate = 5;  // 4/5
    config->preamble_length = 8;
    config->sync_word = 0x12;
    config->crc_enabled = true;
    config->implicit_header = false;
}

void radio_set_config(const radio_config_t* config) {
    if (!config) return;
    
    memcpy(&g_config, config, sizeof(radio_config_t));
    
    // Apply settings
    radio_set_frequency(config->frequency);
    radio_set_tx_power(config->tx_power);
    radio_set_bandwidth(config->bandwidth);
    radio_set_spreading_factor(config->spreading_factor);
    
    // Coding rate
    uint8_t reg = spi_read_register(REG_MODEM_CONFIG_1);
    reg = (reg & 0xF1) | ((config->coding_rate - 4) << 1);
    spi_write_register(REG_MODEM_CONFIG_1, reg);
    
    // Preamble length
    spi_write_register(REG_PREAMBLE_MSB, (config->preamble_length >> 8) & 0xFF);
    spi_write_register(REG_PREAMBLE_LSB, config->preamble_length & 0xFF);
    
    // Sync word
    spi_write_register(REG_SYNC_WORD, config->sync_word);
    
    // CRC
    reg = spi_read_register(REG_MODEM_CONFIG_2);
    if (config->crc_enabled) {
        reg |= 0x04;
    } else {
        reg &= 0xFB;
    }
    spi_write_register(REG_MODEM_CONFIG_2, reg);
    
    // Header mode
    if (config->implicit_header) {
        implicit_header_mode();
    } else {
        explicit_header_mode();
    }
    
    LOG_INFO("Config applied: %lu Hz, SF%d, BW %lu Hz", 
             config->frequency, config->spreading_factor, config->bandwidth);
}

void radio_set_frequency(uint32_t frequency) {
    g_config.frequency = frequency;
    
    uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
    
    spi_write_register(REG_FRF_MSB, (uint8_t)(frf >> 16));
    spi_write_register(REG_FRF_MID, (uint8_t)(frf >> 8));
    spi_write_register(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

void radio_set_tx_power(int8_t power) {
    g_config.tx_power = power;
    
    // Clamp power to valid range
    if (power < 2) power = 2;
    if (power > 20) power = 20;
    
    if (power > 17) {
        // Use PA_BOOST with high power settings
        spi_write_register(REG_PA_DAC, 0x87);  // Enable +20dBm
        spi_write_register(REG_PA_CONFIG, PA_BOOST | (power - 5));
    } else {
        spi_write_register(REG_PA_DAC, 0x84);  // Default
        spi_write_register(REG_PA_CONFIG, PA_BOOST | (power - 2));
    }
}

void radio_set_bandwidth(uint32_t bandwidth) {
    g_config.bandwidth = bandwidth;
    
    uint8_t bw_code;
    if (bandwidth <= 7800) bw_code = 0;
    else if (bandwidth <= 10400) bw_code = 1;
    else if (bandwidth <= 15600) bw_code = 2;
    else if (bandwidth <= 20800) bw_code = 3;
    else if (bandwidth <= 31250) bw_code = 4;
    else if (bandwidth <= 41700) bw_code = 5;
    else if (bandwidth <= 62500) bw_code = 6;
    else if (bandwidth <= 125000) bw_code = 7;
    else if (bandwidth <= 250000) bw_code = 8;
    else bw_code = 9;  // 500 kHz
    
    uint8_t reg = spi_read_register(REG_MODEM_CONFIG_1);
    reg = (reg & 0x0F) | (bw_code << 4);
    spi_write_register(REG_MODEM_CONFIG_1, reg);
}

void radio_set_spreading_factor(uint8_t sf) {
    if (sf < 6) sf = 6;
    if (sf > 12) sf = 12;
    
    g_config.spreading_factor = sf;
    
    uint8_t reg = spi_read_register(REG_MODEM_CONFIG_2);
    reg = (reg & 0x0F) | ((sf << 4) & 0xF0);
    spi_write_register(REG_MODEM_CONFIG_2, reg);
    
    // Detection optimization for SF6
    if (sf == 6) {
        spi_write_register(REG_DETECTION_OPTIMIZE, 0xC5);
        spi_write_register(REG_DETECTION_THRESHOLD, 0x0C);
    } else {
        spi_write_register(REG_DETECTION_OPTIMIZE, 0xC3);
        spi_write_register(REG_DETECTION_THRESHOLD, 0x0A);
    }
}

bool radio_send(const uint8_t* data, uint8_t length) {
    if (!g_initialized || !data || length == 0 || length > RADIO_MAX_PACKET_SIZE) {
        return false;
    }
    
#ifdef ESP32
    xSemaphoreTake(g_mutex, portMAX_DELAY);
#endif
    
    // Go to standby
    set_idle();
    
    // Set FIFO pointer to TX base
    spi_write_register(REG_FIFO_ADDR_PTR, 0x00);
    
    // Write data to FIFO
    spi_write_burst(REG_FIFO, data, length);
    
    // Set payload length
    spi_write_register(REG_PAYLOAD_LENGTH, length);
    
    // Clear IRQ flags
    spi_write_register(REG_IRQ_FLAGS, 0xFF);
    
    // Configure DIO0 for TX Done
    spi_write_register(REG_DIO_MAPPING_1, 0x40);
    
    // Start transmission
    set_mode(MODE_TX);
    g_state = RADIO_STATE_TX;
    
    LOG_DEBUG("TX started, %d bytes", length);
    
#ifdef ESP32
    xSemaphoreGive(g_mutex);
#endif
    
    return true;
}

bool radio_send_blocking(const uint8_t* data, uint8_t length, uint32_t timeout_ms) {
    if (!radio_send(data, length)) {
        return false;
    }
    
    uint32_t start = GET_MILLIS();
    
    while (g_state == RADIO_STATE_TX) {
        radio_handle_interrupt();
        
        if (timeout_ms > 0 && (GET_MILLIS() - start) >= timeout_ms) {
            LOG_ERROR("TX timeout");
            g_stats.tx_timeouts++;
            set_idle();
            return false;
        }
        
        DELAY_MS(1);
    }
    
    return true;
}

void radio_start_receive(void) {
    if (!g_initialized) return;
    
#ifdef ESP32
    xSemaphoreTake(g_mutex, portMAX_DELAY);
#endif
    
    // Go to standby first
    set_idle();
    
    // Set FIFO pointer to RX base
    spi_write_register(REG_FIFO_ADDR_PTR, 0x00);
    
    // Clear IRQ flags
    spi_write_register(REG_IRQ_FLAGS, 0xFF);
    
    // Configure DIO0 for RX Done
    spi_write_register(REG_DIO_MAPPING_1, 0x00);
    
    // Start continuous receive
    set_mode(MODE_RX_CONTINUOUS);
    g_state = RADIO_STATE_RX;
    
    LOG_DEBUG("RX continuous started");
    
#ifdef ESP32
    xSemaphoreGive(g_mutex);
#endif
}

void radio_receive_single(uint32_t timeout_ms) {
    if (!g_initialized) return;
    
    set_idle();
    
    spi_write_register(REG_FIFO_ADDR_PTR, 0x00);
    spi_write_register(REG_IRQ_FLAGS, 0xFF);
    spi_write_register(REG_DIO_MAPPING_1, 0x00);
    
    set_mode(MODE_RX_SINGLE);
    g_state = RADIO_STATE_RX;
    
    if (timeout_ms > 0) {
        // Handle timeout externally
        (void)timeout_ms;
    }
}

void radio_stop_receive(void) {
    set_idle();
}

bool radio_channel_is_free(void) {
    if (!g_initialized) return true;
    
    // Put in CAD mode
    set_mode(MODE_CAD);
    
    // Wait for CAD done
    uint32_t start = GET_MILLIS();
    while (!(spi_read_register(REG_IRQ_FLAGS) & IRQ_CAD_DONE)) {
        if ((GET_MILLIS() - start) > 100) {
            set_idle();
            return true;  // Timeout, assume free
        }
        DELAY_MS(1);
    }
    
    bool detected = (spi_read_register(REG_IRQ_FLAGS) & IRQ_CAD_DETECTED) != 0;
    spi_write_register(REG_IRQ_FLAGS, IRQ_CAD_DONE | IRQ_CAD_DETECTED);
    
    set_idle();
    return !detected;
}

uint8_t radio_read_packet(uint8_t* buffer, uint8_t max_length) {
    if (!g_packet_available || !buffer) {
        return 0;
    }
    
    uint8_t len = (g_rx_length < max_length) ? g_rx_length : max_length;
    memcpy(buffer, g_rx_buffer, len);
    
    g_packet_available = false;
    g_rx_length = 0;
    
    return len;
}

int16_t radio_get_rssi(void) {
    return g_stats.last_rssi;
}

int8_t radio_get_snr(void) {
    return g_stats.last_snr;
}

radio_state_t radio_get_state(void) {
    return g_state;
}

const radio_stats_t* radio_get_stats(void) {
    return &g_stats;
}

void radio_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

void radio_set_rx_callback(radio_rx_callback_t callback) {
    g_rx_callback = callback;
}

void radio_set_tx_callback(radio_tx_callback_t callback) {
    g_tx_callback = callback;
}

void radio_handle_interrupt(void) {
    if (!g_initialized) return;
    
    uint8_t irq_flags = spi_read_register(REG_IRQ_FLAGS);
    
    // TX Done
    if (irq_flags & IRQ_TX_DONE_MASK) {
        spi_write_register(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
        
        g_stats.packets_sent++;
        g_state = RADIO_STATE_IDLE;
        
        LOG_DEBUG("TX done");
        
        if (g_tx_callback) {
            g_tx_callback(true);
        }
        
        // Auto-return to RX mode
        radio_start_receive();
    }
    
    // RX Done
    if (irq_flags & IRQ_RX_DONE_MASK) {
        spi_write_register(REG_IRQ_FLAGS, IRQ_RX_DONE_MASK);
        
        // Check CRC error
        if (irq_flags & IRQ_PAYLOAD_CRC_ERROR) {
            spi_write_register(REG_IRQ_FLAGS, IRQ_PAYLOAD_CRC_ERROR);
            g_stats.crc_errors++;
            LOG_DEBUG("RX CRC error");
            return;
        }
        
        // Read packet from FIFO
        uint8_t rx_current = spi_read_register(REG_FIFO_RX_CURRENT);
        spi_write_register(REG_FIFO_ADDR_PTR, rx_current);
        
        g_rx_length = spi_read_register(REG_RX_NB_BYTES);
        spi_read_burst(REG_FIFO, g_rx_buffer, g_rx_length);
        
        // Read RSSI and SNR
        g_stats.last_rssi = spi_read_register(REG_PKT_RSSI_VALUE) - 157;
        g_stats.last_snr = (int8_t)spi_read_register(REG_PKT_SNR_VALUE) / 4;
        
        g_stats.packets_received++;
        g_packet_available = true;
        
        LOG_DEBUG("RX done: %d bytes, RSSI=%d, SNR=%d", 
                  g_rx_length, g_stats.last_rssi, g_stats.last_snr);
        
        if (g_rx_callback) {
            g_rx_callback(g_rx_buffer, g_rx_length, g_stats.last_rssi, g_stats.last_snr);
        }
    }
}

void radio_update(void) {
#ifdef ESP32
    // Check DIO0 for interrupt
    if (gpio_get_level(PIN_RADIO_DIO0)) {
        radio_handle_interrupt();
    }
#endif
}

void radio_sleep(void) {
    set_mode(MODE_SLEEP);
    g_state = RADIO_STATE_SLEEP;
}

void radio_wake(void) {
    set_idle();
}

