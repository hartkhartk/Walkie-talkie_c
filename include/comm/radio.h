/**
 * @file radio.h
 * @brief דרייבר LoRa SX1276/RFM95 לתקשורת אלחוטית
 */

#ifndef COMM_RADIO_H
#define COMM_RADIO_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Radio Constants
// =============================================================================

#define RADIO_MAX_PACKET_SIZE   255
#define RADIO_FIFO_SIZE         256

// SX1276 Register Addresses
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PA_CONFIG           0x09
#define REG_PA_RAMP             0x0A
#define REG_OCP                 0x0B
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE_ADDR   0x0E
#define REG_FIFO_RX_BASE_ADDR   0x0F
#define REG_FIFO_RX_CURRENT     0x10
#define REG_IRQ_FLAGS_MASK      0x11
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_PKT_SNR_VALUE       0x19
#define REG_PKT_RSSI_VALUE      0x1A
#define REG_RSSI_VALUE          0x1B
#define REG_MODEM_CONFIG_1      0x1D
#define REG_MODEM_CONFIG_2      0x1E
#define REG_PREAMBLE_MSB        0x20
#define REG_PREAMBLE_LSB        0x21
#define REG_PAYLOAD_LENGTH      0x22
#define REG_MODEM_CONFIG_3      0x26
#define REG_FREQ_ERROR_MSB      0x28
#define REG_FREQ_ERROR_MID      0x29
#define REG_FREQ_ERROR_LSB      0x2A
#define REG_RSSI_WIDEBAND       0x2C
#define REG_DETECTION_OPTIMIZE  0x31
#define REG_INVERTIQ            0x33
#define REG_DETECTION_THRESHOLD 0x37
#define REG_SYNC_WORD           0x39
#define REG_INVERTIQ2           0x3B
#define REG_DIO_MAPPING_1       0x40
#define REG_DIO_MAPPING_2       0x41
#define REG_VERSION             0x42
#define REG_PA_DAC              0x4D

// Operation Modes
#define MODE_LONG_RANGE_MODE    0x80
#define MODE_SLEEP              0x00
#define MODE_STDBY              0x01
#define MODE_TX                 0x03
#define MODE_RX_CONTINUOUS      0x05
#define MODE_RX_SINGLE          0x06
#define MODE_CAD                0x07

// IRQ Flags
#define IRQ_TX_DONE_MASK        0x08
#define IRQ_RX_DONE_MASK        0x40
#define IRQ_PAYLOAD_CRC_ERROR   0x20
#define IRQ_VALID_HEADER        0x10
#define IRQ_CAD_DONE            0x04
#define IRQ_CAD_DETECTED        0x01

// PA Config
#define PA_BOOST                0x80

// =============================================================================
// Radio State
// =============================================================================

typedef enum {
    RADIO_STATE_IDLE = 0,
    RADIO_STATE_TX,
    RADIO_STATE_RX,
    RADIO_STATE_CAD,
    RADIO_STATE_SLEEP
} radio_state_t;

// =============================================================================
// Radio Configuration
// =============================================================================

typedef struct {
    uint32_t frequency;         // Hz (e.g., 433000000)
    int8_t   tx_power;          // dBm (2-20)
    uint32_t bandwidth;         // Hz (7800-500000)
    uint8_t  spreading_factor;  // 6-12
    uint8_t  coding_rate;       // 5-8 (4/5 to 4/8)
    uint16_t preamble_length;   // symbols
    uint8_t  sync_word;         // sync word (default 0x12)
    bool     crc_enabled;       // enable CRC
    bool     implicit_header;   // implicit header mode
} radio_config_t;

// =============================================================================
// Radio Statistics
// =============================================================================

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t crc_errors;
    uint32_t tx_timeouts;
    uint32_t rx_timeouts;
    int16_t  last_rssi;
    int8_t   last_snr;
} radio_stats_t;

// =============================================================================
// Callback Types
// =============================================================================

typedef void (*radio_rx_callback_t)(const uint8_t* data, uint8_t length, int16_t rssi, int8_t snr);
typedef void (*radio_tx_callback_t)(bool success);

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מודול הרדיו
 * @return true אם האתחול הצליח
 */
bool radio_init(void);

/**
 * @brief בדיקה אם הרדיו מאותחל ותקין
 * @return true אם הרדיו עובד
 */
bool radio_is_ready(void);

/**
 * @brief הגדרת תצורת הרדיו
 * @param config מבנה הגדרות
 */
void radio_set_config(const radio_config_t* config);

/**
 * @brief קבלת הגדרות ברירת מחדל
 * @param config מבנה לאכלוס
 */
void radio_get_default_config(radio_config_t* config);

/**
 * @brief הגדרת תדר
 * @param frequency תדר ב-Hz
 */
void radio_set_frequency(uint32_t frequency);

/**
 * @brief הגדרת עוצמת שידור
 * @param power עוצמה ב-dBm (2-20)
 */
void radio_set_tx_power(int8_t power);

/**
 * @brief הגדרת רוחב פס
 * @param bandwidth רוחב פס ב-Hz
 */
void radio_set_bandwidth(uint32_t bandwidth);

/**
 * @brief הגדרת Spreading Factor
 * @param sf ערך (6-12)
 */
void radio_set_spreading_factor(uint8_t sf);

/**
 * @brief שליחת חבילה
 * @param data נתונים לשליחה
 * @param length אורך הנתונים
 * @return true אם השליחה החלה בהצלחה
 */
bool radio_send(const uint8_t* data, uint8_t length);

/**
 * @brief שליחת חבילה עם המתנה לסיום
 * @param data נתונים לשליחה
 * @param length אורך הנתונים
 * @param timeout_ms זמן המתנה מקסימלי
 * @return true אם השליחה הצליחה
 */
bool radio_send_blocking(const uint8_t* data, uint8_t length, uint32_t timeout_ms);

/**
 * @brief התחלת האזנה רציפה
 */
void radio_start_receive(void);

/**
 * @brief האזנה לחבילה בודדת
 * @param timeout_ms זמן המתנה מקסימלי (0 = אינסופי)
 */
void radio_receive_single(uint32_t timeout_ms);

/**
 * @brief עצירת האזנה
 */
void radio_stop_receive(void);

/**
 * @brief בדיקת ערוץ פנוי (CAD)
 * @return true אם הערוץ פנוי
 */
bool radio_channel_is_free(void);

/**
 * @brief קריאת חבילה שהתקבלה
 * @param buffer באפר לאחסון
 * @param max_length גודל הבאפר
 * @return אורך החבילה שנקראה (0 אם אין)
 */
uint8_t radio_read_packet(uint8_t* buffer, uint8_t max_length);

/**
 * @brief קבלת RSSI אחרון
 * @return RSSI ב-dBm
 */
int16_t radio_get_rssi(void);

/**
 * @brief קבלת SNR אחרון
 * @return SNR ב-dB
 */
int8_t radio_get_snr(void);

/**
 * @brief קבלת מצב הרדיו
 * @return מצב נוכחי
 */
radio_state_t radio_get_state(void);

/**
 * @brief קבלת סטטיסטיקות
 * @return מבנה סטטיסטיקות
 */
const radio_stats_t* radio_get_stats(void);

/**
 * @brief איפוס סטטיסטיקות
 */
void radio_reset_stats(void);

/**
 * @brief רישום callback לקבלת חבילות
 * @param callback פונקציית callback
 */
void radio_set_rx_callback(radio_rx_callback_t callback);

/**
 * @brief רישום callback לסיום שידור
 * @param callback פונקציית callback
 */
void radio_set_tx_callback(radio_tx_callback_t callback);

/**
 * @brief טיפול בפסיקות (קרא ב-ISR או ב-loop)
 */
void radio_handle_interrupt(void);

/**
 * @brief עדכון מצב הרדיו (קרא ב-loop הראשי)
 */
void radio_update(void);

/**
 * @brief כניסה למצב שינה
 */
void radio_sleep(void);

/**
 * @brief יציאה ממצב שינה
 */
void radio_wake(void);

#endif // COMM_RADIO_H

