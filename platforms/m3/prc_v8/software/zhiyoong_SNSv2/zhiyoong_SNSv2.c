//*******************************************************************
//Author: ZhiYoong Foo
//Description: SNSv2 Functionality Tests
//*******************************************************************
#include "mbus.h"
#include "PRCv8.h"

#define SNS_ADDR 0x4

#define NUM_SAMPLES 50         //Number of CDC samples to take

//Interrupt Handlers
void handler_ext_int_0(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_1(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_2(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_3(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_0(void){
  *((volatile uint32_t *) 0xE000E280) = 0x1;
}
void handler_ext_int_1(void){
  *((volatile uint32_t *) 0xE000E280) = 0x2;
}
void handler_ext_int_2(void){
  *((volatile uint32_t *) 0xE000E280) = 0x4;
}
void handler_ext_int_3(void){
  *((volatile uint32_t *) 0xE000E280) = 0x8;
}

int main() {
  
  uint32_t cdc_data[100];

  //Clear All Pending Interrupts
  *((volatile uint32_t *) 0xE000E280) = 0xF;
  //Enable Interrupts
  *((volatile uint32_t *) 0xE000E100) = 0xF;

  //Enumeration
  enumerate(SNS_ADDR);
  asm ("wfi;");
  delay(10000);

  
  //R7
  //(0x1<<3)|(0x3<<0);
  uint32_t _sns_r7 = (0x0<<3)|(0x4<<0);
  write_mbus_register(SNS_ADDR,7,_sns_r7);
  delay(10000);

  //R0;
  //(0x0<<22)|(0x1<<21)|(0x6<<18)|(0x6<<15)|(0x0<<14)|(0x0<<12)|(0x4<<9)|(0x1<<8)|(0x1<<7)|(0x0<<6)|(0x1<<5)|(0x0<<4)|(0x7<<1)|(0x0<<0);
  uint32_t _sns_r0 = (0x0<<22)|(0x1<<21)|(0x7<<18)|(0x7<<15)|(0x0<<14)|(0x0<<12)|(0x1<<9)|(0x0<<8)|(0x1<<7)|(0x0<<6)|(0x1<<5)|(0x0<<4)|(0x7<<1)|(0x0<<0);
  write_mbus_register(SNS_ADDR,0,_sns_r0);
  delay(10000);
  
  //R1;
  //(0x0<<18)|(0xF<<9)|(0x20<<0);
  uint32_t _sns_r1 = (0x0<<18)|(0xC<<9)|(0x1A<<0);
  write_mbus_register(SNS_ADDR,1,_sns_r1);
  delay(10000);
  
  //Disable ext_reset
  _sns_r0 &= ~(1<<21);
  write_mbus_register(SNS_ADDR,0,_sns_r0);
  asm( "wfi;" );
  delay (10000);
  
  //Disable & Enable cdc_clk
  uint32_t i = 0;
  for (i=0; i<NUM_SAMPLES; i++){
    _sns_r0 |= (1<<22);
    write_mbus_register(SNS_ADDR,0,_sns_r0);
    asm( "wfi;" );
    _sns_r0 &= ~(1<<22);
    write_mbus_register(SNS_ADDR,0,_sns_r0);
    cdc_data[i] = *((volatile uint32_t *) 0xA0001014);

    //    delay(5000);
    //    write_mbus_register(0x5,0,cdc_data[i]);
    //    delay(50000);
  }
  
  for (i=0; i<NUM_SAMPLES; i++){
    delay(3000);
    write_mbus_register(0x5,0,cdc_data[i]);
  }
  while (1){}
}
