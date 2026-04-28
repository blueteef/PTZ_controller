#include <Arduino.h>
#include <Wire.h>
#include <driver/twai.h>
#include <TinyGPS++.h>
#include "can_ids.h"
#include "can_frames.h"
#include "config.h"
#include "motion.h"
#include "sensors.h"

// ---------------------------------------------------------------------------
// Telemetry intervals (ms)
// ---------------------------------------------------------------------------
#define INTERVAL_POS_REPORT     50      // 20 Hz
#define INTERVAL_IMU            20      // 50 Hz
#define INTERVAL_MAG           200      // 5 Hz
#define INTERVAL_ENV          1000      // 1 Hz
#define INTERVAL_GPS          1000      // 1 Hz
#define INTERVAL_HEARTBEAT    1000      // 1 Hz
#define CAN_TIMEOUT_MS        15000     // estop if no Pi heartbeat for 15s (allows Pi boot time)

// ---------------------------------------------------------------------------
// GPS
// ---------------------------------------------------------------------------
static TinyGPSPlus  _gps;
static HardwareSerial _gps_serial(1);   // SERIAL1

// ---------------------------------------------------------------------------
// TWAI (CAN)
// ---------------------------------------------------------------------------
static void can_init() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL
    );
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
    Serial.println("[CAN] TWAI started at 500kbps");
}

static void can_send(uint32_t arb_id, const void *data, uint8_t len) {
    twai_message_t msg = {};
    msg.identifier     = arb_id;
    msg.data_length_code = len;
    msg.extd           = 0;
    if (len && data) memcpy(msg.data, data, len);
    twai_transmit(&msg, pdMS_TO_TICKS(5));
}

// ---------------------------------------------------------------------------
// Outbound telemetry helpers
// ---------------------------------------------------------------------------
static void send_heartbeat() {
    can_send(CAN_ID(NODE_STATIONARY, MSG_HEARTBEAT), nullptr, 0);
}

static void send_pos_report() {
    FramePosReport f;
    f.axis        = AXIS_PAN;
    f.pos_cdeg    = motion_get_pos_cdeg();
    f.vel_cdeg_s  = motion_get_vel_cdeg_s();
    f.flags       = motion_get_flags();
    can_send(CAN_ID(NODE_STATIONARY, MSG_POS_REPORT), &f, sizeof(f));
}

static void send_imu() {
    FrameImu f;
    if (!sensors_get_imu(f.roll_cdeg, f.pitch_cdeg, f.yaw_cdeg)) return;
    can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_IMU), &f, sizeof(f));
}

static void send_mag() {
    FrameMag f;
    f.ok = sensors_get_mag(f.hdg_cdeg) ? 1 : 0;
    if (!f.ok) return;
    can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_MAG), &f, sizeof(f));
}

static void send_env() {
    FrameEnv f;
    if (!sensors_get_env(f.temp_cdeg, f.press_hPa)) return;
    can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_ENV), &f, sizeof(f));

    FramePower p;
    if (sensors_get_power(p.voltage_mv, p.current_ma))
        can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_POWER), &p, sizeof(p));
}

static void send_gps() {
    if (!_gps.location.isValid()) return;

    FrameGps g;
    g.lat_1e6 = (int32_t)(_gps.location.lat() * 1e6);
    g.lon_1e6 = (int32_t)(_gps.location.lng() * 1e6);
    can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_GPS), &g, sizeof(g));

    FrameGps2 g2;
    g2.fix     = _gps.location.isValid() ? 1 : 0;
    g2.sats    = (uint8_t)_gps.satellites.value();
    g2.hdg_cdeg = (int16_t)(_gps.course.deg() * 100.0);
    g2.spd_cm_s = (int16_t)(_gps.speed.mps() * 100.0);
    can_send(CAN_ID(NODE_STATIONARY, MSG_SENSOR_GPS2), &g2, sizeof(g2));
}

static void send_fault(uint8_t code) {
    FrameFault f = { NODE_STATIONARY, code };
    can_send(CAN_ID(NODE_STATIONARY, MSG_FAULT), &f, sizeof(f));
}

// ---------------------------------------------------------------------------
// Inbound CAN handler
// ---------------------------------------------------------------------------
static uint32_t _last_pi_heartbeat_ms = 0;

static void handle_can_rx() {
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        uint8_t  src  = CAN_ID_NODE(msg.identifier);
        uint8_t  type = CAN_ID_MSG_TYPE(msg.identifier);

        if (src != NODE_PI) continue;   // only accept commands from Pi

        switch (type) {
            case MSG_HEARTBEAT:
                _last_pi_heartbeat_ms = millis();
                motion_clear_can_fault();   // recover from CAN timeout fault when Pi reconnects
                break;

            case MSG_ESTOP:
                motion_estop();
                break;

            case MSG_STOP: {
                FrameStop *f = (FrameStop *)msg.data;
                if (f->axis == AXIS_PAN || f->axis == AXIS_ALL)
                    motion_stop();
                break;
            }

            case MSG_VEL_CMD: {
                FrameVelCmd *f = (FrameVelCmd *)msg.data;
                if (f->axis == AXIS_PAN)
                    motion_set_velocity(f->vel_cdeg_s);
                break;
            }

            case MSG_POS_CMD: {
                FramePosCmd *f = (FramePosCmd *)msg.data;
                if (f->axis == AXIS_PAN)
                    motion_set_position(f->pos_cdeg);
                break;
            }

            case MSG_HOME_CMD: {
                FrameHomeCmd *f = (FrameHomeCmd *)msg.data;
                if (f->axis == AXIS_PAN || f->axis == AXIS_ALL)
                    motion_home();
                break;
            }

            case MSG_SETTINGS: {
                FrameSettings *f = (FrameSettings *)msg.data;
                motion_set_settings(f->max_speed_cdeg_s, f->accel_cdeg_s2);
                break;
            }

        }
    }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("[stationary] booting...");

    can_init();
    motion_init();

    sensors_init();

    // Init GPS — send config at all common baud rates to ensure it takes,
    // then reopen at target baud. CASIC protocol for ATGM336H.
    const uint32_t try_bauds[] = { 9600, 38400, 57600, 4800 };
    for (uint32_t baud : try_bauds) {
        _gps_serial.begin(baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        delay(100);
        // Set baud to 115200
        _gps_serial.print("$PCAS01,5*19\r\n");
        delay(100);
        _gps_serial.end();
    }
    // Now open at target baud
    _gps_serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(200);
    // Set update rate to 10Hz (100ms interval)
    _gps_serial.print("$PCAS02,100*1E\r\n");
    delay(100);
    Serial.println("[GPS] configured: 115200 baud, 10Hz");

    _last_pi_heartbeat_ms = millis();   // grace period on boot
    Serial.println("[stationary] ready");
}

void loop() {
    uint32_t now = millis();

    // ── GPS feed ──────────────────────────────────────────────────────
    while (_gps_serial.available())
        _gps.encode(_gps_serial.read());

    // ── CAN receive ───────────────────────────────────────────────────
    handle_can_rx();

    // ── Pi heartbeat watchdog — estop if Pi goes silent ───────────────
    if (now - _last_pi_heartbeat_ms > CAN_TIMEOUT_MS) {
        motion_estop();
        send_fault(FAULT_CAN_TIMEOUT);
        _last_pi_heartbeat_ms = now;   // throttle fault frames
    }

    // ── Motion tick (encoder update, position loop) ───────────────────
    motion_tick();

    // ── Scheduled telemetry ───────────────────────────────────────────
    static uint32_t t_pos = 0, t_imu = 0, t_mag = 0, t_env = 0,
                    t_gps = 0, t_hb  = 0;

    if (now - t_pos >= INTERVAL_POS_REPORT) {
        t_pos = now;
        send_pos_report();
        Serial.printf("[enc] raw=%u pos=%ld\n", motion_get_enc_raw(), motion_get_pos_cdeg());
    }
    if (now - t_imu >= INTERVAL_IMU)        { t_imu = now; send_imu(); }
    if (now - t_mag >= INTERVAL_MAG)        { t_mag = now; send_mag(); }
    if (now - t_env >= INTERVAL_ENV)        { t_env = now; send_env(); }
    if (now - t_gps >= INTERVAL_GPS)        { t_gps = now; send_gps(); }
    if (now - t_hb  >= INTERVAL_HEARTBEAT)  { t_hb  = now; send_heartbeat(); }

}
