/**
 * @file audio_buffer.c
 * @brief מימוש Ring buffer לאודיו
 */

#include "core/audio_buffer.h"
#include <string.h>

// =============================================================================
// Platform-Specific Time
// =============================================================================

#ifdef ESP32
    #include "esp_timer.h"
    #define GET_MILLIS() (esp_timer_get_time() / 1000)
#else
    #include <time.h>
    static uint32_t sim_millis(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    }
    #define GET_MILLIS() sim_millis()
#endif

// =============================================================================
// Internal State
// =============================================================================

static uint8_t g_jitter_depth = 3;  // Default jitter buffer depth

// =============================================================================
// Initialization
// =============================================================================

void audio_buffer_init(audio_ring_buffer_t* buffer) {
    if (!buffer) return;
    
    memset(buffer, 0, sizeof(audio_ring_buffer_t));
    buffer->write_idx = 0;
    buffer->read_idx = 0;
    buffer->next_sequence = 0;
    
    // Mark all frames as invalid
    for (int i = 0; i < AUDIO_BUFFER_FRAMES; i++) {
        buffer->frames[i].valid = false;
    }
}

void audio_buffer_clear(audio_ring_buffer_t* buffer) {
    if (!buffer) return;
    
    buffer->write_idx = 0;
    buffer->read_idx = 0;
    
    for (int i = 0; i < AUDIO_BUFFER_FRAMES; i++) {
        buffer->frames[i].valid = false;
    }
}

// =============================================================================
// Status Functions
// =============================================================================

bool audio_buffer_is_empty(const audio_ring_buffer_t* buffer) {
    if (!buffer) return true;
    return buffer->write_idx == buffer->read_idx;
}

bool audio_buffer_is_full(const audio_ring_buffer_t* buffer) {
    if (!buffer) return true;
    uint8_t next_write = (buffer->write_idx + 1) % AUDIO_BUFFER_FRAMES;
    return next_write == buffer->read_idx;
}

uint8_t audio_buffer_count(const audio_ring_buffer_t* buffer) {
    if (!buffer) return 0;
    
    if (buffer->write_idx >= buffer->read_idx) {
        return buffer->write_idx - buffer->read_idx;
    }
    return AUDIO_BUFFER_FRAMES - buffer->read_idx + buffer->write_idx;
}

uint8_t audio_buffer_fill_percent(const audio_ring_buffer_t* buffer) {
    uint8_t count = audio_buffer_count(buffer);
    return (count * 100) / AUDIO_BUFFER_FRAMES;
}

// =============================================================================
// Write Operations
// =============================================================================

bool audio_buffer_write(audio_ring_buffer_t* buffer, 
                        const uint8_t* samples, 
                        uint16_t length,
                        uint32_t timestamp) {
    if (!buffer || !samples) return false;
    
    // Check if buffer is full
    uint8_t next_write = (buffer->write_idx + 1) % AUDIO_BUFFER_FRAMES;
    if (next_write == buffer->read_idx) {
        // Buffer full - count overrun
        buffer->stats.buffer_overruns++;
        buffer->stats.frames_dropped++;
        return false;
    }
    
    // Get frame slot
    audio_frame_t* frame = &buffer->frames[buffer->write_idx];
    
    // Fill frame
    frame->sequence = buffer->next_sequence++;
    frame->timestamp = (timestamp != 0) ? timestamp : GET_MILLIS();
    frame->length = (length > AUDIO_FRAME_SIZE) ? AUDIO_FRAME_SIZE : length;
    memcpy(frame->samples, samples, frame->length);
    frame->valid = true;
    
    // Update statistics
    buffer->stats.frames_written++;
    uint8_t current_fill = audio_buffer_count(buffer) + 1;
    if (current_fill > buffer->stats.max_fill_level) {
        buffer->stats.max_fill_level = current_fill;
    }
    
    // Advance write index (memory barrier implicit in volatile)
    buffer->write_idx = next_write;
    
    return true;
}

bool audio_buffer_write_frame(audio_ring_buffer_t* buffer,
                              const audio_frame_t* frame) {
    if (!buffer || !frame) return false;
    
    // Check for sequence gap
    uint16_t expected_seq = buffer->stats.last_sequence + 1;
    if (frame->sequence != expected_seq && buffer->stats.frames_written > 0) {
        uint16_t gap = audio_buffer_sequence_gap(expected_seq, frame->sequence);
        buffer->stats.frames_missed += gap;
    }
    buffer->stats.last_sequence = frame->sequence;
    
    // Check if buffer is full
    uint8_t next_write = (buffer->write_idx + 1) % AUDIO_BUFFER_FRAMES;
    if (next_write == buffer->read_idx) {
        buffer->stats.buffer_overruns++;
        buffer->stats.frames_dropped++;
        return false;
    }
    
    // Copy frame
    memcpy(&buffer->frames[buffer->write_idx], frame, sizeof(audio_frame_t));
    buffer->frames[buffer->write_idx].valid = true;
    
    buffer->stats.frames_written++;
    
    // Advance write index
    buffer->write_idx = next_write;
    
    return true;
}

// =============================================================================
// Read Operations
// =============================================================================

bool audio_buffer_read(audio_ring_buffer_t* buffer, audio_frame_t* frame) {
    if (!buffer || !frame) return false;
    
    // Check if empty
    if (buffer->write_idx == buffer->read_idx) {
        buffer->stats.buffer_underruns++;
        return false;
    }
    
    // Copy frame
    memcpy(frame, &buffer->frames[buffer->read_idx], sizeof(audio_frame_t));
    buffer->frames[buffer->read_idx].valid = false;
    
    buffer->stats.frames_read++;
    
    // Advance read index
    buffer->read_idx = (buffer->read_idx + 1) % AUDIO_BUFFER_FRAMES;
    
    return true;
}

bool audio_buffer_peek(const audio_ring_buffer_t* buffer, audio_frame_t* frame) {
    if (!buffer || !frame) return false;
    
    if (buffer->write_idx == buffer->read_idx) {
        return false;
    }
    
    memcpy(frame, &buffer->frames[buffer->read_idx], sizeof(audio_frame_t));
    return true;
}

bool audio_buffer_skip(audio_ring_buffer_t* buffer) {
    if (!buffer) return false;
    
    if (buffer->write_idx == buffer->read_idx) {
        return false;
    }
    
    buffer->frames[buffer->read_idx].valid = false;
    buffer->read_idx = (buffer->read_idx + 1) % AUDIO_BUFFER_FRAMES;
    
    return true;
}

// =============================================================================
// Statistics
// =============================================================================

const audio_buffer_stats_t* audio_buffer_get_stats(const audio_ring_buffer_t* buffer) {
    if (!buffer) return NULL;
    return &buffer->stats;
}

void audio_buffer_reset_stats(audio_ring_buffer_t* buffer) {
    if (!buffer) return;
    memset(&buffer->stats, 0, sizeof(audio_buffer_stats_t));
}

// =============================================================================
// Utility Functions
// =============================================================================

uint32_t audio_buffer_duration_ms(const audio_ring_buffer_t* buffer) {
    return audio_buffer_count(buffer) * AUDIO_FRAME_DURATION_MS;
}

uint16_t audio_buffer_sequence_gap(uint16_t expected, uint16_t received) {
    // Handle wraparound
    if (received >= expected) {
        return received - expected;
    }
    // Wraparound case
    return (0xFFFF - expected) + received + 1;
}

// =============================================================================
// Jitter Buffer Functions
// =============================================================================

void audio_buffer_set_jitter_depth(audio_ring_buffer_t* buffer, uint8_t frames) {
    (void)buffer;  // Could be per-buffer in future
    g_jitter_depth = frames;
    if (g_jitter_depth > AUDIO_BUFFER_FRAMES / 2) {
        g_jitter_depth = AUDIO_BUFFER_FRAMES / 2;
    }
}

bool audio_buffer_jitter_ready(const audio_ring_buffer_t* buffer) {
    return audio_buffer_count(buffer) >= g_jitter_depth;
}

