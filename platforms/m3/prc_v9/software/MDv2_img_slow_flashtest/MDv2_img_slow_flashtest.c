//*******************************************************************
//Author: 		Gyouho Kim, ZhiYoong Foo
//Description: 	Code for Image capturing with MDv2
// 12/17/2014:	Derived from MDv1_motion.c
//*******************************************************************
#include "mbus.h"
#include "PRCv9.h"
#include "SNSv3.h"
#include "HRVv1.h"
#include "RADv5.h"

// Enumeration Sequence: PRC -> MD -> RAD
#define MD_ADDR 0x4		// MD Short Address
#define FLS_ADDR 0x2	// RAD Short Address
//#define SNS_ADDR 0x3  // SNSv1 Short Address

#define MBUS_DELAY 500
#define WAKEUP_DELAY 20000 // 20s
#define WAKEUP_DELAY_FINAL 10000	// Delay for waiting for internal decaps to stabilize after waking up MDSENSOR
#define DELAY_1 10000 // 1s
#define DELAY_IMG 40000 // 1s
#define INT_TIME 5
#define MD_INT_TIME 35
#define MD_TH 10
#define MD_MASK 0x3FF
#define MD_LOWRES 1
#define MD_TOPAD_SEL 0 // 1: thresholding, 0: no thresholding

#define VDD_CC_1P2 1
#define VNW_1P2 1

#define SEL_CC 3
#define SEL_CC_B 4

// FILL THIS OUT
#define SEL_VREF 0
#define SEL_VREFP 7
#define SEL_VBN 3
#define SEL_VBP 3
#define SEL_VB_RAMP 15
#define SEL_RAMP 1

#define SEL_ADC 1
#define SEL_ADC_B 6
#define SEL_PULSE 1
#define SEL_PULSE_COL 0
#define PULSE_SKIP 0
#define PULSE_SKIP_COL 0
#define TAVG 0

#define SEL_CLK_RING 2
#define SEL_CLK_DIV 4
#define SEL_CLK_RING_4US 0
#define SEL_CLK_DIV_4US 1
#define SEL_CLK_RING_ADC 2 
#define SEL_CLK_DIV_ADC 1
#define SEL_CLK_RING_LC 0
#define SEL_CLK_DIV_LC 1

#define START_ROW_IDX 40
#define END_ROW_IDX 120 // 160

#define ROW_SKIP 0
#define COL_SKIP 0
#define IMG_8BIT 1
#define ROW_IDX_EN 0
#define MD_RETURN_ADDR 0x24

#define START_COL_IDX 0 // in words
#define COLS_TO_READ 39 // in # of words: 39 for full frame, 19 for half

//***************************************************
// Global variables
//***************************************************

uint32_t enumerated;
uint32_t MBus_msg_flag;


//************************************
//Interrupt Handlers
//Must clear pending bit!
//************************************
void handler_ext_int_0(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_1(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_2(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_3(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_0(void){
    *((volatile uint32_t *) 0xE000E280) = 0x1;
    MBus_msg_flag = 0x10;
}
void handler_ext_int_1(void){
    *((volatile uint32_t *) 0xE000E280) = 0x2;
    MBus_msg_flag = 0x11;
}
void handler_ext_int_2(void){
    *((volatile uint32_t *) 0xE000E280) = 0x4;
    MBus_msg_flag = 0x12;
}
void handler_ext_int_3(void){
    *((volatile uint32_t *) 0xE000E280) = 0x8;
    MBus_msg_flag = 0x13;
}

	
//************************************
//Internal Functions
//************************************

  uint32_t mdreg_0 = ((MD_INT_TIME<<13)|(INT_TIME<<(3+1)));
  uint32_t mdreg_1 = (((!IMG_8BIT)<<16)|(MD_TOPAD_SEL<<12)|(MD_TH<<5)|(!MD_LOWRES<<2)|(MD_LOWRES<<1));
  uint32_t mdreg_2 = ((0x1F<<19)|(0x3<<12)|(MD_MASK));
  uint32_t mdreg_3 = ((SEL_RAMP<<18)|(SEL_VB_RAMP<<14)|(SEL_VBP<<12)|(SEL_VBN<<10)|(SEL_VREFP<<7)|(SEL_VREF<<4)|(!VNW_1P2<<3)|(VNW_1P2<<2)|(!VDD_CC_1P2<<1)|(VDD_CC_1P2<<0)); // is this inversion safe??
  uint32_t mdreg_4 = ((!TAVG<<19)|(TAVG<<18)|(PULSE_SKIP_COL<<17)|(PULSE_SKIP<<16)|(SEL_CC_B<<13)|(SEL_CC<<10)|(SEL_PULSE_COL<<8)|(SEL_PULSE<<6)|(SEL_ADC_B<<3)|(SEL_ADC<<0));
  uint32_t mdreg_5 = ((SEL_CLK_DIV_LC<<16)|(SEL_CLK_RING_LC<<14)|(SEL_CLK_DIV_ADC<<11)|(SEL_CLK_DIV_4US<<9)|(SEL_CLK_DIV<<6)|(SEL_CLK_RING_ADC<<4)|(SEL_CLK_RING_4US<<2)|(SEL_CLK_RING<<0));
  uint32_t mdreg_6 = ((ROW_IDX_EN<<21)|(IMG_8BIT<<19)|(COL_SKIP<<18)|(ROW_SKIP<<17)|(END_ROW_IDX<<9)|(START_ROW_IDX<<1)|(0x1<<0));
  uint32_t mdreg_7 = ((0x7<<15));
  uint32_t mdreg_8 = ((COLS_TO_READ<<15)|(START_COL_IDX<<8)|(MD_RETURN_ADDR<<0));

static void initialize_md_reg(){

  //Write Registers;
  write_mbus_register(MD_ADDR,0x0,mdreg_0);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x1,mdreg_1);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x3,mdreg_3);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x4,mdreg_4);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x5,mdreg_5);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x6,mdreg_6);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x7,mdreg_7);
  delay (MBUS_DELAY);
  write_mbus_register(MD_ADDR,0x8,mdreg_8);
  delay (MBUS_DELAY);

}

static void poweron_frame_controller(){

  // Release MD Presleep (this also releases reset due to a design bug)
  // 2:22
  mdreg_2 &= ~(1<<22);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay(WAKEUP_DELAY);

  // Release MD Sleep
  // 2:21
  mdreg_2 &= ~(1<<21);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay(WAKEUP_DELAY);

  // Release MD Isolation
  // 7:15
  mdreg_7 &= ~(1<<15);
  write_mbus_register(MD_ADDR,0x7,mdreg_7);
  delay (MBUS_DELAY);
 
  // Release MD Reset
  // 2:23
  mdreg_2 &= ~(1<<23);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);

  // Start MD Clock
  // 5:12
  mdreg_5 |= (1<<12);
  write_mbus_register(MD_ADDR,0x5,mdreg_5);
  delay (MBUS_DELAY);

}

static void poweron_frame_controller_short(){

  // Release MD Presleep
  // 2:22
  mdreg_2 &= ~(1<<22);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);

  // Release MD Sleep
  // 2:21
  mdreg_2 &= ~(1<<21);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);

  // Release MD Isolation
  // 7:15
  mdreg_7 &= ~(1<<15);
  write_mbus_register(MD_ADDR,0x7,mdreg_7);
  delay (MBUS_DELAY);

  // Release MD Reset
  // 2:23
  mdreg_2 &= ~(1<<23);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);

  // Start MD Clock
  // 5:12
  mdreg_5 |= (1<<12);
  write_mbus_register(MD_ADDR,0x5,mdreg_5);
  delay (MBUS_DELAY);

}

static void poweron_array_adc(){

  // Release IMG Presleep 
  // 2:20
  mdreg_2 &= ~(1<<20);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay(WAKEUP_DELAY);

  // Release IMG Sleep
  // 2:19
  mdreg_2 &= ~(1<<19);
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay(WAKEUP_DELAY);

  // Release ADC Isolation
  // 7:17
  mdreg_7 &= ~(1<<17);
  write_mbus_register(MD_ADDR,0x7,mdreg_7);
  delay (MBUS_DELAY);

  // Release ADC Wrapper Reset
  // 6:0
  mdreg_6 &= ~(1<<0);
  write_mbus_register(MD_ADDR,0x6,mdreg_6);
  delay (MBUS_DELAY);

  // Start ADC Clock
  // 5:13
  mdreg_5 |= (1<<13);
  write_mbus_register(MD_ADDR,0x5,mdreg_5);
  delay (MBUS_DELAY);

}

static void poweroff_array_adc(){

  // Stop ADC Clock
  // 5:13
  mdreg_5 &= ~(1<<13);
  write_mbus_register(MD_ADDR,0x5,mdreg_5);
  delay (MBUS_DELAY);

  // Assert ADC Wrapper Reset
  // 6:0
  mdreg_6 |= 1<<0;
  write_mbus_register(MD_ADDR,0x6,mdreg_6);
  delay (MBUS_DELAY);

  // Assert ADC Isolation
  // 7:17
  mdreg_7 |= 1<<17;
  write_mbus_register(MD_ADDR,0x7,mdreg_7);
  delay (MBUS_DELAY);

  // Assert IMG Presleep 
  // 2:20
  mdreg_2 |= 1<<20;

  // Assert IMG Sleep
  // 2:19
  mdreg_2 |= 1<<19;
  write_mbus_register(MD_ADDR,0x2,mdreg_2);
  delay (MBUS_DELAY);

}


static void capture_image_single(){

  // Capture Image
  // 0:0
  mdreg_0 |= (1<<0);
  write_mbus_register(MD_ADDR,0x0,mdreg_0);
  delay(0x80); // about 6ms

  mdreg_0 &= ~(1<<0);
  write_mbus_register(MD_ADDR,0x0,mdreg_0);

  delay(DELAY_IMG); 

}

static void capture_image_start(){

  // Capture Image
  // 0:0
  mdreg_0 |= (1<<0);
  write_mbus_register(MD_ADDR,0x0,mdreg_0);
  delay(0x80); // about 6ms

}

//***************************************************
// End of Program Sleep Operations
//***************************************************
static void operation_sleep(void){

  // Reset wakeup counter
  // This is required to go back to sleep!!
  //set_wakeup_timer (0xFFF, 0x0, 0x1);
  *((volatile uint32_t *) 0xA2000014) = 0x1;

  // Reset IRQ10VEC
  *((volatile uint32_t *) IRQ10VEC/*IMSG0*/) = 0;

  // Go to Sleep
  sleep();
  while(1);

}

static void operation_sleep_noirqreset(void){

  // Reset wakeup counter
  // This is required to go back to sleep!!
  *((volatile uint32_t *) 0xA2000014) = 0x1;

  // Go to Sleep
  sleep();
  while(1);

}

static void operation_sleep_notimer(void){
    
  // Disable Timer
  set_wakeup_timer (0, 0x0, 0x0);
  // Go to sleep without timer
  operation_sleep();

}
int main() {
  
	uint32_t first_exec;

	delay(MBUS_DELAY);

	//Check if it is the first execution
	if (enumerated != 0x12345678){
	  first_exec = 1;
	  //Mark execution
	  enumerated = 0x12345678;
	}else{
	  first_exec = 0;
	}

	// Interrupts
    //Clear All Pending Interrupts
    *((volatile uint32_t *) 0xE000E280) = 0xF;
    //Enable Interrupts
    *((volatile uint32_t *) 0xE000E100) = 0xF;

	if (first_exec == 1){

		// Enumeration
		enumerate(MD_ADDR);
		delay(MBUS_DELAY);
		enumerate(FLS_ADDR);
		delay(MBUS_DELAY);
		
		// Set PMU Strength & division threshold
		// Change PMU_CTRL Register
		// PRCv9 Default: 0x8F770049
		// Fastest sleep osc: 0x8F770079
		// Fastest sleep & active osc: 0x4F773879
		//*((volatile uint32_t *) 0xA200000C) = 0x8F770079; // Works well with 1.2/0.6V override
		//*((volatile uint32_t *) 0xA200000C) = 0x4F771879; // works well with 1.2V override; if 1.2V is not overriden, system still works, but MD donesn't ACK --> MBUS voltage issue again!
		*((volatile uint32_t *) 0xA200000C) = 0x8F772879; // works without any override!
	  
		delay(DELAY_1);
	  
		// Set MBUS Clock faster
		// Change GOC_CTRL Register
		// PRCv9 Default: 0x00202903
		// 0x00202303 = Fastest MBUS clk
		// 0x00201303 = Fastest MBUS clk, faster CPU
		// 0x00200303 = Fastest MBUS clk, fastest CPU
		//*((volatile uint32_t *) 0xA2000008) = 0x00202303;
		*((volatile uint32_t *) 0xA2000008) = 0x00202603;
		
		
		delay(DELAY_1);

		// Flash Config
		// Set Tprog=0x5, Tcyc=0x01C0 
		write_mbus_register(FLS_ADDR,0x02,((0x5 << 16) | (0x01C0 << 0 )));
		delay (MBUS_DELAY);
		// Set VTG_TUNE = 0x8, CRT_TUNE=0x3F 
		write_mbus_register(FLS_ADDR,0x0A,((0x8 << 6) | (0x3F << 0 )));
		delay (MBUS_DELAY);
		// Set IRQ Addr 14 (short addr), 00 (reg addr)
		write_mbus_register(FLS_ADDR,0x0C,((0x14 << 8) | (0x00 << 0 )));
		delay (MBUS_DELAY);


	} // if first_exec

	// Initialize
	initialize_md_reg();

	// Release power gates, isolation, and reset for frame controller
	if (first_exec){
	  poweron_frame_controller();
	}else{
	  poweron_frame_controller_short();
	}

	// Release power gates, isolation, and reset for imager array
	poweron_array_adc();

	delay(WAKEUP_DELAY_FINAL);

	//capture_image_start();

	// Capture a single image
	
	//while (1){
	  capture_image_single();
	//}
	
	poweroff_array_adc();


	//* Write to Flash *//
		// Turn on Flash
		write_mbus_register(FLS_ADDR,0x11,0x000003);
		delay (MBUS_DELAY);
		asm( "wfi;" );
		// Connect Large Cap
		write_mbus_register(FLS_ADDR,0x09,0x00003F);
		delay (MBUS_DELAY);
	
		// SRAM Start Address (maybe unnecessary, since it's already reset to 0)
		//write_mbus_register(FLS_ADDR,0x06,0x000000);
		//delay (MBUS_DELAY);
		// Flash Start Address (maybe unnecessary, since it's already reset to 0)
		//write_mbus_register(FLS_ADDR,0x07,0x000000);
		//delay (MBUS_DELAY);
	
		// Erase
		write_mbus_register(FLS_ADDR,0x05,((0x1 << 15) | (0x4 << 1) | (0x1 << 0)));
		delay (MBUS_DELAY);
		asm( "wfi;" );
		
		// Copy SRAM to Flash (8kB)
		write_mbus_register(FLS_ADDR,0x05,((0x1 << 15) | (0x7FE << 4) | (0x2 << 1) | (0x1 << 0)));
		delay (MBUS_DELAY);
		asm( "wfi;" );
	
		// Turn Off Flash
		write_mbus_register(FLS_ADDR,0x11,0x000002);
		delay (MBUS_DELAY);
		asm( "wfi;" );
		// Disconnect Large Cap
		write_mbus_register(FLS_ADDR,0x09,0x000000);
		delay (MBUS_DELAY);

    set_wakeup_timer(2000, 0x1, 0x0);
	operation_sleep();

	while(1);

}
