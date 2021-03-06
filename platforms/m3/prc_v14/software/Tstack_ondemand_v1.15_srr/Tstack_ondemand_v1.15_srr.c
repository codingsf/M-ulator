//*******************************************************************
//Author: Gyouho Kim
//Description: Temperature Sensing System
//			PRCv14, SNSv7, RADv9, PMUv2, HRVv5
//			Modified from 'Pstack_Ondemand_v2.0'
//			v1.1: Changing chip order
//			v1.2: Increasing PMU floors for long term stability
//			v1.3: PMU IRQ off for stability issues
//				  RADv9 settings updated for shifted center freq and higher curr lim
//			v1.4: Decrease PMU sleep floors for reduced sleep power
//			v1.6: PMUv3; Use for Tstack batch 4 onward
//			v1.7: Chaging RF tuning to 0x04, GOC-based triggers reinitializes reg
//			v1.8: Adding battery measurement, PMU battery clamp reset, and battery discharge mode
//			v1.9: Stays awake during data TX, changing temp sensor tuning 
//			v1.10: Records 0x666 if temp sensor times out and moves on  
//				   Incorporating trig2 wakeup time adjustment based on temp measurement 
//				   LDO voltage set to be highest
//			   	  -->Used for AEC France Test in Nov 2016
//			v1.12: Dynamic adjustment of wakeup time based on CPU clock cycle
//				   Changed temp sensor polling to wfi  
//			v1.13: Optimizing PMU sleep/active settings
//				   Lower sleep power, higher sleep power during radio sleep
//				   PMU ADC disabled during radio sleep
//			v1.14: Fixing how PMU ADC is reset
//			v1.15: Turning off VDD_CLK->VBAT during PMU ADC routine
//				   Getting rid of all non-32 bit declarations
//*******************************************************************
#include "PRCv14.h"
#include "PRCv14_RF.h"
#include "mbus.h"
#include "SNSv7.h"
#include "HRVv2.h"
#include "RADv9.h"
#include "PMUv3_RF.h"
#include "MRRv3.h"

// uncomment this for debug mbus message
// #define DEBUG_MBUS_MSG
#define DEBUG_MBUS_MSG_1

// TStack order  PRC->RAD->SNS->HRV->PMU
#define HRV_ADDR 0x3
#define MRR_ADDR 0x4
#define SNS_ADDR 0x5
#define PMU_ADDR 0x6


// Temp Sensor parameters
#define	MBUS_DELAY 100 // Amount of delay between successive messages; 100: 6-7ms
#define TEMP_TIMEOUT_COUNT 2000
#define WAKEUP_PERIOD_RESET 2
#define WAKEUP_PERIOD_LDO 2
#define TEMP_CYCLE_INIT 5 

// Tstack states
#define	TSTK_IDLE       0x0
#define	TSTK_LDO        0x1
#define TSTK_TEMP_RSTRL 0x3
#define TSTK_TEMP_START 0x2
#define TSTK_TEMP_READ  0x6

#define NUM_TEMP_MEAS 5 

// Radio configurations
#define RADIO_DATA_LENGTH 24
#define RADIO_PACKET_DELAY 5000
#define RADIO_TIMEOUT_COUNT 50
#define WAKEUP_PERIOD_RADIO_INIT 2
#define DELAY_1 20000 // 5000: 0.5s

#define TEMP_STORAGE_SIZE 500 // Need to leave about 500 Bytes for stack

#define TIMERWD_VAL 0xFFFFF // 0xFFFFF about 13 sec with Y2 run default clock

//********************************************************************
// Global Variables
//********************************************************************
// "static" limits the variables to this file, giving compiler more freedom
// "volatile" should only be used for MMIO --> ensures memory storage
volatile uint32_t enumerated;
volatile uint32_t wakeup_data;
volatile uint32_t Tstack_state;
volatile uint32_t exec_count;
volatile uint32_t meas_count;
volatile uint32_t exec_count_irq;
volatile uint32_t mbus_msg_flag;
volatile uint32_t wakeup_period_count;
volatile uint32_t wakeup_timer_multiplier;
volatile uint32_t wfi_timeout_flag;

volatile snsv7_r14_t snsv7_r14 = SNSv7_R14_DEFAULT;
volatile snsv7_r15_t snsv7_r15 = SNSv7_R15_DEFAULT;
volatile snsv7_r18_t snsv7_r18 = SNSv7_R18_DEFAULT;
volatile snsv7_r25_t snsv7_r25 = SNSv7_R25_DEFAULT;
  
volatile uint32_t WAKEUP_PERIOD_CONT_USER; 
volatile uint32_t WAKEUP_PERIOD_CONT; 
volatile uint32_t WAKEUP_PERIOD_CONT_INIT; 

//volatile uint32_t temp_meas_data[NUM_TEMP_MEAS] = {0};
volatile uint32_t temp_storage[TEMP_STORAGE_SIZE] = {0};
volatile uint32_t temp_storage_latest = 2000;
volatile uint32_t temp_storage_last_wakeup_adjust = 2000;
volatile uint32_t temp_storage_diff = 0;
volatile uint32_t temp_storage_count;
volatile uint32_t temp_run_single;
volatile uint32_t temp_running;
volatile uint32_t set_temp_exec_count;
volatile uint32_t read_data_reg11; // [23:0] Temp Sensor D Out

volatile uint32_t radio_tx_count;
volatile uint32_t radio_tx_option;
volatile uint32_t radio_tx_numdata;
volatile uint32_t radio_ready;
volatile uint32_t radio_on;

volatile uint32_t read_data_batadc;

volatile radv9_r0_t radv9_r0 = RADv9_R0_DEFAULT;
volatile radv9_r1_t radv9_r1 = RADv9_R1_DEFAULT;
volatile radv9_r2_t radv9_r2 = RADv9_R2_DEFAULT;
volatile radv9_r3_t radv9_r3 = RADv9_R3_DEFAULT;
volatile radv9_r4_t radv9_r4 = RADv9_R4_DEFAULT;
volatile radv9_r5_t radv9_r5 = RADv9_R5_DEFAULT;
volatile radv9_r11_t radv9_r11 = RADv9_R11_DEFAULT;
volatile radv9_r12_t radv9_r12 = RADv9_R12_DEFAULT;
volatile radv9_r13_t radv9_r13 = RADv9_R13_DEFAULT;
volatile radv9_r14_t radv9_r14 = RADv9_R14_DEFAULT;

volatile mrrv3_r00_t mrrv3_r00 = MRRv3_R00_DEFAULT;
volatile mrrv3_r01_t mrrv3_r01 = MRRv3_R01_DEFAULT;
volatile mrrv3_r02_t mrrv3_r02 = MRRv3_R02_DEFAULT;
volatile mrrv3_r03_t mrrv3_r03 = MRRv3_R03_DEFAULT;
volatile mrrv3_r04_t mrrv3_r04 = MRRv3_R04_DEFAULT;
volatile mrrv3_r05_t mrrv3_r05 = MRRv3_R05_DEFAULT;
volatile mrrv3_r06_t mrrv3_r06 = MRRv3_R06_DEFAULT;
volatile mrrv3_r07_t mrrv3_r07 = MRRv3_R07_DEFAULT;
volatile mrrv3_r08_t mrrv3_r08 = MRRv3_R08_DEFAULT;
volatile mrrv3_r09_t mrrv3_r09 = MRRv3_R09_DEFAULT;
volatile mrrv3_r0A_t mrrv3_r0A = MRRv3_R0A_DEFAULT;
volatile mrrv3_r0B_t mrrv3_r0B = MRRv3_R0B_DEFAULT;
volatile mrrv3_r0C_t mrrv3_r0C = MRRv3_R0C_DEFAULT;
volatile mrrv3_r0D_t mrrv3_r0D = MRRv3_R0D_DEFAULT;
volatile mrrv3_r0E_t mrrv3_r0E = MRRv3_R0E_DEFAULT;
volatile mrrv3_r0F_t mrrv3_r0F = MRRv3_R0F_DEFAULT;
volatile mrrv3_r10_t mrrv3_r10 = MRRv3_R10_DEFAULT;
volatile mrrv3_r11_t mrrv3_r11 = MRRv3_R11_DEFAULT;
volatile mrrv3_r12_t mrrv3_r12 = MRRv3_R12_DEFAULT;
volatile mrrv3_r13_t mrrv3_r13 = MRRv3_R13_DEFAULT;
volatile mrrv3_r1B_t mrrv3_r1B = MRRv3_R1B_DEFAULT;
volatile mrrv3_r1C_t mrrv3_r1C = MRRv3_R1C_DEFAULT;

volatile hrvv2_r0_t hrvv2_r0 = HRVv2_R0_DEFAULT;

volatile prcv14_r0B_t prcv14_r0B = PRCv14_R0B_DEFAULT;

//*******************************************************************
// INTERRUPT HANDLERS
//*******************************************************************
void handler_ext_int_0(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_1(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_2(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_3(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_4(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_5(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_6(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_7(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_8(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_9(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_10(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_11(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_12(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_13(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_14(void) __attribute__ ((interrupt ("IRQ")));

void handler_ext_int_0(void)  { *NVIC_ICPR = (0x1 << 0); wfi_timeout_flag = 1;} // TIMER32
void handler_ext_int_1(void)  { *NVIC_ICPR = (0x1 << 1);  } // TIMER16
void handler_ext_int_2(void)  { *NVIC_ICPR = (0x1 << 2); mbus_msg_flag = 0x10; } // REG0
void handler_ext_int_3(void)  { *NVIC_ICPR = (0x1 << 3); mbus_msg_flag = 0x11; } // REG1
void handler_ext_int_4(void)  { *NVIC_ICPR = (0x1 << 4); mbus_msg_flag = 0x12; } // REG2
void handler_ext_int_5(void)  { *NVIC_ICPR = (0x1 << 5); mbus_msg_flag = 0x13; } // REG3
void handler_ext_int_6(void)  { *NVIC_ICPR = (0x1 << 6); mbus_msg_flag = 0x14; } // REG4
void handler_ext_int_7(void)  { *NVIC_ICPR = (0x1 << 7); mbus_msg_flag = 0x15; } // REG5
void handler_ext_int_8(void)  { *NVIC_ICPR = (0x1 << 8); mbus_msg_flag = 0x16; } // REG6
void handler_ext_int_9(void)  { *NVIC_ICPR = (0x1 << 9); mbus_msg_flag = 0x17; } // REG7
void handler_ext_int_10(void) { *NVIC_ICPR = (0x1 << 10); } // MEM WR
void handler_ext_int_11(void) { *NVIC_ICPR = (0x1 << 11); } // MBUS_RX
void handler_ext_int_12(void) { *NVIC_ICPR = (0x1 << 12); } // MBUS_TX
void handler_ext_int_13(void) { *NVIC_ICPR = (0x1 << 13); } // MBUS_FWD
void handler_ext_int_14(void) { *NVIC_ICPR = (0x1 << 14); } // MBUS_FWD


//************************************
// PMU Related Functions
//************************************

inline static void set_pmu_adc_period(uint32_t val){
	// PMU_CONTROLLER_DESIRED_STATE Active
	mbus_remote_register_write(PMU_ADDR,0x3C,
		((  1 << 0) //state_sar_scn_on
		| (0 << 1) //state_wait_for_clock_cycles
		| (1 << 2) //state_wait_for_time
		| (1 << 3) //state_sar_scn_reset
		| (1 << 4) //state_sar_scn_stabilized
		| (1 << 5) //state_sar_scn_ratio_roughly_adjusted
		| (1 << 6) //state_clock_supply_switched
		| (1 << 7) //state_control_supply_switched
		| (1 << 8) //state_upconverter_on
		| (1 << 9) //state_upconverter_stabilized
		| (1 << 10) //state_refgen_on
		| (0 << 11) //state_adc_output_ready
		| (0 << 12) //state_adc_adjusted
		| (0 << 13) //state_sar_scn_ratio_adjusted
		| (1 << 14) //state_downconverter_on
		| (1 << 15) //state_downconverter_stabilized
		| (1 << 16) //state_vdd_3p6_turned_on
		| (1 << 17) //state_vdd_1p2_turned_on
		| (1 << 18) //state_vdd_0P6_turned_on
		| (1 << 19) //state_state_horizon
	));
	delay(MBUS_DELAY*10);

	// Register 0x36: TICK_REPEAT_VBAT_ADJUST
    mbus_remote_register_write(PMU_ADDR,0x36,val); 
	delay(MBUS_DELAY*10);

	// PMU_CONTROLLER_DESIRED_STATE Active
	mbus_remote_register_write(PMU_ADDR,0x3C,
		((  1 << 0) //state_sar_scn_on
		| (1 << 1) //state_wait_for_clock_cycles
		| (1 << 2) //state_wait_for_time
		| (1 << 3) //state_sar_scn_reset
		| (1 << 4) //state_sar_scn_stabilized
		| (1 << 5) //state_sar_scn_ratio_roughly_adjusted
		| (1 << 6) //state_clock_supply_switched
		| (1 << 7) //state_control_supply_switched
		| (1 << 8) //state_upconverter_on
		| (1 << 9) //state_upconverter_stabilized
		| (1 << 10) //state_refgen_on
		| (0 << 11) //state_adc_output_ready
		| (0 << 12) //state_adc_adjusted
		| (0 << 13) //state_sar_scn_ratio_adjusted
		| (1 << 14) //state_downconverter_on
		| (1 << 15) //state_downconverter_stabilized
		| (1 << 16) //state_vdd_3p6_turned_on
		| (1 << 17) //state_vdd_1p2_turned_on
		| (1 << 18) //state_vdd_0P6_turned_on
		| (1 << 19) //state_state_horizon
	));
	delay(MBUS_DELAY);
}

inline static void set_pmu_sleep_clk_radio(){
	// Register 0x15: SAR_TRIM_v3_SLEEP
    mbus_remote_register_write(PMU_ADDR,0x15, 
		( (0 << 19) // Enable PFM even during periodic reset
		| (0 << 18) // Enable PFM even when Vref is not used as ref
		| (0 << 17) // Enable PFM
		| (3 << 14) // Comparator clock division ratio
		| (0 << 13) // Enable main feedback loop
		| (14 << 9)  // Frequency multiplier R
		| (14 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
    mbus_remote_register_write(PMU_ADDR,0x15, 
		( (0 << 19) // Enable PFM even during periodic reset
		| (0 << 18) // Enable PFM even when Vref is not used as ref
		| (0 << 17) // Enable PFM
		| (3 << 14) // Comparator clock division ratio
		| (0 << 13) // Enable main feedback loop
		| (14 << 9)  // Frequency multiplier R
		| (14 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x17: V3P6 Upconverter Sleep Settings
    mbus_remote_register_write(PMU_ADDR,0x17, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (2 << 9)  // Frequency multiplier R
		| (3 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
}

inline static void set_pmu_sleep_clk_low(){
	// Register 0x17: V3P6 Upconverter Sleep Settings
    mbus_remote_register_write(PMU_ADDR,0x17, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
    mbus_remote_register_write(PMU_ADDR,0x17, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x15: SAR_TRIM_v3_SLEEP
    mbus_remote_register_write(PMU_ADDR,0x15, 
		( (0 << 19) // Enable PFM even during periodic reset
		| (0 << 18) // Enable PFM even when Vref is not used as ref
		| (0 << 17) // Enable PFM
		| (3 << 14) // Comparator clock division ratio
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
}


inline static void set_pmu_clk_init(){
	// Register 0x17: V3P6 Upconverter Sleep Settings
    mbus_remote_register_write(PMU_ADDR,0x17, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// The first register write to PMU needs to be repeated
    mbus_remote_register_write(PMU_ADDR,0x17, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x18: V3P6 Upconverter Active Settings
    mbus_remote_register_write(PMU_ADDR,0x18, 
		( (3 << 14) // Desired Vout/Vin ratio; defualt: 0
		| (0 << 13) // Enable main feedback loop
		| (2 << 9)  // Frequency multiplier R
		| (0 << 5)  // Frequency multiplier L (actually L+1)
		| (16) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x19: DOWNCONV_TRIM_V3_SLEEP
    mbus_remote_register_write(PMU_ADDR,0x19,
		( (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (1 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x1A: DOWNCONV_TRIM_V3_ACTIVE
    mbus_remote_register_write(PMU_ADDR,0x1A,
		( (0 << 13) // Enable main feedback loop
		| (4 << 9)  // Frequency multiplier R
		| (0 << 5)  // Frequency multiplier L (actually L+1)
		| (16) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x15: V1P2 SAR_TRIM_v3_SLEEP
    mbus_remote_register_write(PMU_ADDR,0x15, 
		( (0 << 19) // Enable PFM even during periodic reset
		| (0 << 18) // Enable PFM even when Vref is not used as ref
		| (0 << 17) // Enable PFM
		| (3 << 14) // Comparator clock division ratio
		| (0 << 13) // Enable main feedback loop
		| (1 << 9)  // Frequency multiplier R
		| (2 << 5)  // Frequency multiplier L (actually L+1)
		| (1) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// Register 0x16: V1P2 SAR_TRIM_v3_ACTIVE
    mbus_remote_register_write(PMU_ADDR,0x16, 
		( (0 << 19) // Enable PFM even during periodic reset
		| (0 << 18) // Enable PFM even when Vref is not used as ref
		| (0 << 17) // Enable PFM
		| (3 << 14) // Comparator clock division ratio
		| (0 << 13) // Enable main feedback loop
		| (4 << 9)  // Frequency multiplier R
		| (0 << 5)  // Frequency multiplier L (actually L+1)
		| (16) 		// Floor frequency base (0-63)
	));
	delay(MBUS_DELAY);
	// SAR_RATIO_OVERRIDE
	// Use the new reset scheme in PMUv3
    mbus_remote_register_write(PMU_ADDR,0x05, //default 12'h000
		( (0 << 13) // Enables override setting [12] (1'b1)
		| (0 << 12) // Let VDD_CLK always connected to vbat
		| (1 << 11) // Enable override setting [10] (1'h0)
		| (0 << 10) // Have the converter have the periodic reset (1'h0)
		| (0 << 9) // Enable override setting [8] (1'h0)
		| (0 << 8) // Switch input / output power rails for upconversion (1'h0)
		| (0 << 7) // Enable override setting [6:0] (1'h0)
		| (44) 		// Binary converter's conversion ratio (7'h00)
	));
	delay(MBUS_DELAY);
    mbus_remote_register_write(PMU_ADDR,0x05, //default 12'h000
		( (1 << 13) // Enables override setting [12] (1'b1)
		| (0 << 12) // Let VDD_CLK always connected to vbat
		| (1 << 11) // Enable override setting [10] (1'h0)
		| (0 << 10) // Have the converter have the periodic reset (1'h0)
		| (1 << 9) // Enable override setting [8] (1'h0)
		| (0 << 8) // Switch input / output power rails for upconversion (1'h0)
		| (1 << 7) // Enable override setting [6:0] (1'h0)
		| (44) 		// Binary converter's conversion ratio (7'h00)
	));
	delay(MBUS_DELAY);

	set_pmu_adc_period(0); // 0x100 about 1 min for 1/2/1 1P2 setting
}


inline static void pmu_adc_reset_setting(){
	// PMU ADC will be automatically reset when system wakes up
	// PMU_CONTROLLER_DESIRED_STATE Active
	mbus_remote_register_write(PMU_ADDR,0x3C,
		((  1 << 0) //state_sar_scn_on
		| (1 << 1) //state_wait_for_clock_cycles
		| (1 << 2) //state_wait_for_time
		| (1 << 3) //state_sar_scn_reset
		| (1 << 4) //state_sar_scn_stabilized
		| (1 << 5) //state_sar_scn_ratio_roughly_adjusted
		| (1 << 6) //state_clock_supply_switched
		| (1 << 7) //state_control_supply_switched
		| (1 << 8) //state_upconverter_on
		| (1 << 9) //state_upconverter_stabilized
		| (1 << 10) //state_refgen_on
		| (0 << 11) //state_adc_output_ready
		| (0 << 12) //state_adc_adjusted
		| (0 << 13) //state_sar_scn_ratio_adjusted
		| (1 << 14) //state_downconverter_on
		| (1 << 15) //state_downconverter_stabilized
		| (1 << 16) //state_vdd_3p6_turned_on
		| (1 << 17) //state_vdd_1p2_turned_on
		| (1 << 18) //state_vdd_0P6_turned_on
		| (1 << 19) //state_state_horizon
	));
	delay(MBUS_DELAY);
}

inline static void pmu_adc_disable(){
	// PMU ADC will be automatically reset when system wakes up
	// PMU_CONTROLLER_DESIRED_STATE Sleep
	mbus_remote_register_write(PMU_ADDR,0x3B,
		((  1 << 0) //state_sar_scn_on
		| (1 << 1) //state_wait_for_clock_cycles
		| (1 << 2) //state_wait_for_time
		| (1 << 3) //state_sar_scn_reset
		| (1 << 4) //state_sar_scn_stabilized
		| (1 << 5) //state_sar_scn_ratio_roughly_adjusted
		| (1 << 6) //state_clock_supply_switched
		| (1 << 7) //state_control_supply_switched
		| (1 << 8) //state_upconverter_on
		| (1 << 9) //state_upconverter_stabilized
		| (1 << 10) //state_refgen_on
		| (0 << 11) //state_adc_output_ready
		| (0 << 12) //state_adc_adjusted
		| (0 << 13) //state_sar_scn_ratio_adjusted
		| (1 << 14) //state_downconverter_on
		| (1 << 15) //state_downconverter_stabilized
		| (1 << 16) //state_vdd_3p6_turned_on
		| (1 << 17) //state_vdd_1p2_turned_on
		| (1 << 18) //state_vdd_0P6_turned_on
		| (1 << 19) //state_state_horizon
	));
	delay(MBUS_DELAY);
}

inline static void pmu_adc_enable(){
	// PMU ADC will be automatically reset when system wakes up
	// PMU_CONTROLLER_DESIRED_STATE Sleep
	mbus_remote_register_write(PMU_ADDR,0x3B,
		((  1 << 0) //state_sar_scn_on
		| (1 << 1) //state_wait_for_clock_cycles
		| (1 << 2) //state_wait_for_time
		| (1 << 3) //state_sar_scn_reset
		| (1 << 4) //state_sar_scn_stabilized
		| (1 << 5) //state_sar_scn_ratio_roughly_adjusted
		| (1 << 6) //state_clock_supply_switched
		| (1 << 7) //state_control_supply_switched
		| (1 << 8) //state_upconverter_on
		| (1 << 9) //state_upconverter_stabilized
		| (1 << 10) //state_refgen_on
		| (1 << 11) //state_adc_output_ready
		| (0 << 12) //state_adc_adjusted // Turning off offset cancellation
		| (1 << 13) //state_sar_scn_ratio_adjusted
		| (1 << 14) //state_downconverter_on
		| (1 << 15) //state_downconverter_stabilized
		| (1 << 16) //state_vdd_3p6_turned_on
		| (1 << 17) //state_vdd_1p2_turned_on
		| (1 << 18) //state_vdd_0P6_turned_on
		| (1 << 19) //state_state_horizon
	));
	delay(MBUS_DELAY);
}

inline static void reset_pmu_solar_short(){
    mbus_remote_register_write(PMU_ADDR,0x0E, 
		( (1 << 10) // When to turn on harvester-inhibiting switch (0: PoR, 1: VBAT high)
		| (1 << 9)  // Enables override setting [8]
		| (0 << 8)  // Turn on the harvester-inhibiting switch
		| (1 << 4)  // clamp_tune_bottom (increases clamp thresh)
		| (0) 		// clamp_tune_top (decreases clamp thresh)
	));
	delay(MBUS_DELAY);
    mbus_remote_register_write(PMU_ADDR,0x0E, 
		( (1 << 10) // When to turn on harvester-inhibiting switch (0: PoR, 1: VBAT high)
		| (1 << 9)  // Enables override setting [8]
		| (0 << 8)  // Turn on the harvester-inhibiting switch
		| (1 << 4)  // clamp_tune_bottom (increases clamp thresh)
		| (0) 		// clamp_tune_top (decreases clamp thresh)
	));
	delay(MBUS_DELAY);
    mbus_remote_register_write(PMU_ADDR,0x0E, 
		( (1 << 10) // When to turn on harvester-inhibiting switch (0: PoR, 1: VBAT high)
		| (0 << 9)  // Enables override setting [8]
		| (0 << 8)  // Turn on the harvester-inhibiting switch
		| (1 << 4)  // clamp_tune_bottom (increases clamp thresh)
		| (0) 		// clamp_tune_top (decreases clamp thresh)
	));
	delay(MBUS_DELAY);
}

//***************************************************
// Radio transmission routines for PPM Radio (RADv9)
//***************************************************

static void radio_power_on(){
	// Turn off PMU ADC
	pmu_adc_disable();

	// Need to speed up sleep pmu clock
	set_pmu_sleep_clk_radio();
	
	// This can be safely assumed
	radio_ready = 0;

    // Turn on Current Limter
    mrrv3_r00.MRR_CL_EN = 1;  //Enable CL
    mbus_remote_register_write(MRR_ADDR,0x00,mrrv3_r00.as_int);

    mrrv3_r04.MRR_SCRO_EN_TIMER = 1;  //power on TIMER
    mbus_remote_register_write(MRR_ADDR,0x04,mrrv3_r04.as_int);
	delay(MBUS_DELAY*300); // LDO stab 1s

    mrrv3_r04.MRR_SCRO_RSTN_TIMER = 1;  //UNRST TIMER
    mbus_remote_register_write(MRR_ADDR,0x04,mrrv3_r04.as_int);
	delay(MBUS_DELAY*100); // Freq stab

    mrrv3_r04.MRR_SCRO_EN_CLK = 1;  //Enable clk
    mbus_remote_register_write(MRR_ADDR,0x04,mrrv3_r04.as_int);
	delay(MBUS_DELAY*100); // Freq stab

    // Release FSM Sleep
    mrrv3_r0E.MRR_RAD_FSM_SLEEP = 0;  // Power on BB
    mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);
	delay(MBUS_DELAY*100); // Freq stab

    radio_on = 1;
}

static void radio_power_off(){
	// Need to restore sleep pmu clock
	set_pmu_sleep_clk_low();
	
	// Enable PMU ADC
	pmu_adc_enable();

    // Turn off everything
    radio_on = 0;
	radio_ready = 0;

    mrrv3_r03.MRR_TRX_ISOLATEN = 0;     //set ISOLATEN 0
    mbus_remote_register_write(MRR_ADDR,0x03,mrrv3_r03.as_int);

    mrrv3_r0E.MRR_RAD_FSM_EN = 0;  //Stop BB
    mrrv3_r0E.MRR_RAD_FSM_RSTN = 0;  //RST BB
    mrrv3_r0E.MRR_RAD_FSM_SLEEP = 1;
    mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);

    // Turn off Current Limter
    mrrv3_r00.MRR_CL_EN = 0;  //Enable CL
    mbus_remote_register_write(MRR_ADDR,0x00,mrrv3_r00.as_int);

    mrrv3_r04.MRR_SCRO_EN_TIMER = 0;
    mrrv3_r04.MRR_SCRO_RSTN_TIMER = 0;
    mrrv3_r04.MRR_SCRO_EN_CLK = 0;
    mbus_remote_register_write(MRR_ADDR,0x04,mrrv3_r04.as_int);

}

static void send_radio_data_ppm(uint32_t last_packet, uint32_t radio_data){
    // Write Data: Only up to 24bit data for now
    mrrv3_r06.MRR_RAD_FSM_TX_DATA_0 = radio_data; //SCC
    mbus_remote_register_write(MRR_ADDR,0x06,mrrv3_r06.as_int);

    if (!radio_ready){
		radio_ready = 1;
		
		// Release FSM Reset
		mrrv3_r0E.MRR_RAD_FSM_RSTN = 1; //UNRST BB
		mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);
		delay(MBUS_DELAY*10);

    	mrrv3_r03.MRR_TRX_ISOLATEN = 1; //set ISOLATEN 1, let state machine control
    	mbus_remote_register_write(MRR_ADDR,0x03,mrrv3_r03.as_int);
		delay(MBUS_DELAY*10);
    }

	// Use Timer32 as timeout counter
	config_timer32(0x150000, 1, 0, 0); // 1/10 of MBUS watchdog timer default

    // Fire off data
    uint32_t count;
    mbus_msg_flag = 0;
    wfi_timeout_flag = 0;
	mrrv3_r0E.MRR_RAD_FSM_EN = 1;  //Start BB
	mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);

	// Wait for radio response
	WFI();

	// Turn off Timer32
	*TIMER32_GO = 0;

	if (wfi_timeout_flag){
		mbus_write_message32(0xFA, 0xFAFAFAFA);
	}

	if (last_packet){
		radio_ready = 0;
		radio_power_off();
	}else{
		mrrv3_r0E.MRR_RAD_FSM_EN = 0;
		mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);
		delay(MBUS_DELAY);
	}
}

//***************************************************
// Temp Sensor Functions (SNSv7)
//***************************************************

static void temp_sensor_enable(){
    snsv7_r14.TEMP_SENSOR_ENABLEb = 0x0;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
}
static void temp_sensor_disable(){
    snsv7_r14.TEMP_SENSOR_ENABLEb = 1;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
}
static void temp_sensor_release_reset(){
    snsv7_r14.TEMP_SENSOR_RESETn = 1;
    snsv7_r14.TEMP_SENSOR_ISO = 0;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
}
static void temp_sensor_assert_reset(){
    snsv7_r14.TEMP_SENSOR_RESETn = 0;
    snsv7_r14.TEMP_SENSOR_ISO = 1;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
}
static void ldo_power_off(){
    snsv7_r18.ADC_LDO_ADC_LDO_DLY_ENB = 1;
    snsv7_r18.ADC_LDO_ADC_LDO_ENB = 1;
    mbus_remote_register_write(SNS_ADDR,18,snsv7_r18.as_int);
}
static void temp_power_off(){
    snsv7_r14.TEMP_SENSOR_ENABLEb = 1;
    snsv7_r14.TEMP_SENSOR_RESETn = 0;
    snsv7_r14.TEMP_SENSOR_ISO = 1;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
    ldo_power_off();
}


//***************************************************
// End of Program Sleep Operation
//***************************************************
static void operation_sleep(void){

	// Reset IRQ14VEC
	*((volatile uint32_t *) IRQ14VEC) = 0;

    // Go to Sleep
    mbus_sleep_all();
    while(1);

}

static void operation_sleep_noirqreset(void){

    // Go to Sleep
    mbus_sleep_all();
    while(1);

}

static void operation_sleep_notimer(void){
    
    // Make sure LDO is off
    ldo_power_off();
	
    // Make sure Radio is off
    if (radio_on){radio_power_off();}

    // Disable Timer
    set_wakeup_timer(0, 0, 0);

    // Go to sleep without timer
    operation_sleep();

}


static void operation_tx_stored(void){

    //Fire off stored data to radio
    while(((!radio_tx_numdata)&&(radio_tx_count > 0)) | ((radio_tx_numdata)&&((radio_tx_numdata+radio_tx_count) > temp_storage_count))){
		#ifdef DEBUG_MBUS_MSG_1
			mbus_write_message32(0xDD, radio_tx_count);
			delay(MBUS_DELAY);
			mbus_write_message32(0xDD, temp_storage[radio_tx_count]);
			delay(MBUS_DELAY);
		#endif

		// Reset watchdog timer
		config_timerwd(TIMERWD_VAL);

		// Radio out data
		send_radio_data_ppm(0, temp_storage[radio_tx_count]);
		delay(RADIO_PACKET_DELAY); //Set delays between sending subsequent packet

		radio_tx_count--;
    }

	delay(RADIO_PACKET_DELAY*2); //Set delays between sending subsequent packet
	send_radio_data_ppm(1, 0xFAF000);

	// This is also the end of this IRQ routine
	exec_count_irq = 0;

	// Go to sleep without timer
	radio_tx_count = temp_storage_count; // allows data to be sent more than once
	operation_sleep_notimer();
}

uint32_t dumb_divide(uint32_t nu, uint32_t de) {
// Returns quotient of nu/de

    uint32_t temp = 1;
    uint32_t quotient = 0;

    while (de <= nu) {
        de <<= 1;
        temp <<= 1;
    }

    //printf("%d %d\n",de,temp,nu);
    while (temp > 1) {
        de >>= 1;
        temp >>= 1;

        if (nu >= de) {
            nu -= de;
            //printf("%d %d\n",quotient,temp);
            quotient += temp;
        }
    }

    return quotient;
}

static void measure_wakeup_period(void){

	mbus_write_message32(0xE0, 0x0);
	// Prevent watchdog kicking in
   	config_timerwd(TIMERWD_VAL);

	uint32_t wakeup_timer_val_0 = *((volatile uint32_t *) REG_WUPT_VAL);
	wakeup_period_count = 0;

	while( *((volatile uint32_t *) REG_WUPT_VAL) == wakeup_timer_val_0){
		wakeup_period_count = 0;
	}
	wakeup_timer_val_0++;
	mbus_write_message32(0xE1, wakeup_timer_val_0);
	while( *((volatile uint32_t *) REG_WUPT_VAL) == wakeup_timer_val_0){
		wakeup_period_count++;
	}
	mbus_write_message32(0xE2, wakeup_period_count);
	delay(MBUS_DELAY);

   	config_timerwd(TIMERWD_VAL);
	WAKEUP_PERIOD_CONT = dumb_divide(WAKEUP_PERIOD_CONT_USER*1000*10, wakeup_period_count);
	mbus_write_message32(0xED, WAKEUP_PERIOD_CONT); 
	delay(MBUS_DELAY);
}


static void operation_init(void){
  
	// Set CPU & Mbus Clock Speeds
    prcv14_r0B.DSLP_CLK_GEN_FAST_MODE = 0x1; // Default 0x0
    prcv14_r0B.CLK_GEN_RING = 0x1; // Default 0x1
    prcv14_r0B.CLK_GEN_DIV_MBC = 0x1; // Default 0x1
    prcv14_r0B.CLK_GEN_DIV_CORE = 0x3; // Default 0x3
	*((volatile uint32_t *) REG_CLKGEN_TUNE ) = prcv14_r0B.as_int;

  
    //Enumerate & Initialize Registers
    Tstack_state = TSTK_IDLE; 	//0x0;
    enumerated = 0xDEADBEEF;
    exec_count = 0;
    exec_count_irq = 0;
    mbus_msg_flag = 0;
  
    // Set CPU Halt Option as RX --> Use for register read e.g.
//    set_halt_until_mbus_rx();

    //Enumeration
	delay(MBUS_DELAY);
    mbus_enumerate(MRR_ADDR);
	delay(MBUS_DELAY);
    mbus_enumerate(SNS_ADDR);
	delay(MBUS_DELAY);
    mbus_enumerate(HRV_ADDR);
	delay(MBUS_DELAY);
 	mbus_enumerate(PMU_ADDR);
	delay(MBUS_DELAY);

    // Set CPU Halt Option as TX --> Use for register write e.g.
	//    set_halt_until_mbus_tx();

	// Disable PMUv2 IRQ
    //mbus_remote_register_write(PMU_ADDR,0x51,0x09);
	//delay(MBUS_DELAY);

	// PMU Settings ----------------------------------------------
	set_pmu_clk_init();
	reset_pmu_solar_short();

	// Disable PMU ADC measurement in active mode
	// PMU_CONTROLLER_STALL_ACTIVE
    mbus_remote_register_write(PMU_ADDR,0x3A, 
		( (1 << 19) // ignore state_horizon; default 1
		| (1 << 13) // ignore adc_output_ready; default 0
		| (1 << 12) // ignore adc_output_ready; default 0
		| (1 << 11) // ignore adc_output_ready; default 0
	));
    delay(MBUS_DELAY);
	pmu_adc_reset_setting();
	delay(MBUS_DELAY);
	pmu_adc_enable();
	delay(MBUS_DELAY);

    // Temp Sensor Settings --------------------------------------
	// SNSv7_R25
	snsv7_r25.TEMP_SENSOR_IRQ_PACKET = 0x001000;
    mbus_remote_register_write(SNS_ADDR,0x19,snsv7_r25.as_int);
    // SNSv7_R14
    snsv7_r14.TEMP_SENSOR_BURST_MODE = 0x0;
	snsv7_r14.TEMP_SENSOR_DELAY_SEL = 5;
    snsv7_r14.TEMP_SENSOR_R_tmod = 0x0;
    snsv7_r14.TEMP_SENSOR_R_bmod = 0x0;
    mbus_remote_register_write(SNS_ADDR,0xE,snsv7_r14.as_int);
    // snsv7_R15
    snsv7_r15.TEMP_SENSOR_AMP_BIAS = 0x7; // Default: 2
    snsv7_r15.TEMP_SENSOR_CONT_MODEb = 0x0;
	snsv7_r15.TEMP_SENSOR_SEL_CT = 6;
    mbus_remote_register_write(SNS_ADDR,0xF,snsv7_r15.as_int);

    // snsv7_R18
    snsv7_r18.ADC_LDO_ADC_LDO_ENB      = 0x1;
    snsv7_r18.ADC_LDO_ADC_LDO_DLY_ENB  = 0x1;
    snsv7_r18.ADC_LDO_ADC_CURRENT_2X  = 0x1;

    // Set ADC LDO to around 1.37V: 0x3//0x20
    snsv7_r18.ADC_LDO_ADC_VREF_MUX_SEL = 0x3;
    snsv7_r18.ADC_LDO_ADC_VREF_SEL     = 0x20;

    mbus_remote_register_write(SNS_ADDR,18,snsv7_r18.as_int);

	// CDC Mbus return address; Needs to be between 0x18-0x1F
    mbus_remote_register_write(SNS_ADDR,0x18,0x1800);

    // Radio Settings (MRRv3) -------------------------------------------
    mrrv3_r1C.LC_CLK_RING = 0x3;  // ~ 150 kHz
    mrrv3_r1C.LC_CLK_DIV = 0x3;  // ~ 150 kHz
    mbus_remote_register_write(MRR_ADDR,0x1C,mrrv3_r1C.as_int);

    // Current Limter set-up 
    mrrv3_r00.MRR_CL_CTRL = 16; //Set CL 1-finite 16-20uA

    // TX Setup Carrier Freq
    mrrv3_r00.MRR_TRX_CAP_ANTP_TUNE = 0x0000;  //ANT CAP 14b unary 830.5 MHz
	//mrrv3_r00.MRR_TRX_CAP_ANTP_TUNE = 0x00FF;  //ANT CAP 14b unary 813.8 MHz
	//mrrv3_r00.MRR_TRX_CAP_ANTP_TUNE = 0x0FFF;  //ANT CAP 14b unary 805.5 MHz
    mbus_remote_register_write(MRR_ADDR,0x00,mrrv3_r00.as_int);
    mrrv3_r01.MRR_TRX_CAP_ANTN_TUNE = 0x0000; //ANT CAP 14b unary 830.5 MHz
	//mrrv3_r01.MRR_TRX_CAP_ANTN_TUNE = 0x00FF; //ANT CAP 14b unary 813.8 MHz
	//mrrv3_r01.MRR_TRX_CAP_ANTN_TUNE = 0x0FFF;  //ANT CAP 14b unary 805.5 MHz
    mbus_remote_register_write(MRR_ADDR,0x01,mrrv3_r01.as_int);
    mrrv3_r02.MRR_TX_BIAS_TUNE = 0x1FFF;  //Set TX BIAS TUNE 13b // Set to max
    mbus_remote_register_write(MRR_ADDR,0x02,mrrv3_r02.as_int);

    // RX Setup
	mrrv3_r03.MRR_RX_BIAS_TUNE    = 0x000F;//  turn on Q_enhancement
    mrrv3_r03.MRR_RX_SAMPLE_CAP    = 0x1;  // RX_SAMPLE_CAP
    mbus_remote_register_write(MRR_ADDR,3,mrrv3_r03.as_int);

    mrrv3_r11.MRR_RAD_FSM_RX_POWERON_LEN = 0x0;  //Set RX Power on length
    //mrrv3_r11.MRR_RAD_FSM_RX_SAMPLE_LEN = 0x3;  //Set RX Sample length  16us
    mrrv3_r11.MRR_RAD_FSM_RX_SAMPLE_LEN = 0x7;  //Set RX Sample length  4us
    mrrv3_r11.MRR_RAD_FSM_GUARD_LEN = 0x001C; //Set TX_RX Guard length, TX_RX guard 32 cycle (28+5)
    mbus_remote_register_write(MRR_ADDR,0x11,mrrv3_r11.as_int);

    mrrv3_r12.MRR_RAD_FSM_RX_HDR_BITS = 0x00;  //Set RX header
    mrrv3_r12.MRR_RAD_FSM_RX_HDR_TH = 0x00;    //Set RX header threshold
    mrrv3_r12.MRR_RAD_FSM_RX_DATA_BITS = 0x10; //Set RX data 16b
    mbus_remote_register_write(MRR_ADDR,0x12,mrrv3_r12.as_int);

    mrrv3_r1B.MRR_IRQ_REPLY_PACKET = 0x061400; //Read RX data Reply
    mbus_remote_register_write(MRR_ADDR,0x1B,mrrv3_r1B.as_int);

    // RAD_FSM set-up 
    mrrv3_r0E.MRR_RAD_FSM_TX_H_LEN = 31; //31-31b header (max)
    mrrv3_r0E.MRR_RAD_FSM_TX_D_LEN = RADIO_DATA_LENGTH;//40; //0-skip tx data
    mbus_remote_register_write(MRR_ADDR,0x0E,mrrv3_r0E.as_int);

    mrrv3_r0F.MRR_RAD_FSM_TX_PW_LEN = 0; //4us PW
    mrrv3_r10.MRR_RAD_FSM_TX_C_LEN = 64; // (PW_LEN+1):C_LEN=1:32
    mrrv3_r0F.MRR_RAD_FSM_TX_PS_LEN = 0; // PW=PS
    mrrv3_r12.MRR_RAD_FSM_TX_HDR_CNST = 0; //no shift in LFSR

    //mrrv3_r0F.MRR_RAD_FSM_TX_PW_LEN = 1; //8us PW
    //mrrv3_r10.MRR_RAD_FSM_TX_C_LEN = 64; // (PW_LEN+1):C_LEN=1:32
    //mrrv3_r0F.MRR_RAD_FSM_TX_PS_LEN = 1; // PW=PS
    //mrrv3_r0F.MRR_RAD_FSM_TX_PW_LEN = 124; //500us PW
    //mrrv3_r10.MRR_RAD_FSM_TX_C_LEN = 4000; // (PW_LEN+1):C_LEN=1:32
    //mrrv3_r0F.MRR_RAD_FSM_TX_PS_LEN = 124; // PW=PS
    
    //mrrv3_r0F.MRR_RAD_FSM_TX_PW_LEN = 249; //1ms PW
    //mrrv3_r10.MRR_RAD_FSM_TX_C_LEN = 8000; // (PW_LEN+1):C_LEN=1:32
    //mrrv3_r0F.MRR_RAD_FSM_TX_PS_LEN = 249; // PW=PS
    //mrrv3_r12.MRR_RAD_FSM_TX_HDR_CNST = 8; //8 bit shift in LFSR

    mbus_remote_register_write(MRR_ADDR,0x0F,mrrv3_r0F.as_int);
    mbus_remote_register_write(MRR_ADDR,0x12,mrrv3_r12.as_int);
    
	// Use pulse generator -- Not used currently
    mrrv3_r02.MRR_TX_PULSE_FINE = 1;
    mrrv3_r02.MRR_TX_PULSE_FINE_TUNE = 3;
    mbus_remote_register_write(MRR_ADDR,0x02,mrrv3_r02.as_int);

    mrrv3_r10.MRR_RAD_FSM_SEED = 1; //default
    mrrv3_r10.MRR_RAD_FSM_TX_MODE = 3; //code rate 0:4 1:3 2:2 3:1(baseline) 4:1/2 5:1/3 6:1/4
    mbus_remote_register_write(MRR_ADDR,0x10,mrrv3_r10.as_int);

    mrrv3_r11.MRR_RAD_FSM_TX_POWERON_LEN = 7; //3bits
    mbus_remote_register_write(MRR_ADDR,0x11,mrrv3_r11.as_int);

	// Mbus return address; Needs to be between 0x18-0x1F
    mbus_remote_register_write(MRR_ADDR,0x1B,0x1A00);



    // Initialize other global variables
    WAKEUP_PERIOD_CONT = 33750;   // 1: 2-4 sec with PRCv9
    WAKEUP_PERIOD_CONT_INIT = 3;   // 0x1E (30): ~1 min with PRCv9
    temp_storage_count = 0;
    radio_tx_count = 0;
    radio_tx_option = 0; //enables radio tx for each measurement 
    temp_run_single = 0;
    temp_running = 0;
    radio_ready = 0;
    radio_on = 0;
	wakeup_data = 0;
	set_temp_exec_count = 0; // specifies how many temp sensor executes; 0: unlimited, n: 50*2^n

    // Harvester Settings --------------------------------------
    hrvv2_r0.HRV_TOP_CONV_RATIO = 0x6;
    mbus_remote_register_write(HRV_ADDR,0,hrvv2_r0.as_int);

    delay(MBUS_DELAY);

    // Go to sleep without timer
	// FIXME
	//operation_sleep_notimer();
}


//***************************************************
// Temperature measurement operation (SNSv7)
//***************************************************
static void operation_temp_run(void){

	if (Tstack_state == TSTK_IDLE){
		#ifdef DEBUG_MBUS_MSG 
			mbus_write_message32(0xBB, 0xFBFB0000);
			delay(MBUS_DELAY*10);
		#endif
		Tstack_state = TSTK_LDO;

		wfi_timeout_flag = 0;

		// Power on radio
		if (radio_tx_option || ((exec_count+1) < TEMP_CYCLE_INIT)){
			radio_power_on();
		}

		snsv7_r18.ADC_LDO_ADC_LDO_ENB = 0x0;
		mbus_remote_register_write(SNS_ADDR,18,snsv7_r18.as_int);

		// Put system to sleep
		set_wakeup_timer(WAKEUP_PERIOD_LDO, 0x1, 0x1);
		operation_sleep_noirqreset();

    }else if (Tstack_state == TSTK_LDO){
		#ifdef DEBUG_MBUS_MSG
			mbus_write_message32(0xBB, 0xFBFB1111);
			delay(MBUS_DELAY*10);
		#endif
		Tstack_state = TSTK_TEMP_RSTRL;
		snsv7_r18.ADC_LDO_ADC_LDO_DLY_ENB = 0x0;
		mbus_remote_register_write(SNS_ADDR,18,snsv7_r18.as_int);
		// Put system to sleep
		set_wakeup_timer(WAKEUP_PERIOD_LDO, 0x1, 0x1);
		operation_sleep_noirqreset();

	}else if (Tstack_state == TSTK_TEMP_RSTRL){
		#ifdef DEBUG_MBUS_MSG
			mbus_write_message32(0xBB, 0xFBFB2222);
			delay(MBUS_DELAY*10);
		#endif
		Tstack_state = TSTK_TEMP_READ;

		// Release Temp Sensor Reset
		temp_sensor_release_reset();
		delay(MBUS_DELAY);
			
		// Start Temp Sensor
		temp_sensor_enable();

		// Put system to sleep
		set_wakeup_timer(20, 0x1, 0x1); // FIXME timeout value should be set
		operation_sleep_noirqreset();

	}else if (Tstack_state == TSTK_TEMP_START){
	// Start temp measurement
	#ifdef DEBUG_MBUS_MSG
		mbus_write_message32(0xBB, 0xFBFB3333);
		delay(MBUS_DELAY*10);
	#endif

		mbus_msg_flag = 0;

		// Start Temp Sensor
		temp_sensor_enable();

		// Use Timer32 as timeout counter
		config_timer32(0x249F0, 1, 0, 0); // 1/10 of MBUS watchdog timer default

		// Wait for temp sensor output
		WFI();

		// Turn off Timer32
		*TIMER32_GO = 0;
		Tstack_state = TSTK_TEMP_READ;

	}else if (Tstack_state == TSTK_TEMP_READ){
		#ifdef DEBUG_MBUS_MSG
			mbus_write_message32(0xBB, 0xFBFB4444);
			delay(MBUS_DELAY*10);
		#endif

		// Grab Temp Sensor Data
		if (wfi_timeout_flag){
			mbus_write_message32(0xFA, 0xFAFAFAFA);
		}else{
			read_data_reg11 = *((volatile uint32_t *) 0xA0000000);
		}
		meas_count++;

		// Last measurement from this wakeup
		if (meas_count == NUM_TEMP_MEAS){
			// No error; see if there was a timeout
			if (wfi_timeout_flag){
				temp_storage_latest = 0x666;
				wfi_timeout_flag = 0;
			}else{
				temp_storage_latest = read_data_reg11;

				// Record temp difference from last wakeup adjustment
				if (temp_storage_latest > temp_storage_last_wakeup_adjust){
					temp_storage_diff = temp_storage_latest - temp_storage_last_wakeup_adjust;
				}else{
					temp_storage_diff = temp_storage_last_wakeup_adjust - temp_storage_latest;
				}
				#ifdef DEBUG_MBUS_MSG_1
					mbus_write_message32(0xEA, temp_storage_diff);
					delay(MBUS_DELAY);
				#endif
				
				// FIXME: for now, do this every time					
				//measure_wakeup_period();
				
				if ((temp_storage_diff > 10) || (exec_count < 2)){
					measure_wakeup_period();
					temp_storage_last_wakeup_adjust = temp_storage_latest;
				}
				
			}
		}

		// Option to take multiple measurements per wakeup
		if (meas_count < NUM_TEMP_MEAS){	
			// Repeat measurement while awake
			temp_sensor_disable();
			Tstack_state = TSTK_TEMP_START;
				
		}else{
			meas_count = 0;

			// Assert temp sensor isolation & turn off temp sensor power
			temp_power_off();
			Tstack_state = TSTK_IDLE;

			#ifdef DEBUG_MBUS_MSG_1
				mbus_write_message32(0xCC, exec_count);
				delay(MBUS_DELAY);
				mbus_write_message32(0xC0, temp_storage_latest);
				delay(MBUS_DELAY);
			#endif

			exec_count++;
			// Store results in memory; unless buffer is full
			if (temp_storage_count < TEMP_STORAGE_SIZE){
				temp_storage[temp_storage_count] = temp_storage_latest;
				radio_tx_count = temp_storage_count;
				temp_storage_count++;
			}

			// Optionally transmit the data
			if (radio_tx_option){
				send_radio_data_ppm(0, temp_storage_latest);
			}

			// Enter long sleep
			if(exec_count < TEMP_CYCLE_INIT){
				// Send some signal
				delay(RADIO_PACKET_DELAY);
				send_radio_data_ppm(1, 0xFAF000);
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);

			}else{	
				set_wakeup_timer(WAKEUP_PERIOD_CONT, 0x1, 0x1);
			}

			// Make sure Radio is off
			if (radio_on){
				radio_ready = 0;
				radio_power_off();
			}

			if (temp_run_single){
				temp_run_single = 0;
				temp_running = 0;
				operation_sleep_notimer();
			}

			if ((set_temp_exec_count != 0) && (exec_count > (50<<set_temp_exec_count))){
				// No more measurement required
				// Make sure temp sensor is off
				temp_running = 0;
				operation_sleep_notimer();
			}else{
				operation_sleep_noirqreset();
			}

		}

    }else{
        //default:  // THIS SHOULD NOT HAPPEN
		// Reset Temp Sensor 
		temp_sensor_assert_reset();
		temp_power_off();
		operation_sleep_notimer();
    }

}


static void operation_goc_trigger_init(void){

	// This is critical
	set_halt_until_mbus_tx();
	mbus_write_message32(0xAA,0xABCD1234);
	mbus_write_message32(0xAA,wakeup_data);

	// Initialize variables & registers
	temp_running = 0;
	Tstack_state = TSTK_IDLE;
	
	radio_power_off();
	ldo_power_off();
	temp_power_off();
}

//********************************************************************
// MAIN function starts here             
//********************************************************************

int main() {

    // Reset Wakeup Timer; This is required for PRCv13
    //set_wakeup_timer(200, 0, 1);

    // Initialize Interrupts
    // Only enable register-related interrupts
	enable_reg_irq();
  
    // Config watchdog timer to about 10 sec; default: 0x02FFFFFF
    //config_timerwd(TIMERWD_VAL);
	// FIXME
	disable_timerwd();

	// Disable Mbus watchdog
	// FIXME
	*((volatile uint32_t *) MBCWD_RESET) = 1;

    // Initialization sequence
    if (enumerated != 0xDEADBEEF){
        // Set up PMU/GOC register in PRC layer (every time)
        // Enumeration & RAD/SNS layer register configuration
        operation_init();
    }

	//***************************************************
	// For continuous mode

    mrrv3_r03.MRR_DCP_S_OW = 1;  //TX_Decap S
    mbus_remote_register_write(MRR_ADDR,3,mrrv3_r03.as_int);
    delay(MBUS_DELAY*100);


    mrrv3_r00.MRR_CL_EN = 1;  //Enable CL
    mrrv3_r00.MRR_CL_CTRL = 0x01;//0x01 ; //Set CL
    mbus_remote_register_write(MRR_ADDR,0x00,mrrv3_r00.as_int);
    delay(MBUS_DELAY*100);

    mrrv3_r02.MRR_TX_EN_OW = 1;  //Continuous TX
    mbus_remote_register_write(MRR_ADDR,2,mrrv3_r02.as_int);
    delay(MBUS_DELAY*100);

	while(1);


	//***************************************************
	// FIXME
	// Prepare radio TX
	radio_power_on();

	delay(DELAY_1);
	delay(DELAY_1);
	delay(DELAY_1);
	delay(DELAY_1);

	while(1){
		send_radio_data_ppm(0,0xABC000+exec_count_irq);	
		delay(DELAY_1);
	}

	//***************************************************




    // Check if wakeup is due to GOC interrupt  
    // 0x78 is reserved for GOC-triggered wakeup (Named IRQ14VEC)
    // 8 MSB bits of the wakeup data are used for function ID
    wakeup_data = *((volatile uint32_t *) IRQ14VEC);
    uint32_t wakeup_data_header = (wakeup_data>>24) & 0xFF;
    uint32_t wakeup_data_field_0 = wakeup_data & 0xFF;
    uint32_t wakeup_data_field_1 = wakeup_data>>8 & 0xFF;
    uint32_t wakeup_data_field_2 = wakeup_data>>16 & 0xFF;

	// In case GOC triggered in the middle of routines
	if ((wakeup_data_header != 0) && (exec_count_irq == 0)){
		operation_goc_trigger_init();
	}

    if(wakeup_data_header == 1){
        // Debug mode: Transmit something via radio and go to sleep w/o timer
        // wakeup_data[7:0] is the # of transmissions
        // wakeup_data[15:8] is the user-specified period
        // wakeup_data[23:16] is the MSB of # of transmissions
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;

        if (exec_count_irq < (wakeup_data_field_0 + (wakeup_data_field_2<<8))){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				// radio
				send_radio_data_ppm(0,0xABC000+exec_count_irq);	
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            // radio
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }

    }else if(wakeup_data_header == 2){
		// Slow down PMU sleep osc and run temp sensor code with desired wakeup period
        // wakeup_data[15:0] is the user-specified period
        // wakeup_data[19:16] is the initial user-specified period
        // wakeup_data[20] enables radio tx for each measurement
        // wakeup_data[23:21] specifies how many temp sensor executes; 0: unlimited, n: 50*2^n
    	WAKEUP_PERIOD_CONT_USER = (wakeup_data_field_0 + (wakeup_data_field_1<<8));
        WAKEUP_PERIOD_CONT_INIT = (wakeup_data_field_2 & 0xF);
        radio_tx_option = wakeup_data_field_2 & 0x10;

		temp_run_single = 0;
        //set_pmu_sleep_clk_low();

		if (!temp_running){
			// Go to sleep for initial settling of temp sensing // FIXME
			set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
			temp_running = 1;
			set_temp_exec_count = wakeup_data_field_2 >> 5;
            exec_count_irq++;
			operation_sleep_noirqreset();
		}
		exec_count = 0;
		meas_count = 0;
		temp_storage_count = 0;
		radio_tx_count = 0;

		// Reset IRQ14VEC
		*((volatile uint32_t *) IRQ14VEC) = 0;
        exec_count_irq = 0;

		// Run Temp Sensor Program
		wfi_timeout_flag = 0;
		operation_temp_run();

    }else if(wakeup_data_header == 3){
		// Stop temp sensor program and transmit the battery reading and execution count (alternating n times)
        // wakeup_data[7:0] is the # of transmissions
        // wakeup_data[15:8] is the user-specified period 
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;

		temp_running = 0;
		Tstack_state = TSTK_IDLE;


        if (exec_count_irq < wakeup_data_field_0){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Grab latest PMU ADC readings
				// PMUv2 register read is handled differently
				mbus_remote_register_write(PMU_ADDR,0x00,0x03);
				delay(MBUS_DELAY);
				delay(MBUS_DELAY);
				read_data_batadc = *((volatile uint32_t *) REG0) & 0xFF;
		
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				if (exec_count_irq & 0x1){
					// radio
					send_radio_data_ppm(0,0xBBB000+read_data_batadc);	
				}else{
					// radio
					send_radio_data_ppm(0,0xC00000+exec_count);	
				}
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            // radio
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }


    }else if(wakeup_data_header == 4){
        // Transmit the stored temp sensor data
        // wakeup_data[7:0] is the # of data to transmit; if zero, all stored data is sent
        // wakeup_data[15:8] is the user-specified period 
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;

		radio_tx_numdata = wakeup_data_field_0;
		// Make sure the requested numdata makes sense
		if (radio_tx_numdata >= temp_storage_count){
			radio_tx_numdata = 0;
		}
		
        if (exec_count_irq < 3){
			exec_count_irq++;
			if (exec_count_irq == 1){
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				send_radio_data_ppm(0, 0xABC000+exec_count_irq);
				if (exec_count_irq == 3){
					// set timer
					set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				}else{
					// set timer
					set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				}
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
		}else{
			operation_tx_stored();
		}
		
    }else if(wakeup_data_header == 7){
		// Transmit PMU's ADC reading as a battery voltage indicator
		// wakeup_data[7:0] is the # of transmissions
		// wakeup_data[15:8] is the user-specified period 
		WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;

        if (exec_count_irq < wakeup_data_field_0){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Grab latest PMU ADC readings
				// PMUv2 register read is handled differently
				mbus_remote_register_write(PMU_ADDR,0x00,0x03);
				delay(MBUS_DELAY);
				delay(MBUS_DELAY);
				read_data_batadc = *((volatile uint32_t *) REG0) & 0xFF;
		
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				// radio
				send_radio_data_ppm(0,0xBBB000+read_data_batadc);	
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            // radio
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }

    }else if(wakeup_data_header == 8){
		// Discharge battery by staying active and TX radio
		// wakeup_data[15:0] is the # of transmissions
		// wakeup_data[16] resets PMU solar clamp

		exec_count_irq++;
		if (exec_count_irq == 1){
			// Prepare radio TX
			radio_power_on();
			// Go to sleep for SCRO stabilitzation
			set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
			operation_sleep_noirqreset();
		}else{
			// radio
			uint32_t discharge_count = 0;
			while (discharge_count < (wakeup_data_field_0 + (wakeup_data_field_1<<8))){
				send_radio_data_ppm(0,0xBB0000+discharge_count);
				discharge_count++;
				delay(RADIO_PACKET_DELAY*2);
				// Prevent watchdog kicking in
    			config_timerwd(TIMERWD_VAL);
				delay(RADIO_PACKET_DELAY*2);
				
			}
		}

        if (wakeup_data_field_2 & 0x1){
			// Reset PMU solar clamp
			reset_pmu_solar_short();
		}

		// Finalize
		exec_count_irq = 0;
		// radio
		send_radio_data_ppm(1,0xFAF000);	
		// Go to sleep without timer
		operation_sleep_notimer();


    }else if(wakeup_data_header == 0x14){
		// Run temp sensor once to update room temperature reference
        radio_tx_option = 1;
		temp_run_single = 1;
		temp_running = 1;

		exec_count = 0;
		meas_count = 0;
		temp_storage_count = 0;
		radio_tx_count = 0;

		// Reset IRQ14VEC
		*((volatile uint32_t *) IRQ14VEC) = 0;
        exec_count_irq = 0;

		// Run Temp Sensor Program
		wfi_timeout_flag = 0;
		operation_temp_run();

    }else if(wakeup_data_header == 0x15){
		// Transmit wakeup period as counted by (roughly) CPU clock
		// wakeup_data[7:0] is the # of transmissions
		// wakeup_data[15:8] is the user-specified period 
		WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;

        if (exec_count_irq < wakeup_data_field_0){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Measure wakeup period
				measure_wakeup_period();

				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				// radio
				send_radio_data_ppm(0,0xC00000+wakeup_period_count);	
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            // radio
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }

    }


    // Proceed to continuous mode
    while(1){
        operation_temp_run();
    }

    while(1);
}


