#ifndef CONFIG_H
#define CONFIG_H

// --- CAMERA & VISION TUNING ---
#define CAMERA_CENTER_X             41.0f
#define IDEAL_LANE_HALF_WIDTH       39.0f
#define HORIZONTAL_ANGLE_THRESHOLD  78.0f // Ignore lines flatter than 78 degrees
#define START_LINE_X_TOLERANCE      15.0f
#define POS_ERROR_Y_THRESHOLD       40.0f
#define ANGLE_Y_THRESHOLD           40.0f
#define PERSPECTIVE_BEND_FACTOR     0.52f
#define slow_down_angle				25.0f

#define MAX_VECTORS 10

// --- TIMING TUNING ---
#define STARTUP_DELAY_MS            2000
#define SLOW_MODE_TIME_MS           2000

// --- PID TUNING ---
#define K_P                         1.0f   // Steers to the center
#define K_I                         0.05f  // Gently nudges back to center on curves
#define K_D                         1.0f   // Dampens steering using camera angle
#define INTEGRAL_LIMIT              200.0f

// --- STEERING LIMITS ---
#define STEERING_LIMIT_RIGHT        80.0f
#define STEERING_LIMIT_LEFT         -80.0f
#define STEERING_OFFSET             0.0f

// --- SPEED CONTROL ---
#define SPEED						80
#define SLOW_SPEED_RIGHT            -30
#define SLOW_SPEED_LEFT             30
#define turning_speed				60

#endif // CONFIG_H
