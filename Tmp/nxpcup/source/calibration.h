#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "pixy.h"

// Define MAX_VECTORS if it isn't already defined in Config.h
#ifndef MAX_VECTORS
#define MAX_VECTORS 10
#endif

// The 'extern' keyword tells the compiler:
// "These variables exist somewhere else, just trust me and let me use them."
extern double g_camera_center_x;
extern double g_ideal_lane_half_width;

// Function prototype
void CalibrateTrack(pixy_t *cam);

#endif /* CALIBRATION_H */
