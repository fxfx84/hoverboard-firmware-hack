
#include "stm32f1xx_hal.h"
#include "defines.h"
#include "setup.h"
#include "config.h"


volatile int posl = 0;
volatile int posr = 0;
volatile int pwml = 0;
volatile int pwmr = 0;
volatile int weakl = 0;
volatile int weakr = 0; 
volatile int posSVl = 0; 
volatile int posSVr = 0; 
volatile int direction=1;

volatile uint8_t halll_old=0;
volatile uint8_t hallr_old=0;
volatile float posdegl = 0;
volatile float posdegr = 0;
volatile int deltatl=0;
volatile int deltatr=0;
volatile float deltadegl=0.1;
volatile float deltadegr=0.1;

extern volatile int speed;

extern volatile adc_buf_t adc_buffer;

extern volatile uint32_t timeout;

uint32_t buzzerFreq = 0;
uint32_t buzzerPattern = 0;

uint8_t enable = 0;

const int pwm_res = 64000000 / 2 / PWM_FREQ; // = 2000

const uint8_t hall_to_pos[8] = {
    0,
    0,
    2,
    1,
    4,
    5,
    3,
    0,
};
//is exatly doing what expected reusing the old one
//uint8_t halll = hall_ul * 1 + hall_vl * 2 + hall_wl * 4;
//          Sect2
//    010->2     011->3    
//     S3           Sect1
//110->6              001->1
//      S4              S6
//    100->4      101->5
//            S5
//const uint8_t hall_to_pos_SV[8] = { // tryng to use space vector convetnion counterclockwise 0 at fully right position when u is active.
//    0,
//    0,//dec=1 Sect 1
//    2,//dec=2 Sect 3
//    1,//dec=3 sect 2
//    4,//dec=4 Sect 5
//    5,//dec=5 sect 6
//    3,//dec=6 sect 4
//    0,//
//};


inline void blockPWM(int pwm, int pos, int *u, int *v, int *w) {
  switch(pos) {
    case 0:
      *u = 0;
      *v = pwm;
      *w = -pwm;
      break;
    case 1:
      *u = -pwm;
      *v = pwm;
      *w = 0;
      break;
    case 2:
      *u = -pwm;
      *v = 0;
      *w = pwm;
      break;
    case 3:
      *u = 0;
      *v = -pwm;
      *w = pwm;
      break;
    case 4:
      *u = pwm;
      *v = -pwm;
      *w = 0;
      break;
    case 5:
      *u = pwm;
      *v = 0;
      *w = -pwm;
      break;
    default:
      *u = 0;
      *v = 0;
      *w = 0;
  }
}

inline void blockPhaseCurrent(int pos, int u, int v, int *q) {
  switch(pos) {
    case 0:
      *q = u - v;
      // *u = 0;
      // *v = pwm;
      // *w = -pwm;
      break;
    case 1:
      *q = u;
      // *u = -pwm;
      // *v = pwm;
      // *w = 0;
      break;
    case 2:
      *q = u;
      // *u = -pwm;
      // *v = 0;
      // *w = pwm;
      break;
    case 3:
      *q = v;
      // *u = 0;
      // *v = -pwm;
      // *w = pwm;
      break;
    case 4:
      *q = v;
      // *u = pwm;
      // *v = -pwm;
      // *w = 0;
      break;
    case 5:
      *q = -(u - v);
      // *u = pwm;
      // *v = 0;
      // *w = -pwm;
      break;
    default:
      *q = 0;
      // *u = 0;
      // *v = 0;
      // *w = 0;
  }
}

inline void calcDeg(int pos, *deg) {
  switch(pos) {
    case 0: // Sector 1 u=1 v = 0 w = 0
          *deg = 0;
      // *u = 0;
      // *v = pwm;
      // *w = -pwm;
      break;
    case 1: // Sector 2 u=1 v = 1 w = 0
          *deg=60;
     // *q = u;
      // *u = -pwm;
      // *v = pwm;
      // *w = 0;
      break;
    case 2: // Sector 3 u=0 v = 1 w = 0
      *deg = 120;
      // *u = -pwm;
      // *v = 0;
      // *w = pwm;
      break;
    case 3: // Sector 4 u=0 v = 1 w = 1
      *deg = 180;
      // *u = 0;
      // *v = -pwm;
      // *w = pwm;
      break;
    case 4: // Sector 5 u=0 v = 0 w = 1
      *deg = 240;
      // *u = pwm;
      // *v = -pwm;
      // *w = 0;
      break;
    case 5: // Sector 5 u=1 v = 0 w = 1
       *deg = 300;
      // *u = pwm;
      // *v = 0;
      // *w = -pwm;
      break;
    default:
      *deg = 0;
      // *u = 0;
      // *v = 0;
      // *w = 0;
  }
}

uint32_t buzzerTimer        = 0;

int offsetcount = 0;
int offsetrl1   = 2000;
int offsetrl2   = 2000;
int offsetrr1   = 2000;
int offsetrr2   = 2000;
int offsetdcl   = 2000;
int offsetdcr   = 2000;

float batteryVoltage = BAT_NUMBER_OF_CELLS * 4.0;

int curl = 0;
// int errorl = 0;
// int kp = 5;
// volatile int cmdl = 0;

int last_pos = 0;
int timer = 0;
const int max_time = PWM_FREQ / 10;
volatile int vel = 0;

//scan 8 channels with 2ADCs @ 20 clk cycles per sample
//meaning ~80 ADC clock cycles @ 8MHz until new DMA interrupt =~ 100KHz
//=640 cpu cycles
void DMA1_Channel1_IRQHandler() {
  DMA1->IFCR = DMA_IFCR_CTCIF1;
  // HAL_GPIO_WritePin(LED_PORT, LED_PIN, 1);

  if(offsetcount < 1000) {  // calibrate ADC offsets
    offsetcount++;
    offsetrl1 = (adc_buffer.rl1 + offsetrl1) / 2;
    offsetrl2 = (adc_buffer.rl2 + offsetrl2) / 2;
    offsetrr1 = (adc_buffer.rr1 + offsetrr1) / 2;
    offsetrr2 = (adc_buffer.rr2 + offsetrr2) / 2;
    offsetdcl = (adc_buffer.dcl + offsetdcl) / 2;
    offsetdcr = (adc_buffer.dcr + offsetdcr) / 2;
    return;
  }

  if (buzzerTimer % 1000 == 0) {  // because you get float rounding errors if it would run every time
    batteryVoltage = batteryVoltage * 0.99 + ((float)adc_buffer.batt1 * ((float)BAT_CALIB_REAL_VOLTAGE / (float)BAT_CALIB_ADC)) * 0.01;
  }

  //disable PWM when current limit is reached (current chopping)
  if(ABS((adc_buffer.dcl - offsetdcl) * MOTOR_AMP_CONV_DC_AMP) > DC_CUR_LIMIT || timeout > TIMEOUT || enable == 0) {
    LEFT_TIM->BDTR &= ~TIM_BDTR_MOE;
    //HAL_GPIO_WritePin(LED_PORT, LED_PIN, 1);
  } else {
    LEFT_TIM->BDTR |= TIM_BDTR_MOE;
    //HAL_GPIO_WritePin(LED_PORT, LED_PIN, 0);
  }

  if(ABS((adc_buffer.dcr - offsetdcr) * MOTOR_AMP_CONV_DC_AMP)  > DC_CUR_LIMIT || timeout > TIMEOUT || enable == 0) {
    RIGHT_TIM->BDTR &= ~TIM_BDTR_MOE;
  } else {
    RIGHT_TIM->BDTR |= TIM_BDTR_MOE;
  }

  int ul, vl, wl;
  int ur, vr, wr;

  //determine next position based on hall sensors
  uint8_t hall_ul = !(LEFT_HALL_U_PORT->IDR & LEFT_HALL_U_PIN);
  uint8_t hall_vl = !(LEFT_HALL_V_PORT->IDR & LEFT_HALL_V_PIN);
  uint8_t hall_wl = !(LEFT_HALL_W_PORT->IDR & LEFT_HALL_W_PIN);

  uint8_t hall_ur = !(RIGHT_HALL_U_PORT->IDR & RIGHT_HALL_U_PIN);
  uint8_t hall_vr = !(RIGHT_HALL_V_PORT->IDR & RIGHT_HALL_V_PIN);
  uint8_t hall_wr = !(RIGHT_HALL_W_PORT->IDR & RIGHT_HALL_W_PIN);

  uint8_t halll = hall_ul * 1 + hall_vl * 2 + hall_wl * 4;
    // a 1000 rpm deve fare 6000 deg/s se la funzione la chiamo ogni 1k al sec    
    if (halll_old!= halll) {
        posSVl_old=posSVl;
        
        posSVl= hall_to_pos[halll];
        if  (posSVl >= posSVl_old ){
                if (posSVl_old ==0 && posSVl>=180){
                direction=-1;
                }
                else {
                    direction=1;
                }
          else{
            direction=-1;
          }
        calcDeg(posSVl, &posdegl);
        deltadegl=6/deltatl;
        deltatl=0;
        halll_old=halll;
    }
    else {
         if (buzzerTimer % 100 == 0) { // chiamo l'update 1000 volte al sec
             if (direction=1){
            posdegl+=deltadegl;}
             else{
              posdegl-=deltadegl;
             }
             posdegl=CLAMP(posdegl,0,360)
            deltatl+=1; // viene in millisec
            deltatl=CLAMP(deltatl,0,2147483646);
         }
    }
       
            
  posl          = hall_to_pos[halll];
  posl += 2;
  posl %= 6;

  uint8_t hallr = hall_ur * 1 + hall_vr * 2 + hall_wr * 4;
  posr          = hall_to_pos[hallr];
  posr += 2;
  posr %= 6;

  blockPhaseCurrent(posl, adc_buffer.rl1 - offsetrl1, adc_buffer.rl2 - offsetrl2, &curl);

  //setScopeChannel(2, (adc_buffer.rl1 - offsetrl1) / 8);
  //setScopeChannel(3, (adc_buffer.rl2 - offsetrl2) / 8);


  // uint8_t buzz(uint16_t *notes, uint32_t len){
    // static uint32_t counter = 0;
    // static uint32_t timer = 0;
    // if(len == 0){
        // return(0);
    // }
    
    // struct {
        // uint16_t freq : 4;
        // uint16_t volume : 4;
        // uint16_t time : 8;
    // } note = notes[counter];
    
    // if(timer / 500 == note.time){
        // timer = 0;
        // counter++;
    // }
    
    // if(counter == len){
        // counter = 0;
    // }

    // timer++;
    // return(note.freq);
  // }


  //create square wave for buzzer
  buzzerTimer++;
  if (buzzerFreq != 0 && (buzzerTimer / 5000) % (buzzerPattern + 1) == 0) {
    if (buzzerTimer % buzzerFreq == 0) {
      HAL_GPIO_TogglePin(BUZZER_PORT, BUZZER_PIN);
    }
  } else {
      HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, 0);
  }

  //update PWM channels based on position
  blockPWM(pwml, posl, &ul, &vl, &wl);
  blockPWM(pwmr, posr, &ur, &vr, &wr);

  int weakul, weakvl, weakwl;
  if (pwml > 0) {
    blockPWM(weakl, (posl+5) % 6, &weakul, &weakvl, &weakwl);
  } else {
    blockPWM(-weakl, (posl+1) % 6, &weakul, &weakvl, &weakwl);
  }
  ul += weakul;
  vl += weakvl;
  wl += weakwl;

  int weakur, weakvr, weakwr;
  if (pwmr > 0) {
    blockPWM(weakr, (posr+5) % 6, &weakur, &weakvr, &weakwr);
  } else {
    blockPWM(-weakr, (posr+1) % 6, &weakur, &weakvr, &weakwr);
  }
  ur += weakur;
  vr += weakvr;
  wr += weakwr;

  LEFT_TIM->LEFT_TIM_U = CLAMP(ul + pwm_res / 2, 10, pwm_res-10);
  LEFT_TIM->LEFT_TIM_V = CLAMP(vl + pwm_res / 2, 10, pwm_res-10);
  LEFT_TIM->LEFT_TIM_W = CLAMP(wl + pwm_res / 2, 10, pwm_res-10);

  RIGHT_TIM->RIGHT_TIM_U = CLAMP(ur + pwm_res / 2, 10, pwm_res-10);
  RIGHT_TIM->RIGHT_TIM_V = CLAMP(vr + pwm_res / 2, 10, pwm_res-10);
  RIGHT_TIM->RIGHT_TIM_W = CLAMP(wr + pwm_res / 2, 10, pwm_res-10);
}
