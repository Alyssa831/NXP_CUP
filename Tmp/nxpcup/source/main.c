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

#define MAX_VECTORS 10

double g_camera_center_x = 45.0;
double g_ideal_lane_half_width = 31.0;

// ==========================================
// 1. ADD A GLOBAL MILLISECOND TIMER VARIABLE
// ==========================================
volatile uint32_t g_ms_timer = 0;

// ==========================================
// 2. ADD THE SYSTICK INTERRUPT HANDLER
// ==========================================
void SysTick_Handler(void) {
    g_ms_timer++;
}

// OUTER LOOP TUNING VARIABLES (To be tuned later)
double K_px = 0.5;   // Proportional gain for position
double K_ix = 0.01;  // Integral gain for position

void CalibrateTrack(pixy_t *cam) {
    // ... [Your existing CalibrateTrack code remains unchanged] ...
    uint16_t vectors[MAX_VECTORS * 4];
    size_t num_vectors;

    PRINTF("\r\n--- STARTING AUTO-CALIBRATION ---\r\n");
    PRINTF("Keep the car perfectly centered on the straight track...\r\n");

    SDK_DelayAtLeastUs(1000000, SystemCoreClock);

    int successful_reads = 0;
    double total_center = 0.0;
    double total_width = 0.0;

    while (successful_reads < 10) {
        if (pixy_get_vectors(cam, vectors, MAX_VECTORS, &num_vectors) == kStatus_Success) {
//            PRINTF("\r\nCalibration Frame %d - Detected %u Vectors:\r\n", successful_reads + 1, num_vectors);

            for (size_t i = 0; i < num_vectors; i++) {
                uint16_t x0 = vectors[4*i + 0];
                uint16_t y0 = vectors[4*i + 1];
                uint16_t x1 = vectors[4*i + 2];
                uint16_t y1 = vectors[4*i + 3];
//                PRINTF("  [%2u] (%3u,%3u)->(%3u,%3u)\r\n", (unsigned)i, x0, y0, x1, y1);
            }

            if (num_vectors >= 2) {
                double left_x = 999.0;
                double right_x = 0.0;

                for(size_t i = 0; i < num_vectors; i++) {
                    uint16_t x0 = vectors[4*i + 0];
                    uint16_t x1 = vectors[4*i + 2];
                    double mid_x = ((double)x0 + (double)x1) / 2.0;

                    if (mid_x < left_x)  left_x = mid_x;
                    if (mid_x > right_x) right_x = mid_x;
                }

                double center = (left_x + right_x) / 2.0;
                double half_width = (right_x - left_x) / 2.0;

//                PRINTF("  -> Calculated Center X: %d | Half-Width: %d\r\n", (int)center, (int)half_width);

                total_center += center;
                total_width += half_width;
                successful_reads++;

                SDK_DelayAtLeastUs(50000, SystemCoreClock);
            } else {
//                PRINTF("  -> Not enough lines for calibration (need >= 2). Retrying...\r\n");
                SDK_DelayAtLeastUs(50000, SystemCoreClock);
            }
        }
    }

    g_camera_center_x = total_center / 10.0;
    g_ideal_lane_half_width = total_width / 10.0;

//    PRINTF("\r\nCalibration Complete!\r\n");
//    PRINTF("Final Averaged Center X: %d\r\n", (int)g_camera_center_x);
//    PRINTF("Final Averaged Half-Width: %d\r\n", (int)g_ideal_lane_half_width);
//    PRINTF("---------------------------------\r\n\n");
}


int main(void)
{
    uint16_t vectors[MAX_VECTORS * 4];
    size_t   num_vectors;
//    PRINTF("Initializing Board\n");
    BOARD_InitHardware();
    BOARD_InitBootPins();
    BOARD_InitBootPeripherals();

//    PRINTF("Initializing HBRIDGE\n");
    HbridgeInit(&g_hbridge,
                CTIMER0_PERIPHERAL,
                CTIMER0_PWM_PERIOD_CH,
                CTIMER0_PWM_1_CHANNEL,
                CTIMER0_PWM_2_CHANNEL,
                GPIO0, 24U,
                GPIO0, 27U);

    pixy_t cam1;
//    PRINTF("Initializing PIXY\n");
    pixy_init(&cam1, LPI2C2, 0x54U, &LP_FLEXCOMM2_RX_Handle, &LP_FLEXCOMM2_TX_Handle);

//    PRINTF("Setting PIXY LED\n");
    pixy_set_led(&cam1, 255, 0, 0);

//    PRINTF("INITIALIZATION COMPLETE\n");

//    TestServo();
    //CalibrateTrack(&cam1);

    const double PERPENDICULAR_THRESHOLD = 4.7;

    // INNER LOOP STATE VARIABLES
    double previous_angle_error = 0.0;
    const double DERIVATIVE_ALPHA = 0.3;
    double previous_filtered_derivative = 0.0;

    // OUTER LOOP STATE VARIABLES
    double integral_x = 0.0;

    while (1)
    {
        if (pixy_get_vectors(&cam1, vectors, MAX_VECTORS, &num_vectors) == kStatus_Success) {

            double total_angle = 0.0;
            double total_position_error = 0.0;

            int valid_lines_count = 0;
            int valid_position_count = 0;

            for (size_t i = 0; i < num_vectors; i++) {
                uint16_t x0 = vectors[4*i + 0];
                uint16_t y0 = vectors[4*i + 1];
                uint16_t x1 = vectors[4*i + 2];
                uint16_t y1 = vectors[4*i + 3];

                if (y0 == y1) continue;

                // 1. Calculate and accumulate angle (theta)
                double m = ((double)x0-(double)x1) / ((double)y0-(double)y1);
                double theta_degrees = atan(m) * (180.0 / 3.1415926535);

                if (fabs(m) > PERPENDICULAR_THRESHOLD){
                	PRINTF(" Vec[%u]: (%3u,%3u)->(%3u,%3u) | m: %5.2f | Theta: %5.1f | IGNORED (Crossroad)\r\n", i, x0, y0, x1, y1, m, theta_degrees);
                	continue;
                }

                total_angle += m;
                valid_lines_count++;
                // Print the accepted vector, m, Theta, and running tally
                PRINTF(" Vec[%u]: (%3u,%3u)->(%3u,%3u) | m: %5.2f | Theta: %5.1f | ACCEPTED | Run Angle: %5.2f\r\n", i, x0, y0, x1, y1, m, theta_degrees,  total_angle);


                // 2. Calculate and accumulate cross-track error (x_c - x)
                double bottom_x, bottom_y;
                if (y0 > y1) {
                    bottom_x = x0; bottom_y = y0;
                } else {
                    bottom_x = x1; bottom_y = y1;
                }

                if (bottom_y > 35) {
                    double expected_x;
                    if (bottom_x < g_camera_center_x) {
                        expected_x = g_camera_center_x - g_ideal_lane_half_width;
                    } else {
                        expected_x = g_camera_center_x + g_ideal_lane_half_width;
                    }

                    // Calculate positional error: Target(xc) - Current(x)
                    double position_error = expected_x - bottom_x;
                    total_position_error += position_error;
                    valid_position_count++;
                }
            }

            double final_steering = 0.0;

            if (valid_lines_count > 0) {

                // Average the readings
                double current_angle = total_angle / (double)valid_lines_count;
                current_angle *= -1.0; // Maintain your original sign convention


                double avg_position_error = 0.0;
                if (valid_position_count > 0) {
                    avg_position_error = total_position_error / (double)valid_position_count;
                }

                // ========================================================
                // OUTER LOOP (PI): Translates Position Error -> Target Angle
                // ========================================================

                /* --- COMMENTED OUT FOR INNER LOOP TUNING ---

                integral_x += avg_position_error;

                // Calculate Theta_r (Reference Angle)
                double target_angle = (avg_position_error * K_px) + (integral_x * K_ix);

                // Optional: Clamp target angle so the car doesn't try to turn 90 degrees to fix an error
                if (target_angle > 1.5) target_angle = 1.5;
                if (target_angle < -1.5) target_angle = -1.5;

                --------------------------------------------- */

                // Hardcode Target Angle to 0 for inner-loop tuning
                double target_angle = 0 ;

                // ========================================================
                // INNER LOOP (PD): Translates Angle Error -> Steering Effort
                // ========================================================

                // Calculate error (Theta_r - Theta)
                double angle_error = -target_angle + current_angle;

                // 1. P-TERM (Angle)
                double p_term = (angle_error > 0) ? (angle_error * STEERING_P_RIGHT) : (angle_error * STEERING_P_LEFT);

                // 2. D-TERM (Damper based on error rate of change)
                double raw_derivative = angle_error - previous_angle_error;
                double filtered_derivative = (DERIVATIVE_ALPHA * raw_derivative) +
                                             ((1.0 - DERIVATIVE_ALPHA) * previous_filtered_derivative);
                previous_filtered_derivative = filtered_derivative;

                double d_term = filtered_derivative * STEERING_D;

                // Save state for next frame
                previous_angle_error = angle_error;


                // 3. THE TOTAL COMBINATION (F = P + D)
                final_steering = p_term + d_term;

//                PRINTF("Error: %.2f | P: %.2f | D: %.2f | TOTAL: %.2f\r\n",
//                        angle_error, p_term, d_term, final_steering);

            } else {
                // LOST LINE LOGIC
                final_steering = 0.0;
                previous_angle_error = 0.0;
                previous_filtered_derivative = 0.0;
                integral_x = 0.0; // Reset outer loop integral to prevent windup off-track
//                PRINTF(">>> LOST LINE: Straight\r\n");
            }

            // Apply Limiters
            if (final_steering > STEERING_LIMIT_RIGHT) final_steering = STEERING_LIMIT_RIGHT;
            if (final_steering < STEERING_LIMIT_LEFT) final_steering = STEERING_LIMIT_LEFT;

            // Actuate Servo
            Steer(final_steering + STEERING_OFFSET);
        }

        // ==========================================
		// 4. CHECK THE TIMER BEFORE POWERING MOTORS
		// ==========================================
		if (g_ms_timer < 5000) {
			// Keep driving
			HbridgeSpeed(&g_hbridge, SPEED_LEFT, SPEED_RIGHT);
		} else {
			// 5 Seconds have passed: Kill motors and center steering
			HbridgeSpeed(&g_hbridge, 0, 0);
			Steer(STEERING_OFFSET);
		}
    }
}

