
#include <stdbool.h>
#include <stdint.h>

#include <stdlib.h>

#include "platform.h"

#include "gpio_common.h"
#include "timer_common.h"

#include "pwm_output.h"
#include "pwm_rssi.h"
#include "pwm_rx.h"
#include "pwm_mapping.h"
/*
    Configuration maps

    Note: this documentation is only valid for STM32F10x, for STM32F30x please read the code itself.

    1) multirotor PPM input
    PWM1 used for PPM
    PWM5..8 used for motors
    PWM9..10 used for servo or else motors
    PWM11..14 used for motors

    2) multirotor PPM input with more servos
    PWM1 used for PPM
    PWM5..8 used for motors
    PWM9..10 used for servo or else motors
    PWM11..14 used for servos

    2) multirotor PWM input
    PWM1..8 used for input
    PWM9..10 used for servo or else motors
    PWM11..14 used for motors

    3) airplane / flying wing w/PWM
    PWM1..8 used for input
    PWM9 used for motor throttle +PWM10 for 2nd motor
    PWM11.14 used for servos

    4) airplane / flying wing with PPM
    PWM1 used for PPM
    PWM5..8 used for servos
    PWM9 used for motor throttle +PWM10 for 2nd motor
    PWM11.14 used for servos
*/

enum {
    TYPE_IP = 1,
    TYPE_IW,
    TYPE_IR,
    TYPE_M,
    TYPE_S,
};

static const uint16_t multiPPM[] = {
    PWM1  | (TYPE_IP << 8),     // PPM input
    PWM2  | (TYPE_IR << 8),      // PWM RSSI input
    PWM9  | (TYPE_M << 8),      // Swap to servo if needed
    PWM10 | (TYPE_M << 8),     // Swap to servo if needed
    PWM11 | (TYPE_M << 8),
    PWM12 | (TYPE_M << 8),
    PWM13 | (TYPE_M << 8),
    PWM14 | (TYPE_M << 8),
    PWM5  | (TYPE_M << 8),      // Swap to servo if needed
    PWM6  | (TYPE_M << 8),      // Swap to servo if needed
    PWM7  | (TYPE_M << 8),      // Swap to servo if needed
    PWM8  | (TYPE_M << 8),      // Swap to servo if needed
    0xFFFF
};

static const uint16_t multiPWM[] = {
    PWM1  | (TYPE_IW << 8),     // input #1
    PWM2  | (TYPE_IW << 8),
    PWM3  | (TYPE_IW << 8),
    PWM4  | (TYPE_IW << 8),
    PWM5  | (TYPE_IW << 8),
    PWM6  | (TYPE_IW << 8),
    PWM7  | (TYPE_IW << 8),
    PWM8  | (TYPE_IW << 8),     // input #8
    PWM9  | (TYPE_M  << 8),      // motor #1 or servo #1 (swap to servo if needed)
    PWM10 | (TYPE_M  << 8),     // motor #2 or servo #2 (swap to servo if needed)
    PWM11 | (TYPE_M  << 8),     // motor #1 or #3
    PWM12 | (TYPE_M  << 8),
    PWM13 | (TYPE_M  << 8),
    PWM14 | (TYPE_M  << 8),     // motor #4 or #6
    0xFFFF
};

static const uint16_t airPPM[] = {
    PWM1  | (TYPE_IP << 8),     // PPM input
    PWM2  | (TYPE_IR << 8),     // PWM RSSI input
    PWM9  | (TYPE_M  << 8),      // motor #1
    PWM10 | (TYPE_M  << 8),     // motor #2
    PWM11 | (TYPE_S  << 8),     // servo #1
    PWM12 | (TYPE_S  << 8),
    PWM13 | (TYPE_S  << 8),
    PWM14 | (TYPE_S  << 8),     // servo #4
    PWM5  | (TYPE_S  << 8),      // servo #5
    PWM6  | (TYPE_S  << 8),
    PWM7  | (TYPE_S  << 8),
    PWM8  | (TYPE_S  << 8),      // servo #8
    0xFFFF
};

static const uint16_t airPWM[] = {
    PWM1  | (TYPE_IW << 8),     // input #1
    PWM2  | (TYPE_IW << 8),
    PWM3  | (TYPE_IW << 8),
    PWM4  | (TYPE_IW << 8),
    PWM5  | (TYPE_IW << 8),
    PWM6  | (TYPE_IW << 8),
    PWM7  | (TYPE_IW << 8),
    PWM8  | (TYPE_IW << 8),     // input #8
    PWM9  | (TYPE_M  << 8),      // motor #1
    PWM10 | (TYPE_M  << 8),     // motor #2
    PWM11 | (TYPE_S  << 8),     // servo #1
    PWM12 | (TYPE_S  << 8),
    PWM13 | (TYPE_S  << 8),
    PWM14 | (TYPE_S  << 8),     // servo #4
    0xFFFF
};

static const uint16_t * const hardwareMaps[] = {
    multiPWM,
    multiPPM,
    airPWM,
    airPPM,
};

void pwmInit(drv_pwm_config_t *init)
{
    int i = 0;
    const uint16_t *setup;

    int channelIndex = 0;
    int servoIndex = 0;
    int motorIndex = 0;

    // this is pretty hacky shit, but it will do for now. array of 4 config maps, [ multiPWM multiPPM airPWM airPPM ]
    if (init->airplane)
        i = 2; // switch to air hardware config
    if (init->usePPM)
        i++; // next index is for PPM

    setup = hardwareMaps[i];

    for (i = 0; i < USABLE_TIMER_CHANNEL_COUNT; i++) {
        uint8_t timerIndex = setup[i] & 0x00FF;
        uint8_t type = (setup[i] & 0xFF00) >> 8;

        if (setup[i] == 0xFFFF) // terminator
            break;

#ifdef OLIMEXINO_UNCUT_LED2_E_JUMPER
        // PWM2 is connected to LED2 on the board and cannot be connected unless you cut LED2_E
        if (timerIndex == PWM2)
            continue;
#endif

#ifdef STM32F10X_MD
        // skip UART2 ports
        if (init->useUART2 && (timerIndex == PWM3 || timerIndex == PWM4))
            continue;
#endif

#ifdef STM32F10X_MD
        // skip softSerial ports
        if (init->useSoftSerial && (timerIndex == PWM5 || timerIndex == PWM6 || timerIndex == PWM7 || timerIndex == PWM8))
            continue;
#endif

#ifdef CHEBUZZF3
        // skip softSerial ports
        // PWM4 can no-longer be used since it uses the same timer as PWM5 and PWM6
        if (init->useSoftSerial && (timerIndex == PWM4 || timerIndex == PWM5 || timerIndex == PWM6 || timerIndex == PWM7 || timerIndex == PWM8))
            continue;
#endif

#if defined(STM32F3DISCOVERY) && !defined(CHEBUZZF3)
        // skip softSerial ports
        if (init->useSoftSerial && (timerIndex == PWM9 || timerIndex == PWM10 || timerIndex == PWM11 || timerIndex == PWM12))
            continue;
#endif

#ifdef STM32F10X_MD
        // skip ADC for powerMeter if configured
        // See FIXME where init->adcChannel is initialised
        if (init->adcChannel && (init->adcChannel == timerIndex))
            continue;
#endif

        // hacks to allow current functionality
        if (type == TYPE_IW && !init->useParallelPWM)
            type = 0;

        if (type == TYPE_IP && !init->usePPM)
            type = 0;

        if (type == TYPE_IR && (!init->usePWMRSSI || init->useParallelPWM))
            type = 0;

        if (init->useServos && !init->airplane) {
#if defined(STM32F10X_MD) || defined(CHEBUZZF3)
            // remap PWM9+10 as servos
            if (timerIndex == PWM9 || timerIndex == PWM10)
                type = TYPE_S;
#endif

#if (defined(STM32F303xC) || defined(STM32F3DISCOVERY)) && !defined(CHEBUZZF3)
            // remap PWM 5+6 or 9+10 as servos - softserial pin pairs require timer ports that use the same timer
            if (init->useSoftSerial) {
                if (timerIndex == PWM5 || timerIndex == PWM6)
                    type = TYPE_S;
            } else {
                if (timerIndex == PWM9 || timerIndex == PWM10)
                    type = TYPE_S;
            }
#endif
        }

        if (init->extraServos && !init->airplane) {
            // remap PWM5..8 as servos when used in extended servo mode
            if (timerIndex >= PWM5 && timerIndex <= PWM8)
                type = TYPE_S;
        }

        if (type == TYPE_IP) {
            ppmInConfig(timerIndex);
        } else if (type == TYPE_IW) {
            pwmInConfig(timerIndex, channelIndex);
            channelIndex++;
        } else if (type == TYPE_IR) {
            pwmRSSIInConfig(timerIndex);
        } else if (type == TYPE_M) {
            if (init->motorPwmRate > 500) {
            	pwmBrushedMotorConfig(&timerHardware[timerIndex], motorIndex, init->motorPwmRate, init->idlePulse);
            } else {
            	pwmBrushlessMotorConfig(&timerHardware[timerIndex], motorIndex, init->motorPwmRate, init->idlePulse);
            }
            motorIndex++;
        } else if (type == TYPE_S) {
            pwmServoConfig(&timerHardware[timerIndex], servoIndex, init->servoPwmRate, init->servoCenterPulse);
            servoIndex++;
        }
    }
}
