/**
 * @file audio.c
 * @brief מימוש דרייבר אודיו - ADC/DAC ו-I2S
 */

#include "hal/audio.h"
#include "config.h"
#include <string.h>
#include <math.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "driver/i2s.h"
    #include "driver/adc.h"
    #include "driver/dac.h"
    #include "driver/gpio.h"
    #include "esp_timer.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "soc/i2s_reg.h"
    
    static const char* TAG = "AUDIO";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
#else
    #include <stdio.h>
    #include <time.h>
    #define LOG_INFO(fmt, ...) printf("[AUDIO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[AUDIO ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
#endif

// =============================================================================
// Constants
// =============================================================================

#define I2S_NUM             I2S_NUM_0
#define DMA_BUF_COUNT       4
#define DMA_BUF_LEN         512
#define NOISE_GATE_DEFAULT  500

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static audio_state_t g_state = AUDIO_STATE_IDLE;
static audio_config_t g_config;
static audio_stats_t g_stats;

// Buffers
static audio_ring_buffer_t* g_record_buffer = NULL;
static audio_ring_buffer_t* g_playback_buffer = NULL;

// Callbacks
static audio_capture_callback_t g_capture_callback = NULL;
static audio_playback_callback_t g_playback_callback = NULL;

// Volume & Gain
static uint8_t g_input_gain = 70;
static uint8_t g_output_volume = 80;
static bool g_muted = false;

// Processing
static bool g_noise_gate_enabled = true;
static uint16_t g_noise_gate_threshold = NOISE_GATE_DEFAULT;
static bool g_agc_enabled = true;

// Levels
static uint16_t g_current_input_level = 0;
static uint16_t g_current_output_level = 0;

// DMA buffers
static int16_t g_dma_read_buffer[DMA_BUF_LEN];
static int16_t g_dma_write_buffer[DMA_BUF_LEN];

#ifdef ESP32
static TaskHandle_t g_audio_task_handle = NULL;
static SemaphoreHandle_t g_audio_mutex = NULL;
#endif

// =============================================================================
// Forward Declarations
// =============================================================================

static void audio_task(void* param);
static void process_input_samples(int16_t* samples, uint16_t count);
static void process_output_samples(int16_t* samples, uint16_t count);
static uint16_t calculate_rms_level(const int16_t* samples, uint16_t count);

// =============================================================================
// Initialization
// =============================================================================

void audio_get_default_config(audio_config_t* config) {
    if (!config) return;
    
    config->mode = AUDIO_MODE_ADC_DAC;
    config->sample_rate = AUDIO_SAMPLE_RATE;
    config->bits_per_sample = AUDIO_BITS;
    config->use_aec = false;
    config->use_agc = true;
    config->use_noise_gate = true;
    config->input_gain = 70;
    config->output_volume = 80;
}

bool audio_init(const audio_config_t* config) {
    if (g_initialized) {
        return true;
    }
    
    LOG_INFO("Initializing audio system...");
    
    // Use provided config or defaults
    if (config) {
        memcpy(&g_config, config, sizeof(audio_config_t));
    } else {
        audio_get_default_config(&g_config);
    }
    
    memset(&g_stats, 0, sizeof(g_stats));
    
    g_input_gain = g_config.input_gain;
    g_output_volume = g_config.output_volume;
    g_noise_gate_enabled = g_config.use_noise_gate;
    g_agc_enabled = g_config.use_agc;
    
#ifdef ESP32
    g_audio_mutex = xSemaphoreCreateMutex();
    if (!g_audio_mutex) {
        LOG_ERROR("Failed to create audio mutex");
        return false;
    }
    
    // Configure speaker enable pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_SPEAKER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_SPEAKER_EN, 0);  // Speaker off initially
    
    if (g_config.mode == AUDIO_MODE_ADC_DAC) {
        // Configure ADC for microphone input
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
        
        // Configure DAC for speaker output
        dac_output_enable(DAC_CHANNEL_1);  // GPIO25
        
        // Configure I2S for ADC mode (DMA-driven ADC sampling)
        i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN,
            .sample_rate = g_config.sample_rate,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = DMA_BUF_COUNT,
            .dma_buf_len = DMA_BUF_LEN,
            .use_apll = false,
            .tx_desc_auto_clear = true
        };
        
        esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
        if (err != ESP_OK) {
            LOG_ERROR("Failed to install I2S driver: %d", err);
            return false;
        }
        
        // Set ADC mode
        i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0);
        
        LOG_INFO("ADC/DAC mode configured");
        
    } else if (g_config.mode == AUDIO_MODE_I2S) {
        // Full I2S mode for external codec
        i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
            .sample_rate = g_config.sample_rate,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = DMA_BUF_COUNT,
            .dma_buf_len = DMA_BUF_LEN,
            .use_apll = true,
            .tx_desc_auto_clear = true
        };
        
        i2s_pin_config_t pin_config = {
            .bck_io_num = 26,
            .ws_io_num = 25,
            .data_out_num = 22,
            .data_in_num = 35
        };
        
        esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
        if (err != ESP_OK) {
            LOG_ERROR("Failed to install I2S driver: %d", err);
            return false;
        }
        
        err = i2s_set_pin(I2S_NUM, &pin_config);
        if (err != ESP_OK) {
            LOG_ERROR("Failed to set I2S pins: %d", err);
            return false;
        }
        
        LOG_INFO("I2S mode configured");
    }
#endif
    
    g_initialized = true;
    LOG_INFO("Audio system initialized successfully");
    
    return true;
}

void audio_deinit(void) {
    if (!g_initialized) return;
    
    audio_stop_recording();
    audio_stop_playback();
    
#ifdef ESP32
    i2s_driver_uninstall(I2S_NUM);
    
    if (g_audio_mutex) {
        vSemaphoreDelete(g_audio_mutex);
        g_audio_mutex = NULL;
    }
#endif
    
    g_initialized = false;
    LOG_INFO("Audio system deinitialized");
}

bool audio_is_initialized(void) {
    return g_initialized;
}

// =============================================================================
// Recording
// =============================================================================

bool audio_start_recording(audio_ring_buffer_t* buffer) {
    if (!g_initialized || !buffer) return false;
    
    if (g_state == AUDIO_STATE_RECORDING || g_state == AUDIO_STATE_DUPLEX) {
        return true;  // Already recording
    }
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_record_buffer = buffer;
    g_capture_callback = NULL;
    
#ifdef ESP32
    // Enable ADC
    i2s_adc_enable(I2S_NUM);
    
    // Create audio task if not running
    if (g_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", 4096, NULL, 10, &g_audio_task_handle);
    }
#endif
    
    g_state = AUDIO_STATE_RECORDING;
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Recording started");
    return true;
}

bool audio_start_recording_callback(audio_capture_callback_t callback) {
    if (!g_initialized || !callback) return false;
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_capture_callback = callback;
    g_record_buffer = NULL;
    
#ifdef ESP32
    i2s_adc_enable(I2S_NUM);
    
    if (g_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", 4096, NULL, 10, &g_audio_task_handle);
    }
#endif
    
    g_state = AUDIO_STATE_RECORDING;
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Recording started (callback mode)");
    return true;
}

void audio_stop_recording(void) {
    if (g_state != AUDIO_STATE_RECORDING && g_state != AUDIO_STATE_DUPLEX) {
        return;
    }
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
    
    i2s_adc_disable(I2S_NUM);
#endif
    
    g_record_buffer = NULL;
    g_capture_callback = NULL;
    
    if (g_state == AUDIO_STATE_RECORDING) {
        g_state = AUDIO_STATE_IDLE;
        
#ifdef ESP32
        if (g_audio_task_handle) {
            vTaskDelete(g_audio_task_handle);
            g_audio_task_handle = NULL;
        }
#endif
    } else {
        // Was duplex, now just playback
        g_state = AUDIO_STATE_PLAYING;
    }
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Recording stopped");
}

bool audio_is_recording(void) {
    return g_state == AUDIO_STATE_RECORDING || g_state == AUDIO_STATE_DUPLEX;
}

// =============================================================================
// Playback
// =============================================================================

bool audio_start_playback(audio_ring_buffer_t* buffer) {
    if (!g_initialized || !buffer) return false;
    
    if (g_state == AUDIO_STATE_PLAYING || g_state == AUDIO_STATE_DUPLEX) {
        return true;
    }
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_playback_buffer = buffer;
    g_playback_callback = NULL;
    
    // Enable speaker
    audio_speaker_enable(true);
    
#ifdef ESP32
    if (g_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", 4096, NULL, 10, &g_audio_task_handle);
    }
#endif
    
    if (g_state == AUDIO_STATE_RECORDING) {
        g_state = AUDIO_STATE_DUPLEX;
    } else {
        g_state = AUDIO_STATE_PLAYING;
    }
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Playback started");
    return true;
}

bool audio_start_playback_callback(audio_playback_callback_t callback) {
    if (!g_initialized || !callback) return false;
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_playback_callback = callback;
    g_playback_buffer = NULL;
    
    audio_speaker_enable(true);
    
#ifdef ESP32
    if (g_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", 4096, NULL, 10, &g_audio_task_handle);
    }
#endif
    
    if (g_state == AUDIO_STATE_RECORDING) {
        g_state = AUDIO_STATE_DUPLEX;
    } else {
        g_state = AUDIO_STATE_PLAYING;
    }
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Playback started (callback mode)");
    return true;
}

void audio_stop_playback(void) {
    if (g_state != AUDIO_STATE_PLAYING && g_state != AUDIO_STATE_DUPLEX) {
        return;
    }
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_playback_buffer = NULL;
    g_playback_callback = NULL;
    
    audio_speaker_enable(false);
    
    if (g_state == AUDIO_STATE_PLAYING) {
        g_state = AUDIO_STATE_IDLE;
        
#ifdef ESP32
        if (g_audio_task_handle) {
            vTaskDelete(g_audio_task_handle);
            g_audio_task_handle = NULL;
        }
#endif
    } else {
        // Was duplex, now just recording
        g_state = AUDIO_STATE_RECORDING;
    }
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Playback stopped");
}

bool audio_is_playing(void) {
    return g_state == AUDIO_STATE_PLAYING || g_state == AUDIO_STATE_DUPLEX;
}

// =============================================================================
// Duplex
// =============================================================================

bool audio_start_duplex(audio_ring_buffer_t* record_buffer,
                       audio_ring_buffer_t* playback_buffer) {
    if (!g_initialized) return false;
    
#ifdef ESP32
    xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
#endif
    
    g_record_buffer = record_buffer;
    g_playback_buffer = playback_buffer;
    
#ifdef ESP32
    i2s_adc_enable(I2S_NUM);
#endif
    
    audio_speaker_enable(true);
    
#ifdef ESP32
    if (g_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", 4096, NULL, 10, &g_audio_task_handle);
    }
#endif
    
    g_state = AUDIO_STATE_DUPLEX;
    
#ifdef ESP32
    xSemaphoreGive(g_audio_mutex);
#endif
    
    LOG_INFO("Duplex mode started");
    return true;
}

void audio_stop_duplex(void) {
    audio_stop_recording();
    audio_stop_playback();
    g_state = AUDIO_STATE_IDLE;
}

// =============================================================================
// Volume & Gain
// =============================================================================

void audio_set_input_gain(uint8_t gain) {
    if (gain > 100) gain = 100;
    g_input_gain = gain;
}

void audio_set_output_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    g_output_volume = volume;
}

uint8_t audio_get_input_gain(void) {
    return g_input_gain;
}

uint8_t audio_get_output_volume(void) {
    return g_output_volume;
}

void audio_set_mute(bool mute) {
    g_muted = mute;
    
    if (mute) {
        audio_speaker_enable(false);
    } else if (g_state == AUDIO_STATE_PLAYING || g_state == AUDIO_STATE_DUPLEX) {
        audio_speaker_enable(true);
    }
}

bool audio_is_muted(void) {
    return g_muted;
}

// =============================================================================
// Levels & Statistics
// =============================================================================

uint16_t audio_get_input_level(void) {
    return g_current_input_level;
}

uint16_t audio_get_output_level(void) {
    return g_current_output_level;
}

const audio_stats_t* audio_get_stats(void) {
    return &g_stats;
}

void audio_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

// =============================================================================
// Processing Controls
// =============================================================================

void audio_enable_noise_gate(bool enable) {
    g_noise_gate_enabled = enable;
}

void audio_set_noise_gate_threshold(uint16_t threshold) {
    g_noise_gate_threshold = threshold;
}

void audio_enable_agc(bool enable) {
    g_agc_enabled = enable;
}

// =============================================================================
// Utility
// =============================================================================

audio_state_t audio_get_state(void) {
    return g_state;
}

void audio_update(void) {
    // Called from main loop - nothing needed for ESP32 (uses task)
#ifndef ESP32
    // Simulator: simulate some audio processing
    (void)0;
#endif
}

void audio_speaker_enable(bool enable) {
#ifdef ESP32
    gpio_set_level(PIN_SPEAKER_EN, enable ? 1 : 0);
#endif
    LOG_DEBUG("Speaker %s", enable ? "enabled" : "disabled");
}

// =============================================================================
// Test Functions
// =============================================================================

void audio_play_tone(uint16_t frequency, uint16_t duration_ms) {
    if (!g_initialized) return;
    
    LOG_INFO("Playing tone: %d Hz for %d ms", frequency, duration_ms);
    
#ifdef ESP32
    audio_speaker_enable(true);
    
    uint32_t samples_per_cycle = g_config.sample_rate / frequency;
    uint32_t total_samples = (g_config.sample_rate * duration_ms) / 1000;
    
    int16_t* tone_buffer = malloc(DMA_BUF_LEN * sizeof(int16_t));
    if (!tone_buffer) return;
    
    uint32_t sample_idx = 0;
    
    while (sample_idx < total_samples) {
        for (int i = 0; i < DMA_BUF_LEN && sample_idx < total_samples; i++, sample_idx++) {
            float angle = 2.0f * M_PI * (sample_idx % samples_per_cycle) / samples_per_cycle;
            int16_t value = (int16_t)(sinf(angle) * 16000 * g_output_volume / 100);
            tone_buffer[i] = value;
        }
        
        // Output via DAC
        for (int i = 0; i < DMA_BUF_LEN && i < (int)(total_samples - sample_idx + DMA_BUF_LEN); i++) {
            uint8_t dac_value = (uint8_t)((tone_buffer[i] + 32768) >> 8);
            dac_output_voltage(DAC_CHANNEL_1, dac_value);
        }
    }
    
    free(tone_buffer);
    audio_speaker_enable(false);
#endif
}

void audio_beep(void) {
    audio_play_tone(1000, 100);  // 1kHz for 100ms
}

// =============================================================================
// Internal Functions
// =============================================================================

static uint16_t calculate_rms_level(const int16_t* samples, uint16_t count) {
    if (!samples || count == 0) return 0;
    
    uint64_t sum = 0;
    for (uint16_t i = 0; i < count; i++) {
        int32_t val = samples[i];
        sum += val * val;
    }
    
    return (uint16_t)sqrtf((float)sum / count);
}

static void process_input_samples(int16_t* samples, uint16_t count) {
    // Apply gain
    int32_t gain_factor = (g_input_gain * 256) / 100;
    
    for (uint16_t i = 0; i < count; i++) {
        int32_t val = samples[i];
        val = (val * gain_factor) >> 8;
        
        // Clamp
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        
        samples[i] = (int16_t)val;
    }
    
    // Calculate input level
    g_current_input_level = calculate_rms_level(samples, count);
    
    // Update peak
    if (g_current_input_level > g_stats.peak_input_level) {
        g_stats.peak_input_level = g_current_input_level;
    }
    
    // Noise gate
    if (g_noise_gate_enabled && g_current_input_level < g_noise_gate_threshold) {
        memset(samples, 0, count * sizeof(int16_t));
    }
    
    // Simple AGC (slow attack, fast release)
    static float agc_gain = 1.0f;
    if (g_agc_enabled) {
        float target_level = 8000.0f;
        if (g_current_input_level > 0) {
            float desired_gain = target_level / g_current_input_level;
            if (desired_gain < agc_gain) {
                agc_gain = agc_gain * 0.9f + desired_gain * 0.1f;  // Fast attack
            } else {
                agc_gain = agc_gain * 0.99f + desired_gain * 0.01f;  // Slow release
            }
            
            // Limit gain
            if (agc_gain > 4.0f) agc_gain = 4.0f;
            if (agc_gain < 0.25f) agc_gain = 0.25f;
            
            for (uint16_t i = 0; i < count; i++) {
                int32_t val = (int32_t)(samples[i] * agc_gain);
                if (val > 32767) val = 32767;
                if (val < -32768) val = -32768;
                samples[i] = (int16_t)val;
            }
        }
    }
}

static void process_output_samples(int16_t* samples, uint16_t count) {
    // Apply volume
    int32_t vol_factor = g_muted ? 0 : (g_output_volume * 256) / 100;
    
    for (uint16_t i = 0; i < count; i++) {
        int32_t val = samples[i];
        val = (val * vol_factor) >> 8;
        
        // Clamp
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        
        samples[i] = (int16_t)val;
    }
    
    // Calculate output level
    g_current_output_level = calculate_rms_level(samples, count);
    
    // Update peak
    if (g_current_output_level > g_stats.peak_output_level) {
        g_stats.peak_output_level = g_current_output_level;
    }
}

// =============================================================================
// Audio Task (ESP32)
// =============================================================================

#ifdef ESP32
static void audio_task(void* param) {
    (void)param;
    
    size_t bytes_read, bytes_written;
    audio_frame_t frame;
    
    LOG_INFO("Audio task started");
    
    while (g_state != AUDIO_STATE_IDLE) {
        // Recording
        if (g_state == AUDIO_STATE_RECORDING || g_state == AUDIO_STATE_DUPLEX) {
            // Read from I2S/ADC
            esp_err_t err = i2s_read(I2S_NUM, g_dma_read_buffer, 
                                     DMA_BUF_LEN * sizeof(int16_t),
                                     &bytes_read, portMAX_DELAY);
            
            if (err == ESP_OK && bytes_read > 0) {
                uint16_t sample_count = bytes_read / sizeof(int16_t);
                
                // Process samples
                process_input_samples(g_dma_read_buffer, sample_count);
                
                // Send to buffer or callback
                if (g_record_buffer) {
                    audio_buffer_write(g_record_buffer, 
                                      (uint8_t*)g_dma_read_buffer,
                                      bytes_read, 0);
                }
                
                if (g_capture_callback) {
                    g_capture_callback(g_dma_read_buffer, sample_count);
                }
                
                g_stats.frames_captured++;
            }
        }
        
        // Playback
        if (g_state == AUDIO_STATE_PLAYING || g_state == AUDIO_STATE_DUPLEX) {
            bool have_data = false;
            
            // Get data from buffer or callback
            if (g_playback_buffer && audio_buffer_read(g_playback_buffer, &frame)) {
                memcpy(g_dma_write_buffer, frame.samples, frame.length);
                have_data = true;
            } else if (g_playback_callback) {
                have_data = g_playback_callback(g_dma_write_buffer, DMA_BUF_LEN);
            }
            
            if (have_data) {
                // Process samples
                process_output_samples(g_dma_write_buffer, DMA_BUF_LEN);
                
                // Write to DAC
                for (int i = 0; i < DMA_BUF_LEN; i++) {
                    uint8_t dac_value = (uint8_t)((g_dma_write_buffer[i] + 32768) >> 8);
                    dac_output_voltage(DAC_CHANNEL_1, dac_value);
                }
                
                g_stats.frames_played++;
            } else {
                // No data - output silence
                memset(g_dma_write_buffer, 0, DMA_BUF_LEN * sizeof(int16_t));
                g_stats.buffer_underruns++;
            }
        }
        
        // Small yield
        vTaskDelay(1);
    }
    
    LOG_INFO("Audio task ended");
    g_audio_task_handle = NULL;
    vTaskDelete(NULL);
}
#endif

