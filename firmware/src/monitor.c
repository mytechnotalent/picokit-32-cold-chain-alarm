// MIT License
//
// Copyright (c) 2026 Kevin Thomas
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Author:  Kevin Thomas
// Email:   kevin@mytechnotalent.com
// GitHub:  https://github.com/mytechnotalent/picokit-32-cold-chain-alarm
// File:    monitor.c
// Desc:    Implements the telemetry alarm state machine that latches a band
//          breach and drives the door latch servo.
// Created: 2026

#include "picokit_32_cold_chain_alarm.h"
#include "monitor.h"
#include "radio.h"
#include "status_led.h"
#include "servo.h"
#include "ir_remote.h"
#include "button.h"
#include "sensor.h"
#include "ccm.h"
#include "envelope.h"
#include "field_secrets.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Module-ready flag.
 *
 * Set to true by monitor_init() once the peripherals are configured.
 * monitor_step() returns false while this flag is clear.
 */
static bool g_ready;

/**
 * @brief Latched alarm flag, cleared only by an acknowledge.
 */
static bool g_alarm;

/**
 * @brief Most recent temperature in tenths of a degree Celsius.
 */
static int16_t g_temperature;

/**
 * @brief Current door latch angle in degrees.
 */
static uint8_t g_door;

/**
 * @brief True once a valid temperature reading has been seen.
 */
static bool g_valid;

/**
 * @brief Monotonic transmit sequence number.
 */
static uint16_t g_seq;

/**
 * @brief Absolute time in microseconds of the next DHT11 sample.
 */
static uint64_t g_next_read_us;

/**
 * @brief Absolute time in microseconds of the next authenticated transmit.
 */
static uint64_t g_next_tx_us;

/**
 * @brief Most recent decoded DHT11 reading.
 */
static dht_reading_t g_reading;

/**
 * @brief Inbound radio line accumulator.
 */
static char g_rx_line[RADIO_LINE_BUF_LEN];

/**
 * @brief Number of bytes currently held in the inbound line accumulator.
 */
static size_t g_rx_len;

/**
 * @brief AES-128 session key for telemetry.
 */
static uint8_t g_key[CCM_KEY_LEN];

/**
 * @brief True once the telemetry session key has been loaded.
 */
static bool g_key_ready;

/**
 * @brief Configure the onboard heartbeat LED as a dark output.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init_io(void) {
    gpio_init(PICOKIT_32_COLD_CHAIN_ALARM_LED_PIN);
    gpio_set_dir(PICOKIT_32_COLD_CHAIN_ALARM_LED_PIN, GPIO_OUT);
    gpio_put(PICOKIT_32_COLD_CHAIN_ALARM_LED_PIN, 0);
}

/**
 * @brief Reset the latch, reading, and door latch position.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_reset_control(void) {
    g_alarm = false;
    g_temperature = 0;
    g_door = MONITOR_DOOR_CLOSED_DEGREES;
    g_valid = false;
}

/**
 * @brief Reset the control state, sequence, and transmit timing.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init(void) {
    uint64_t now_us = time_us_64();
    monitor_state_reset_control();
    memset(&g_reading, 0, sizeof(g_reading));
    g_seq = 0u;
    g_next_read_us = now_us;
    g_next_tx_us = now_us + (uint64_t)PICOKIT_32_COLD_CHAIN_ALARM_TX_INTERVAL_MS * 1000u;
    g_ready = true;
}

/**
 * @brief Load the telemetry session key from the field secret.
 *
 * LAB-ONLY: production must provision the session key through OTP rather
 * than embedding a committed key.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_load_key(void) {
    static const uint8_t key[CCM_KEY_LEN] = FIELD_SECRET_KEY;
    memcpy(g_key, key, CCM_KEY_LEN);
    g_key_ready = true;
}

/**
 * @brief Print the boot banner for the telemetry alarm lesson.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_banner(void) {
    printf("=== PICOKIT-32 COLD CHAIN ALARM // LATCHED BAND + AUTHENTICATED HEARTBEAT ===\n");
}

/**
 * @brief Derive the field key and announce a ready monitor.
 *
 * @param void No parameters.
 * @return bool true when the field key was derived and installed.
 */
static bool monitor_finish(void) {
    monitor_load_key();
    monitor_banner();
    return true;
}

/**
 * @brief Blink the onboard heartbeat LED exactly once.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_heartbeat(void) {
    gpio_put(PICOKIT_32_COLD_CHAIN_ALARM_LED_PIN, 1);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
    gpio_put(PICOKIT_32_COLD_CHAIN_ALARM_LED_PIN, 0);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
}

/**
 * @brief Drive the door latch to match the latched alarm state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_drive_door(void) {
    g_door = g_alarm ? MONITOR_DOOR_OPEN_DEGREES : MONITOR_DOOR_CLOSED_DEGREES;
    servo_set_angle(g_door);
}

/**
 * @brief Print the current temperature, alarm, and door state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_log_state(void) {
    printf("TEMP %d ALARM %u DOOR %u\n", (int)g_temperature, (unsigned)g_alarm, (unsigned)g_door);
}

/**
 * @brief Latch the alarm when a valid reading leaves the safe band.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_evaluate(void) {
    if (!g_valid) {
        return;
    }
    if (g_temperature < MONITOR_ALARM_LOW_TENTHS || g_temperature > MONITOR_ALARM_HIGH_TENTHS) {
        g_alarm = true;
    }
}

/**
 * @brief Sample the DHT11, latch the band, and schedule the next read.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_read_tick(uint64_t now_us) {
    sensor_result_t rc = sensor_read(&g_reading);
    g_valid = (rc == SENSOR_RESULT_OK);
    if (g_valid) {
        g_temperature = g_reading.temperature_tenths;
    }
    monitor_evaluate();
    monitor_drive_door();
    monitor_log_state();
    g_next_read_us = now_us + (uint64_t)MONITOR_READ_INTERVAL_MS * 1000u;
}

/**
 * @brief Acknowledge the alarm, clear the latch, and close the door.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_ack(void) {
    g_alarm = false;
    monitor_drive_door();
    printf("ACK\n");
}

/**
 * @brief Consume one acknowledge press from the push-button.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_poll_button(void) {
    if (button_consume_press()) {
        monitor_ack();
    }
}

/**
 * @brief Apply one decoded infrared remote command to the alarm latch.
 *
 * @param cmd Decoded eight-bit remote command code.
 * @return void
 */
static void monitor_ir_apply(uint8_t cmd) {
    if (cmd == MONITOR_KEY_ACK) {
        monitor_ack();
    }
}

/**
 * @brief Poll the infrared eye and apply any remote acknowledge.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_poll_ir(void) {
    ir_command_t cmd;
    if (ir_remote_poll(&cmd)) {
        monitor_ir_apply(cmd.command);
    }
}

/**
 * @brief Poll the button and the infrared eye for an acknowledge.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_input_tick(void) {
    monitor_poll_button();
    monitor_poll_ir();
}

/**
 * @brief Format the heartbeat JSON body for the alarm and temperature.
 *
 * @param frame Pointer to the mutable frame output buffer.
 * @param frame_len Capacity of the frame output buffer in bytes.
 * @return size_t Number of JSON bytes written, or zero on overflow.
 */
static size_t monitor_build_frame(char *frame, size_t frame_len) {
    int written = snprintf(frame, frame_len, "{\"n\":%u,\"s\":%u,\"a\":%u,\"t\":%d}", (unsigned)PACKET_NODE_ID, (unsigned)g_seq, (unsigned)g_alarm, (int)g_temperature);
    return (written > 0 && (size_t)written < frame_len) ? (size_t)written : 0u;
}

/**
 * @brief Seal the current heartbeat body into a hex envelope.
 *
 * @param hex Pointer to the NUL-terminated hex output buffer.
 * @param hex_len Capacity of the hex output buffer in bytes.
 * @return bool true when the heartbeat was sealed and encoded.
 */
static bool monitor_seal_frame(char *hex, size_t hex_len) {
    char frame[PICOKIT_32_COLD_CHAIN_ALARM_FRAME_SIZE];
    uint8_t nonce[ENVELOPE_NONCE_LEN];
    uint8_t ad = (uint8_t)PACKET_NODE_ID;
    size_t frame_len = monitor_build_frame(frame, sizeof(frame));
    envelope_fill_nonce(nonce);
    return envelope_seal_hex(g_key, nonce, &ad, 1u, (const uint8_t *)frame, frame_len, hex, hex_len);
}

/**
 * @brief Build and transmit the authenticated heartbeat frame.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_transmit(void) {
    char hex[ENVELOPE_MAX_HEX_LEN];
    if (!g_key_ready) {
        return;
    }
    if (monitor_seal_frame(hex, sizeof(hex))) {
        radio_send_frame(PICOKIT_32_COLD_CHAIN_ALARM_UART, (const uint8_t *)hex, strlen(hex));
        g_seq += 1u;
    }
}

/**
 * @brief Transmit one heartbeat and schedule the next transmit.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_tx_tick(uint64_t now_us) {
    monitor_heartbeat();
    monitor_transmit();
    g_next_tx_us = now_us + (uint64_t)PICOKIT_32_COLD_CHAIN_ALARM_TX_INTERVAL_MS * 1000u;
}

/**
 * @brief Drain inbound radio lines and log every valid +RCV report.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_rx_tick(void) {
    radio_rcv_t rcv;
    while (radio_line_pump(PICOKIT_32_COLD_CHAIN_ALARM_UART, g_rx_line, &g_rx_len)) {
        if (radio_parse_rcv(g_rx_line, &rcv) == RADIO_RESULT_OK) {
            printf("RX from 0x%04X, %u bytes\n", (unsigned)rcv.sender, (unsigned)rcv.len);
        }
    }
}

/**
 * @brief Service the DHT11 sample and heartbeat transmit timers.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_service_timers(uint64_t now_us) {
    if (now_us >= g_next_read_us) {
        monitor_read_tick(now_us);
    }
    if (now_us >= g_next_tx_us) {
        monitor_tx_tick(now_us);
    }
}

bool monitor_init(void) {
    bool ok;
    ok = status_led_init() && radio_init(PICOKIT_32_COLD_CHAIN_ALARM_UART);
    ok = ok && ir_remote_init() && button_init();
    ok = ok && servo_init() && sensor_init();
    monitor_state_init_io();
    monitor_state_init();
    return ok && monitor_finish();
}

void monitor_deinit(void) {
    g_ready = false;
}

bool monitor_step(void) {
    uint64_t now_us;
    if (!g_ready) {
        return false;
    }
    now_us = time_us_64();
    monitor_service_timers(now_us);
    monitor_input_tick();
    monitor_rx_tick();
    return true;
}
