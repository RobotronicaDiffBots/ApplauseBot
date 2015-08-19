#include "util.h"
#include <Servo.h>

#define SERVOTOP 165
///////////////////////////////////////////////////////////////////////
//Global constants
int light_pins[3] = {3, 5, 6};
int servo_pins[2] = {9, 10};
int botAngle[2] = {165, 5};

radio_message_t radioMessage;
radio_message_t latestValid;
 enum { MSG_SYNC_0 = 0, MSG_SYNC_1, MSG_QBOT_ID, MSG_TYPE, MSG_D1, MSG_D2,
   MSG_D3, MSG_D4, MSG_SEQNO, MSG_CHECKSUM };

uint8_t prevbuttons = 0;
//Messages:
//120: fadecolour: colour, 0, bmod, fade timer (in 1/40ths of a second)
//121: pulsecolour: colour, 0, bmod, period (in 1/40ths of a second)
//                  if colour == 0, uses default colour
//122: turncolour: (1 or 0), 0, bmod, fade timer

//123: moveservos: s1level (0 to 180), s2level (0 to 180), 0, 0
//124: clap: colourful (0 is not, else is), 0, 0, 0

//AN: bmod = brightness modifier, 0 to 100

//remote buttons
#define L6 1<<0
#define L5 1<<1
#define L4 1<<2
#define LT 1<<3
#define R3 1<<4
#define R2 1<<5
#define R1 1<<6
#define RT 1<<7

/////////////////////////////////////////////////////////////////////
//Variables. Please igmore the terrible globalness
uint32_t dttimer = 0; 

uint8_t ledmode;
uint8_t servoMode;

uint8_t clapTimer = 0;
uint8_t clapWidth = 0;

uint8_t servoDemands[2] = {0, 0};

uint8_t colourful = 0;

uint8_t prevColour = 0;
uint8_t nextColour = 0;

rgb24_t currentColour = {0,0,0};
float sr;
float sg;
float sb;
float period;
uint8_t fadeTimer;
uint8_t counter;
uint32_t cycle;
rgb24_t startColour;  
float bmod = 1;
float startbmod;
float stepbmod;

Servo servos[2];

/////////////////////////////////////////////////////////////////////
//Helper functions

//Sets the current light value.
void setColour(rgb24_t rgbcolour, float brightness) {
  uint8_t *c = (uint8_t *) &rgbcolour;
  float bm = sqrt(brightness);
  for (int i = 0; i < 3; i++) {
    uint8_t colour = c[i] * bm;
    analogWrite(light_pins[i], colour);
  }
  
}
//Sets the servo levels
void setServos() {
  servos[0].write(botAngle[0] - servoDemands[0]);
  servos[1].write(botAngle[1] + servoDemands[1]);
}
 
int checkPacket() {

  while (Serial.available() > 0) {
    int crc = 0;
    uint8_t *p = (uint8_t *) &radioMessage;
    radioMessage.nextByte = (uint8_t)(Serial.read());

    //Update state
    switch (radioMessage.mode) {
    case MSG_SYNC_0:
      if (radioMessage.nextByte == 0xAA) {
        radioMessage.mode = MSG_SYNC_1;
      }
      break;

    case MSG_SYNC_1:
      if (radioMessage.nextByte == 0x55) {
        radioMessage.mode = MSG_QBOT_ID;
      }
      else {
        radioMessage.mode = MSG_SYNC_0;
      }
      break;

    case MSG_QBOT_ID:
      radioMessage.robotID = radioMessage.nextByte;
      radioMessage.mode = MSG_TYPE;
      break;

    case MSG_TYPE:
      radioMessage.type = radioMessage.nextByte;
      radioMessage.mode = MSG_D1;
      break;

    case MSG_D1:
      radioMessage.d1 = radioMessage.nextByte;
      radioMessage.mode = MSG_D2;
      break;

    case MSG_D2:
      radioMessage.d2 = radioMessage.nextByte;
      radioMessage.mode = MSG_D3;
      break;

    case MSG_D3:
      radioMessage.d3 = radioMessage.nextByte;
      radioMessage.mode = MSG_D4;
      break;

    case MSG_D4:
      radioMessage.d4 = radioMessage.nextByte;
      radioMessage.mode = MSG_SEQNO;
      break;

    case MSG_SEQNO:
      if (radioMessage.nextByte != radioMessage.seqno) {
        radioMessage.seqno = radioMessage.nextByte;
        radioMessage.mode = MSG_CHECKSUM;
      }
      else {
        radioMessage.mode = MSG_SYNC_0;
      }
      break;

    case MSG_CHECKSUM:
      radioMessage.crc = radioMessage.nextByte;
      /* Calculate the CRC, inc the seqnum */
      for (int k = 0; k < 9; k++) {
        crc ^= p[k];
      }

      //if it's a valid message, reset the "time since last packet" timer
      radioMessage.mode = MSG_SYNC_0;
      if (radioMessage.crc == crc) {
        return 1;
      }
      else {
        return 0;
      }
    default:
      break;
    }
    
  }
  return 0;
}

//Sets the robot to fade to a colour, in ft/40 seconds, with brightness mod bm
void setFade(rgb24_t colour, uint8_t ft, float bm) {
    if (ft == 0) return;
    ledmode = 120;
    fadeTimer = ft;
    counter = 0;
    startColour = currentColour;
    rgb24_t finishColour = colour;
    sr = (finishColour.r - currentColour.r) / (float) fadeTimer;
    sg = (finishColour.g - currentColour.g) / (float) fadeTimer;
    sb = (finishColour.b - currentColour.b) / (float) fadeTimer;
    startbmod = bmod;
    stepbmod = (bm - bmod) / (float) fadeTimer;
}

//Sets a robot to oscillate a colour, with period p/40 seconds, with brightness mod bm
void setOsc(rgb24_t colour, uint8_t p, float bm) {
    if (p == 0) return;
    ledmode = 121;
    cycle = 0;    
    period = (2 * PI) / p;
    currentColour = colour;

    stepbmod = bm * 0.5;
}

//Process a valid radio message
void process() {
  //Check the button states vs previous
  if (radioMessage.type <= 5) {
    uint8_t buttons = radioMessage.d4;
    //light button has been pressed
    //L6
    if (((buttons & L6)) && !((prevbuttons & L6))) {
      setFade(colours[robots[radioMessage.robotID]], 40, (buttons & LT ? .5 : 1));
    }
    //L5
    if ((buttons & L5) && !(prevbuttons & L5)) {
      setOsc(colours[robots[radioMessage.robotID]], 80, (buttons & LT ? .5 : 1));
    }
    //L4
    if ((buttons & L4) && !(prevbuttons & L4)) {
      setFade(colours[0], 40, (buttons & LT ? .5 : 1));
    }
  }
  else {
    if (radioMessage.type == 120) {
      setFade(colours[radioMessage.d1], radioMessage.d4, radioMessage.d3 * 0.01);
    }
    if (radioMessage.type == 121) {
      setOsc(radioMessage.d1 == 0 ? colours[robots[radioMessage.robotID]] : colours[radioMessage.d1],
      radioMessage.d4, radioMessage.d3 * 0.01);
    }
    if (radioMessage.type == 122) {
      setFade(radioMessage.d1 == 1 ? colours[robots[radioMessage.robotID]] : colours[0],
      radioMessage.d4, radioMessage.d3 * 0.01);
    }
  }
  if (radioMessage.type == 123) {
    if (radioMessage.d1 > 180 || radioMessage.d2 > 180) return;
    servoMode = 0;
    servoDemands[0] = radioMessage.d1;
    servoDemands[1] = radioMessage.d2;
    setServos();
  }
    if (radioMessage.type == 124) {
    servoMode = 124;
    clapTimer = 10;
    clapWidth = 3 + random(4);
    servoDemands[0] = SERVOTOP - clapWidth*7;
    servoDemands[1] = SERVOTOP - clapWidth*7;
    setServos();
    colourful = radioMessage.d1;
    if (colourful != 255) {
      prevColour = robots[radioMessage.robotID];
    }
    else {
      prevColour = 0;
      nextColour = 0;
    }
  }
  


}
  
    
//is called once every 40th of a second
void updateState() {
  //If we're fading
  if (ledmode == 120 || ledmode == 122) {
    counter++;
    currentColour.r = startColour.r + counter * sr;
    currentColour.g = startColour.g + counter * sg;
    currentColour.b = startColour.b + counter * sb;
    bmod = startbmod + counter * stepbmod;
    if (counter == fadeTimer) {
      ledmode = 255;
    }
  }

  //if we're oscillating
  if (ledmode == 121) {     
    float cosval = -cos(period*cycle) + 1;
    bmod = cosval * stepbmod;
    cycle++;
  }

  //if we're clapping
  if (servoMode > 0) {
    if (clapTimer > 0) {
      clapTimer--;
    }
    else {
      clapTimer = clapWidth;
      if (servoMode == 124 || servoMode == 2) { //if the arms are low
        servoMode = 1;
        servoDemands[0] = SERVOTOP;
        servoDemands[1] = SERVOTOP;
      }
      else {                                    //if the arms are high
        servoMode = 2;
        servoDemands[0] = SERVOTOP - clapWidth*6;
        servoDemands[1] = SERVOTOP - clapWidth*6;
        
        if (colourful == 255) {
          do {
            nextColour = random(10) + 2;
          } while (nextColour == prevColour);
          currentColour = colours[nextColour];
          setFade(colours[0], 20, 1);
          prevColour = nextColour;
        }
        else if (colourful) {
          currentColour = colours[prevColour];
          setFade(colours[0], 20, 1);
        }
      }
      setServos();
    }
  }
  
  //Finally, set the colour
  setColour(currentColour, bmod);
}
 
void setup() {
  for (int i = 0; i < 3; i++) {
    pinMode(light_pins[i], OUTPUT);
  }
  for (int i = 0; i < 2; i++) {
    servos[i].attach(i+9);
  }

  //Robot default light colours
  //Diffbots
  robots[1] = 11;  //lime
  robots[2] = 2;  //red
  robots[3] = 3;  //green
  robots[4] = 4;  //blue
  robots[5] = 5;  //yellow
  robots[6] = 6;  //magenta
  robots[7] = 7;  //cyan
  robots[8] = 8;  //orange
  robots[9] = 9;  //purple
  robots[10] = 10;  //turquoise

  //Spare diffbots
  robots[11] = 1;  //white
  robots[12] = 1;  //white

  //Amoebots
  robots[13] = 3;  //Green
  robots[14] = 3; 
  robots[15] = 3; 
  robots[16] = 3; 
  robots[17] = 3; 
  robots[18] = 3; 
  robots[19] = 3; 
  robots[20] = 3; 
  robots[21] = 3; 
  robots[22] = 3; 

  //Appendagebots
  robots[23] = 11;  //lime
  robots[24] = 2;  //red
  robots[25] = 3;  //green
  robots[26] = 4;  //blue
  robots[27] = 5;  //yellow
  robots[28] = 6;  //magenta
  robots[29] = 7;  //cyan
  robots[30] = 8;  //orange
  robots[31] = 9;  //purple
  robots[32] = 10;  //turquoise

  //Applausebots
  robots[33] = 11;  //lime
  robots[34] = 2;  //red
  robots[35] = 3;  //green
  robots[36] = 4;  //blue
  robots[37] = 5;  //yellow
  robots[38] = 6;  //magenta
  robots[39] = 7;  //cyan
  robots[40] = 8;  //orange
  robots[41] = 9;  //purple
  robots[42] = 10;  //turquoise
  
  Serial.begin(9600);
  radioMessage.hdr0 = 0xAA;
  radioMessage.hdr1 = 0x55;
  radioMessage.seqno = 255;
  
  currentColour = colours[0];
  updateState();
  setServos();
  
}

void loop() {
  if (checkPacket()) {
    process(); 
  }
   if (millis() - dttimer > 25) {
    dttimer = millis();
    
    updateState();
  }

}

