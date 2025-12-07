/**
 * @file audio.h
 * @brief דרייבר אודיו - לכידה והשמעה
 * 
 * תמיכה ב:
 * - I2S לקלט/פלט דיגיטלי
 * - ADC ללכידה אנלוגית
 * - DAC להשמעה אנלוגית
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "core/audio_buffer.h"

// =============================================================================
// Audio Configuration
// =============================================================================

#define AUDIO_SAMPLE_RATE_8K    8000
#define AUDIO_SAMPLE_RATE_16K   16000
#define AUDIO_SAMPLE_RATE_22K   22050
#define AUDIO_SAMPLE_RATE_44K   44100

#define AUDIO_BITS_8            8
#define AUDIO_BITS_16           16

#define AUDIO_DMA_BUFFER_COUNT  4
#define AUDIO_DMA_BUFFER_SIZE   512

// =============================================================================
// Audio Mode
// =============================================================================

typedef enum {
    AUDIO_MODE_NONE = 0,
    AUDIO_MODE_ADC_DAC,     // אנלוגי - ADC לקלט, DAC לפלט
    AUDIO_MODE_I2S,         // I2S דיגיטלי
    AUDIO_MODE_PDM          // PDM מיקרופון MEMS
} audio_mode_t;

// =============================================================================
// Audio State
// =============================================================================

typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_DUPLEX      // הקלטה והשמעה במקביל
} audio_state_t;

// =============================================================================
// Audio Configuration Structure
// =============================================================================

typedef struct {
    audio_mode_t mode;
    uint32_t sample_rate;
    uint8_t bits_per_sample;
    bool use_aec;           // Acoustic Echo Cancellation
    bool use_agc;           // Automatic Gain Control
    bool use_noise_gate;    // Noise Gate
    uint8_t input_gain;     // 0-100
    uint8_t output_volume;  // 0-100
} audio_config_t;

// =============================================================================
// Audio Statistics
// =============================================================================

typedef struct {
    uint32_t frames_captured;
    uint32_t frames_played;
    uint32_t buffer_overruns;
    uint32_t buffer_underruns;
    uint16_t peak_input_level;
    uint16_t peak_output_level;
    uint16_t avg_input_level;
} audio_stats_t;

// =============================================================================
// Callback Types
// =============================================================================

// נקראת כשיש frame חדש מהמיקרופון
typedef void (*audio_capture_callback_t)(const int16_t* samples, uint16_t sample_count);

// נקראת כשצריך frame חדש להשמעה
typedef bool (*audio_playback_callback_t)(int16_t* samples, uint16_t sample_count);

// =============================================================================
// API Functions - Initialization
// =============================================================================

/**
 * @brief אתחול מערכת האודיו
 * @param config הגדרות התחלתיות
 * @return true אם הצליח
 */
bool audio_init(const audio_config_t* config);

/**
 * @brief שחרור משאבי אודיו
 */
void audio_deinit(void);

/**
 * @brief בדיקה אם מערכת האודיו מאותחלת
 * @return true אם מאותחל
 */
bool audio_is_initialized(void);

/**
 * @brief קבלת הגדרות ברירת מחדל
 * @param config מבנה לאכלוס
 */
void audio_get_default_config(audio_config_t* config);

// =============================================================================
// API Functions - Recording (Capture)
// =============================================================================

/**
 * @brief התחלת הקלטה לבאפר
 * @param buffer באפר ring לאחסון
 * @return true אם הצליח
 */
bool audio_start_recording(audio_ring_buffer_t* buffer);

/**
 * @brief התחלת הקלטה עם callback
 * @param callback פונקציה שתיקרא לכל frame
 * @return true אם הצליח
 */
bool audio_start_recording_callback(audio_capture_callback_t callback);

/**
 * @brief עצירת הקלטה
 */
void audio_stop_recording(void);

/**
 * @brief בדיקה אם מקליט
 * @return true אם מקליט
 */
bool audio_is_recording(void);

// =============================================================================
// API Functions - Playback
// =============================================================================

/**
 * @brief התחלת השמעה מבאפר
 * @param buffer באפר ring לקריאה
 * @return true אם הצליח
 */
bool audio_start_playback(audio_ring_buffer_t* buffer);

/**
 * @brief התחלת השמעה עם callback
 * @param callback פונקציה שתיקרא לבקש frame
 * @return true אם הצליח
 */
bool audio_start_playback_callback(audio_playback_callback_t callback);

/**
 * @brief עצירת השמעה
 */
void audio_stop_playback(void);

/**
 * @brief בדיקה אם משמיע
 * @return true אם משמיע
 */
bool audio_is_playing(void);

// =============================================================================
// API Functions - Duplex (Recording + Playback)
// =============================================================================

/**
 * @brief התחלת מצב דופלקס - הקלטה והשמעה במקביל
 * @param record_buffer באפר להקלטה
 * @param playback_buffer באפר להשמעה
 * @return true אם הצליח
 */
bool audio_start_duplex(audio_ring_buffer_t* record_buffer,
                       audio_ring_buffer_t* playback_buffer);

/**
 * @brief עצירת מצב דופלקס
 */
void audio_stop_duplex(void);

// =============================================================================
// API Functions - Volume & Gain
// =============================================================================

/**
 * @brief הגדרת עוצמת קלט (gain של מיקרופון)
 * @param gain ערך 0-100
 */
void audio_set_input_gain(uint8_t gain);

/**
 * @brief הגדרת עוצמת פלט (ווליום רמקול)
 * @param volume ערך 0-100
 */
void audio_set_output_volume(uint8_t volume);

/**
 * @brief קבלת עוצמת קלט נוכחית
 * @return ערך 0-100
 */
uint8_t audio_get_input_gain(void);

/**
 * @brief קבלת עוצמת פלט נוכחית
 * @return ערך 0-100
 */
uint8_t audio_get_output_volume(void);

/**
 * @brief השתקת פלט
 * @param mute true להשתקה
 */
void audio_set_mute(bool mute);

/**
 * @brief בדיקה אם מושתק
 * @return true אם מושתק
 */
bool audio_is_muted(void);

// =============================================================================
// API Functions - Levels & Statistics
// =============================================================================

/**
 * @brief קבלת רמת קלט נוכחית (לתצוגת VU meter)
 * @return רמה 0-32767
 */
uint16_t audio_get_input_level(void);

/**
 * @brief קבלת רמת פלט נוכחית
 * @return רמה 0-32767
 */
uint16_t audio_get_output_level(void);

/**
 * @brief קבלת סטטיסטיקות
 * @return מצביע לסטטיסטיקות
 */
const audio_stats_t* audio_get_stats(void);

/**
 * @brief איפוס סטטיסטיקות
 */
void audio_reset_stats(void);

// =============================================================================
// API Functions - Processing
// =============================================================================

/**
 * @brief הפעלת/כיבוי Noise Gate
 * @param enable true להפעלה
 */
void audio_enable_noise_gate(bool enable);

/**
 * @brief הגדרת סף Noise Gate
 * @param threshold סף (0-32767)
 */
void audio_set_noise_gate_threshold(uint16_t threshold);

/**
 * @brief הפעלת/כיבוי AGC
 * @param enable true להפעלה
 */
void audio_enable_agc(bool enable);

// =============================================================================
// API Functions - Utility
// =============================================================================

/**
 * @brief קבלת מצב האודיו הנוכחי
 * @return מצב
 */
audio_state_t audio_get_state(void);

/**
 * @brief עדכון מערכת האודיו (קרא ב-loop הראשי)
 */
void audio_update(void);

/**
 * @brief הפעלת/כיבוי רמקול
 * @param enable true להפעלה
 */
void audio_speaker_enable(bool enable);

// =============================================================================
// Low-Level Test Functions
// =============================================================================

/**
 * @brief השמעת טון לבדיקה
 * @param frequency תדר ב-Hz
 * @param duration_ms משך במילישניות
 */
void audio_play_tone(uint16_t frequency, uint16_t duration_ms);

/**
 * @brief השמעת ביפ
 */
void audio_beep(void);

#endif // HAL_AUDIO_H

