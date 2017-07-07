#include <stdio.h>
#include <stdlib.h>
#include "car_lib.h"

unsigned char status;
short speed;
unsigned char gain;
int position, position_now;
short angle;
int channel;
int data;
char sensor;
int i, j;
int tol;
char byte = 0x80;


void main(void){
  CarControlInit();
  SpeedControlOnOff_Write(CONTROL);
  DesireSpeed_Write(150);
  PositionControlOnOff_Write(CONTROL);
  EncoderCounter_Write(0);
  DesireEncoderCount_Write(300);
  tol = 10;
  while(abs(position_now-position)>tol){
    position_now=EncoderCounter_Read();
  }
  PositionControlOnOff_Write(UNCONTROL);
  while(1) {
    channel = 1;
    data = DistanceSensor(channel);
    if(data > 1500){
      DesireSpeed_Write(0);
      while(data > 1500) data = DistanceSensor(channel);
      DesireSpeed_Write(150);
    }
  }
}
