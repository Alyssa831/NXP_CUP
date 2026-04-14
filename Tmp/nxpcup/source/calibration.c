#include "calibration.h"
#include "fsl_debug_console.h" // For PRINTF
#include "fsl_common.h"        // For SDK_DelayAtLeastUs and SystemCoreClock

void CalibrateTrack(pixy_t *cam) {
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

                total_center += center;
                total_width += half_width;
                successful_reads++;

                SDK_DelayAtLeastUs(50000, SystemCoreClock);
            } else {
                SDK_DelayAtLeastUs(50000, SystemCoreClock);
            }
        }
    }

    // These update the global variables directly
    g_camera_center_x = total_center / 10.0;
    g_ideal_lane_half_width = total_width / 10.0;

    PRINTF("\r\nCalibration Complete!\r\n");
}
