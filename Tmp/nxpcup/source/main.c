#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "fsl_pwm.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "hbridge.h"
#include "pixy.h"
#include "fsl_common.h"
#include "Config.h"
#include "servo.h"
#include "esc.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "calibration.h"

// --- OBSTACLE SENSOR DEFINES ---
#define TRIG_PIN  31U   // P0_31
#define ECHO_PIN  28U   // P0_28
#define OBSTACLE_THRESHOLD_CM 15.0f

volatile uint32_t g_ms_timer = 0;
static bool is_braking = false;
float current_drive_speed = 100.0f;

void SysTick_Handler(void) {
    g_ms_timer++;
}

// --- ULTRASONIC SENSOR FUNCTIONS ---
static void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
}

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000U) * us;
    while ((DWT->CYCCNT - start) < cycles) {}
}

static uint32_t micros_now(void) {
    return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

static void HC_SR04_InitPins(void) {
    SYSCON->AHBCLKCTRLSET[0] = (1u << 13);  // PORT0
    SYSCON->AHBCLKCTRLSET[0] = (1u << 19);  // GPIO0
    PORT0->PCR[TRIG_PIN] = 0x1000;
    PORT0->PCR[ECHO_PIN] = 0x1000;
    GPIO0->PDDR |=  (1u << TRIG_PIN);
    GPIO0->PDDR &= ~(1u << ECHO_PIN);
    GPIO0->PCOR = (1u << TRIG_PIN);
}

static bool HC_SR04_ReadDistanceCm(float *distance_cm) {
    uint32_t t0, t1, timeout_start;
    GPIO0->PCOR = (1u << TRIG_PIN);
    delay_us(2);
    GPIO0->PSOR = (1u << TRIG_PIN);
    delay_us(10);
    GPIO0->PCOR = (1u << TRIG_PIN);
    timeout_start = micros_now();
    while (((GPIO0->PDIR >> ECHO_PIN) & 1u) == 0u) {
        if ((micros_now() - timeout_start) > 30000U) return false;
    }
    t0 = micros_now();
    timeout_start = micros_now();
    while (((GPIO0->PDIR >> ECHO_PIN) & 1u) == 1u) {
        if ((micros_now() - timeout_start) > 30000U) return false;
    }
    t1 = micros_now();
    *distance_cm = (t1 - t0) / 58.0f;
    return true;
}

int main(void) {
    uint16_t vectors[MAX_VECTORS * 4];
    size_t   num_vectors;
    float distance_cm = 100.0f;

    BOARD_InitHardware();
    BOARD_InitBootPins();
    BOARD_InitBootPeripherals();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    DWT_Init();
    HC_SR04_InitPins();

    HbridgeInit(&g_hbridge, CTIMER0_PERIPHERAL, CTIMER0_PWM_PERIOD_CH,
                CTIMER0_PWM_1_CHANNEL, CTIMER0_PWM_2_CHANNEL,
                GPIO0, 24U, GPIO0, 27U);

    pixy_t cam1;
    pixy_init(&cam1, LPI2C2, 0x54U, &LP_FLEXCOMM2_RX_Handle, &LP_FLEXCOMM2_TX_Handle);
    pixy_set_led(&cam1, 255, 0, 0);

    float integral_x = 0.0f;
    float previous_pos_error = 0.0f;
    bool is_slow_mode = false;

    while (1) {
        if (g_ms_timer < STARTUP_DELAY_MS) {
            HbridgeSpeed(&g_hbridge, 0, 0);
            Steer(STEERING_OFFSET);
            continue;
        }

        // --- OBSTACLE DETECTION & //PRINT ---
        if (HC_SR04_ReadDistanceCm(&distance_cm)) {
            //PRINTF("Distance: %.2f cm | <10cm: %s\r\n",distance_cm, (distance_cm < OBSTACLE_THRESHOLD_CM) ? "YES" : "NO");
        }

        // --- CAMERA PROCESSING ---
        if (pixy_get_vectors(&cam1, vectors, MAX_VECTORS, &num_vectors) == kStatus_Success) {
            float total_angle = 0.0f;
            float total_position_error = 0.0f;
            int valid_lines_count = 0;
            int valid_position_count = 0;

            for (size_t i = 0; i < num_vectors; i++) {
                uint16_t x0 = vectors[4*i + 0], y0 = vectors[4*i + 1];
                uint16_t x1 = vectors[4*i + 2], y1 = vectors[4*i + 3];

                if (y1 > y0) {
                    uint16_t tempX = x0; uint16_t tempY = y0;
                    x0 = x1; y0 = y1; x1 = tempX; y1 = tempY;
                }
                if (y0 == y1) continue;

                float theta_degrees = atanf(((float)x0 - (float)x1) / ((float)y0 - (float)y1)) * (180.0f / 3.1415926535f);

                if (fabsf(theta_degrees) > HORIZONTAL_ANGLE_THRESHOLD) {
                    float line_mid_x = ((float)x0 + (float)x1) / 2.0f;
                    if (fabsf(line_mid_x - CAMERA_CENTER_X) < START_LINE_X_TOLERANCE) {
                        if (g_ms_timer > SLOW_MODE_TIME_MS) is_slow_mode = true;
                        //PRINTF("Vector[%d]: theta_degrees = %d, \r\n", (int)i, (int)theta_degrees);

                    }
                    continue;
                }

                if (y0 > POS_ERROR_Y_THRESHOLD) {
                    float expected_x = (x0 < CAMERA_CENTER_X) ? (CAMERA_CENTER_X - IDEAL_LANE_HALF_WIDTH) : (CAMERA_CENTER_X + IDEAL_LANE_HALF_WIDTH);
                    float individual_error = expected_x - (float)x0;

                    // RESTORED POSITION //PRINT
                    //PRINTF("Vector[%d]: position = %d, position error = %d\r\n", (int)i, (int)x0, (int)individual_error);

                    total_position_error += -individual_error;
                    valid_position_count++;
                }

                if (y0 > ANGLE_Y_THRESHOLD) {
                    float perspective_bend = ((float)x0 - CAMERA_CENTER_X) * PERSPECTIVE_BEND_FACTOR;
                    float true_angle = theta_degrees - perspective_bend;

                    // RESTORED ANGLE //PRINT
                    //PRINTF("Vector[%d]: Raw Angle = %d, Corrected Angle = %d\r\n", (int)i, (int)theta_degrees, (int)true_angle);

                    total_angle += -true_angle;
                    valid_lines_count++;
                }
            }

            // --- PID CONTROLLER ---
            float final_steering = 0.0f;
            if (valid_lines_count > 0 && valid_position_count > 0) {
                float avg_pos_err = total_position_error / (float)valid_position_count;
                float current_angle = total_angle / (float)valid_lines_count;

                if (!is_braking && fabsf(current_angle) > slow_down_angle) {
                    is_braking = true;
                    current_drive_speed = turning_speed;
                }
                else if (is_braking && fabsf(current_angle) < slow_down_angle - 3.0) {
                    is_braking = false;
                    current_drive_speed = SPEED;
                }

                integral_x += avg_pos_err;
                if (integral_x > INTEGRAL_LIMIT) integral_x = INTEGRAL_LIMIT;
                if (integral_x < -INTEGRAL_LIMIT) integral_x = -INTEGRAL_LIMIT;
                if ((previous_pos_error > 0.0f && avg_pos_err < 0.0f) || (previous_pos_error < 0.0f && avg_pos_err > 0.0f)) integral_x = 0.0f;

                previous_pos_error = avg_pos_err;

                float p_term = avg_pos_err * K_P;
                float i_term = integral_x * K_I;
                float d_term = current_angle * K_D;
                final_steering = p_term + i_term + d_term;

                // RESTORED PID SUMMARY //PRINT
                //PRINTF("PID | PosErr: %d | Angle: %d | P: %d, I: %d, D: %d | Final Steer: %d\r\n",(int)avg_pos_err, (int)current_angle, (int)p_term, (int)i_term, (int)d_term, (int)final_steering);
            }

            if (final_steering > STEERING_LIMIT_RIGHT) final_steering = STEERING_LIMIT_RIGHT;
            if (final_steering < STEERING_LIMIT_LEFT)  final_steering = STEERING_LIMIT_LEFT;

            Steer(final_steering + STEERING_OFFSET);
        }

        // --- SPEED CONTROL & OBSTACLE STOP ---
        if (distance_cm < OBSTACLE_THRESHOLD_CM) {
            HbridgeSpeed(&g_hbridge, 0, 0);
        } else if (is_slow_mode) {
            HbridgeSpeed(&g_hbridge, SLOW_SPEED_LEFT, SLOW_SPEED_RIGHT);
            Steer(STEERING_OFFSET);
        } else {
            HbridgeSpeed(&g_hbridge, current_drive_speed, -current_drive_speed);
        }
        //PRINTF("SLOW MODE: %s\r\n", is_slow_mode ? "YES" : "NO");
    }
}
