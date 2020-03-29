#if 0
#include "SmartElec.h"

// regulator works as follows
// interrupt is set for 1 milli-second
// zero cross-detect interrupt is set, which at 50 Hz AC line doubled up, will trigger every 10 milli-seconds.
// the regulator level is between 1 - 10. At 10, it is full speed without regulation
// any level below, the dim variable is set to that value
// zero cross-detect will reset the counter and TRIAC output state
// every milli-second, when the counter is incremented and becomes equal to dim level, the TRIAC output is turned on
// this way, the regulation is achieved

// Timer1 operates at 5MHz which is 5 ticks per us.
// So, if the timer is set at 5000, it will trigger every msec
#define REGULATOR_TIMER_INTERRUPT        500
// At a scale of 1, timer is 1msec. This scale will decide how fast the timer should run
#define REGULATOR_SCALE_FACTOR           10

volatile int cntr = 0;                   // Variable to use as a counter
volatile boolean zero_cross = false;  // Boolean to store a "switch" to tell us if we have crossed zero
volatile int dim = 0;
volatile boolean regulator_running = false;

// to handle the bug in interrupt generation
volatile boolean ignore_int = false;
volatile int ignore_int_cntr = 0;

// zero cross detect interrupt routine
static void ICACHE_RAM_ATTR zero_cross_detect() 
{
  if (ignore_int == false)
  {
    digitalWrite(SMARTELEC_UNIT_LEVEL_OUTPUT_PIN, LOW);
    ignore_int = true;
    ignore_int_cntr = 0;
    zero_cross = true;
    cntr = 0;
  }
}                                 

// Turn on the TRIAC at the appropriate time
static void ICACHE_RAM_ATTR check_regulator() 
{
  ignore_int_cntr++;
  if (ignore_int_cntr >= (5*REGULATOR_SCALE_FACTOR))
    ignore_int = false;
  if(zero_cross == true)
  {
    if(cntr>=dim)
    {
      digitalWrite(SMARTELEC_UNIT_LEVEL_OUTPUT_PIN, HIGH);
      cntr = 0; // reset counter
      zero_cross = false; // reset zero cross detection
    }
    cntr++; // increment time step counter
  }
}

void ICACHE_RAM_ATTR onTimerISR()
{
  digitalWrite(LED_BUILTIN,!(digitalRead(LED_BUILTIN)));  //Toggle LED Pin
  //digitalWrite(SMARTELEC_UNIT_LEVEL_OUTPUT_PIN,!(digitalRead(SMARTELEC_UNIT_LEVEL_OUTPUT_PIN)));  //Toggle LED Pin
}

void start_regulator(int level_val)
{
  // set the regulator level internally irrespective of whether regulator is running or not
  //dim = level_val*REGULATOR_SCALE_FACTOR;
  dim = map (level_val, 0, 10, 10*REGULATOR_SCALE_FACTOR, 0);
  
  // start only if not running
  if (regulator_running == false)
  {
    cntr = 0;
    // Attach an Interupt for Zero Cross Detection
    attachInterrupt(SMARTELEC_UNIT_LEVEL_INTERRUPT_PIN, zero_cross_detect, RISING);
    // Initialize and Enable the timer interrupt
    timer1_isr_init();
    timer1_attachInterrupt(check_regulator);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(REGULATOR_TIMER_INTERRUPT);
  }
  regulator_running = true;
}

void stop_regulator()
{
  if (regulator_running == true)
  {
    // stop the interrupt
    timer1_disable();
    // detach the interrupt
    detachInterrupt(SMARTELEC_UNIT_LEVEL_INTERRUPT_PIN);
  }
  regulator_running = false;
}
#endif
