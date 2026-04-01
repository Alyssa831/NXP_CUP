#include "fsl_ctimer.h"
#include "peripherals.h"
#include "fsl_debug_console.h"

void Steer(double angle)
{
	PRINTF("Function Steer is running\n");
    if (angle > 100.0)  angle = 100.0;
    if (angle < -100.0) angle = -100.0;

    PRINTF("Converting angle to duty cycles\n");
    PRINTF("Angle = %d\n", (int)angle);
    double duty = 5.0 + ((angle + 100.0) / 200.0) * 5.0;
    PRINTF("Conversion complete, duty = %u\n", (int)duty);

    uint32_t periodTicks = CTIMER2_PERIPHERAL->MR[CTIMER2_PWM_PERIOD_CH];
    uint32_t pulseTicks = (uint32_t)((periodTicks * (100.0 - duty)) / 100.0);

    PRINTF("Setting Ctimer\n");
    CTIMER2_PERIPHERAL->MR[2] = pulseTicks;

    PRINTF("Function ends here\n\n");
}

void TestServo(){
	volatile int Delay;
	volatile int SteerStrength;
	while(1){
		for(SteerStrength = -40; SteerStrength <=40; SteerStrength++){
			Delay = 2000;
			while(Delay){
				Delay--;
			}
			PRINTF("Steer: %d\n", SteerStrength);
			Steer(SteerStrength);
			if (SteerStrength==0) return;
		}

	}
}
