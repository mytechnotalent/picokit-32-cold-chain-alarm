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
// File:    monitor.h
// Desc:    Declares the telemetry alarm state machine that latches a band
//          breach and drives the door latch servo.
// Created: 2026

#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Spacing between two DHT11 samples in milliseconds.
 */
#define MONITOR_READ_INTERVAL_MS 2000u

/**
 * @brief Onboard heartbeat LED on and off time in microseconds.
 */
#define MONITOR_HEARTBEAT_BLINK_US 50000u

/**
 * @brief Lower edge of the safe telemetry band in tenths of a degree.
 */
#define MONITOR_ALARM_LOW_TENTHS 20

/**
 * @brief Upper edge of the safe telemetry band in tenths of a degree.
 */
#define MONITOR_ALARM_HIGH_TENTHS 80

/**
 * @brief Servo angle in degrees that releases the door latch.
 */
#define MONITOR_DOOR_OPEN_DEGREES 90u

/**
 * @brief Servo angle in degrees that holds the door latch closed.
 */
#define MONITOR_DOOR_CLOSED_DEGREES 0u

/**
 * @brief NEC command code that acknowledges and clears the alarm.
 */
#define MONITOR_KEY_ACK 0x45u

/**
 * @brief Initialize the telemetry alarm monitor state machine.
 *
 * Configures the onboard heartbeat LED, the door latch servo, the DHT11
 * data pin, the push-button input, the VS1838B infrared receiver, and the
 * RYLR998 UART, derives the field key, and resets the latch and timing.
 *
 * @param void No parameters.
 * @return bool true when all submodules initialized.
 */
bool monitor_init(void);

/**
 * @brief Clear the monitor-ready flag.
 *
 * Test and recovery hook that returns the state machine to the
 * uninitialized policy state.
 *
 * @param void No parameters.
 * @return void
 */
void monitor_deinit(void);

/**
 * @brief Execute one monitor state-machine tick.
 *
 * Samples the DHT11 on the read interval, latches the alarm when the
 * reading leaves the safe band, drives the door latch, acknowledges the
 * alarm from the button or an infrared key, transmits the authenticated
 * heartbeat on the telemetry interval, and pumps inbound +RCV lines.
 *
 * @param void No parameters.
 * @return bool true when the tick completed without a policy error.
 */
bool monitor_step(void);

#endif // MONITOR_H
