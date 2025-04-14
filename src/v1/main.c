/*
 * CreatiVision Controller PS/2 Keyboard 
 * -------------------------------------
 * 
 * This work is licensed under GNU General Public License v3.0
 * 
 * Version: 1.0
 * 
 * Author:  Greg@DigicoolThings.com            
 * Date: October 2024
 * 
 * Controller PS/2 Keyboard for the CreatiVision Controller.
 * 
 * Written for 28 pin AVR EA series. 
 * e.g. AVR32EA28, AVR64EA128, AVR128EA28
 * 
 * Implements PS/2 Keyboard output
 * 
 * Code was developed in MPLAB X v6.20
 * MCC is used to generate code for AVR device settings, as below.
 *  - Clock Control Pre-scaler Disabled (20MHz Internal Clock)
 *  - Reset Pin (PF6) set to "No External Reset"
 *  - UPDI Pin (PF7) set to "UPDI"
 *  - Global Interrupt Enabled
 *  - PA0 - PA7, PD0 - PD7 GPIO defined as Inputs, with Pull-ups enabled. 
 *  - PF0 - PF1 GPIO defined as Outputs, start high, and inverted outputs.
 *  - Timer TCA0 driver added
 *      - System Clock (20MHz), 16 bit Timer, Requested Timeout 35us.
 *      - Enable Overflow Interrupt
 * 
 * N.B. Inverted Outputs on PF0 - PF1 is to allow for MOSFET inversion of lines.
 * 
 * Change Log
 * ----------
 * v1.0 - Initial release.
 *    
 */
#include "mcc_generated_files/system/system.h"
#include "util/atomic.h"
#include "util/delay.h"

const struct TMR_INTERFACE *Timer = &TCA0_Interface;

/*
 * Our Keyboard matrix is 8x8 using PORTA (Rows) & PORTD (Cols)
 */
#define MatrixRows 8
#define MatrixCols 8

/*
 * Scan Code to be sent prior to key Scan Code on key release.
 */
#define ReleaseScanCode 0xF0

/*
 * Scan Code to be sent prior to Extended key Scan Code.
 */
#define ExtendedScanCode 0xE0

/*
 * DebounceCount is how many Keyboard Scans for a de-bounce interval.
 */
#define DebounceCount 0x14

/*
 * IntervalCount is how many Clock cycles for each PS/2 transmission period.
 */
#define IntervalCount 0x28

/*
 * PS/2 Scan Codes based on key matrix Row / Column.
 * Rows are PORTA, Columns are PORTD 
 */
static const uint8_t PS2_KeyScanCode[MatrixRows][MatrixCols] 
                        = {{0x16,0x1E,0x26,0x25,0x2E,0x36,0x00,0x00},
                           {0x00,0x15,0x1D,0x24,0x2D,0x2C,0x14,0x00},
                           {0x6B,0x1C,0x1B,0x23,0x2B,0x34,0x00,0x00},
                           {0x00,0x1A,0x22,0x21,0x2A,0x32,0x00,0x59},
                           {0x3D,0x3E,0x46,0x45,0x52,0x4E,0x00,0x00},
                           {0x35,0x3C,0x43,0x44,0x4D,0x5A,0x00,0x00},
                           {0x33,0x3B,0x42,0x4B,0x4C,0x74,0x00,0x00},
                           {0x31,0x3A,0x41,0x49,0x4A,0x29,0x00,0x00}};

/* 
 * KeyswitchDebounce = the decrementing de-bounce count 
 * KeyswitchReleased = true if key switch is released (open), or false if closed.
 */ 
static uint8_t KeyswitchDebounce[MatrixRows][MatrixCols];
static bool KeyswitchReleased[MatrixRows][MatrixCols];

/*
 * PORT bit mask for each row or column
 */
static const uint8_t RowCol_bm[MatrixRows] 
                        = {PIN0_bm,PIN1_bm,PIN2_bm,PIN3_bm,PIN4_bm,PIN5_bm,PIN6_bm,PIN7_bm};

/*
 * PS/2 Keyboard ScanCode Output Buffer
 * Rotating buffer containing th ScanCodes to send.
 */
#define PS2_ScanCodeBuffer_Size 128
static volatile uint8_t PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_Size];
static volatile uint8_t PS2_ScanCodeBuffer_Start = 0;
static volatile uint8_t PS2_ScanCodeBuffer_End   = 0;

/*
 * PS/2 PORTF  PIN Bit Mask (bm) Definitions
 */
static const uint8_t PS2_Clock_bm  = PIN0_bm;
static const uint8_t PS2_Data_bm = PIN1_bm;

/*
 * Scan our Keyboard matrix
 * De-bounce delay any detected changes, then store Scan Codes in ScanCodeBuffer
 */
static void scanKeyboard(void) 
{
    uint8_t matrix_row;
    bool keySwitchStatus;
    
    for (uint8_t r = 0; r < MatrixRows; r++)
    {
        /* For each Row, read the row columns by row output low */
        PORTA.DIRSET = RowCol_bm[r];
        PORTA.OUTCLR = RowCol_bm[r];
        _delay_us(10);
        /* re-read Joystick to get Button Right & Left (1 is ON) */
        matrix_row = PORTD.IN;
        /* Return to Input for this row */
        PORTA.DIRCLR = RowCol_bm[r];
        
        for (uint8_t c = 0; c < MatrixCols; c++)
        {    
            /* Process each column in the current row */
            if (KeyswitchDebounce[r][c] > 1)
            {  /* We are de-bouncing, so just decrement the de-bounce count */   
                KeyswitchDebounce[r][c]--;
            } else
            {  /* If de-bounce count is down to 1 */  
                keySwitchStatus = (matrix_row & RowCol_bm[c]);
                
                if (KeyswitchDebounce[r][c] == 1)
                {    
                    /* Check switch is still in the same state (and has a Scan Code!) */
                    if ((keySwitchStatus == KeyswitchReleased[r][c])
                        && (PS2_KeyScanCode[r][c] != 0x00))
                    {  /* Same state after de-bouncing, key action confirmed! */
                    	ATOMIC_BLOCK(ATOMIC_FORCEON) 
                        {
                            if ((PS2_KeyScanCode[r][c] == 0x6B) || (PS2_KeyScanCode[r][c] == 0x74))
                            {
                                /* Key was extended Scan Code key, so send Extended Scan Code */
                                PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_End] = ExtendedScanCode;

                                if (++PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Size)
                                    PS2_ScanCodeBuffer_End = 0;

                                /* If buffer is now full, drop oldest value */
                                if ((PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Start)
                                    && (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size))
                                        PS2_ScanCodeBuffer_Start = 0;
                            }    
                            if (keySwitchStatus)
                            {
                                /* Key was released so first send Release Scan Code */
                                PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_End] = ReleaseScanCode;

                                if (++PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Size)
                                    PS2_ScanCodeBuffer_End = 0;

                                /* If buffer is now full, drop oldest value */
                                if ((PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Start)
                                    && (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size))
                                        PS2_ScanCodeBuffer_Start = 0;
                            }
                            /* Send Key Scan Code */
                            PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_End] = PS2_KeyScanCode[r][c];

                            if (++PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Size)
                                PS2_ScanCodeBuffer_End = 0;

                            /* If buffer is now full, drop oldest value */
                            if ((PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Start)
                                && (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size))
                                    PS2_ScanCodeBuffer_Start = 0;
                            
                        }    
                    }
                    KeyswitchDebounce[r][c] = 0;
                } else
                { /* If no de-bounce count check if key state has changed. */
                    if (keySwitchStatus != KeyswitchReleased[r][c])
                    {  /* key state has changed, save key state and start de-bounce count */  
                      KeyswitchReleased[r][c] = keySwitchStatus;
                      KeyswitchDebounce[r][c] = DebounceCount;
                    }
                }    
            }
        }
    }
    
}

/*
 * Timer Interrupt - INTERRUPT SERVICE ROUTINE!
 * Interrupt is called every half clock cycle for creating PS/2 output
 */
void TCA0_OverflowInterrupt(void) 
{
    static uint8_t clock = 1;
  	static uint8_t clockCount = 0;
    static uint8_t parityCount = 0;
    static uint8_t scanCode = 0;

    switch (clockCount) 
    {
        case 0: /* count zero, so if clock is high check if Scan Code to send. */
            if (clock)
            { /* Clock is high */   
                if (PS2_ScanCodeBuffer_Start != PS2_ScanCodeBuffer_End)
                {  /* There is a scan code to send! */
                    scanCode = PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_Start];

                    if (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size)
                        PS2_ScanCodeBuffer_Start = 0;

                    /* Output start bit (low) */
                    PORTF.OUTCLR = PS2_Data_bm;
                    _delay_us(10);
                    
                    /* Issue Clock falling edge */
                    clock = 0;
                    PORTF.OUTCLR = PS2_Clock_bm;


                } /* No character to send, so nothing to do! */   
            } else
            {  /* Clock is low */
                /* Issue Clock rising edge */
                clock = 1;
                PORTF.OUTSET = PS2_Clock_bm;
                /* Increment Clock count */
                clockCount++;
            }
            break;
        
        case 1 ... 8: /* data bits */
            if (clock)
            { /* Clock is high so set this data bit */   
                if (scanCode & 0x01)
                {
                    PORTF.OUTSET = PS2_Data_bm;
                    parityCount++;
                } else PORTF.OUTCLR = PS2_Data_bm;
                _delay_us(10);
                    
                scanCode >>= 1;

                /* Issue Clock falling edge */
                clock = 0;
                PORTF.OUTCLR = PS2_Clock_bm;
            } else
            {  /* Clock is low */
                /* Issue Clock rising edge */
                clock = 1;
                PORTF.OUTSET = PS2_Clock_bm;
                /* Increment Clock count */
                clockCount++;
            }
            break;
            
        case 9: /* Parity bit (odd) */
            if (clock)
            { /* Clock is high so set the odd parity bit */   
                if (parityCount % 2) PORTF.OUTCLR = PS2_Data_bm;
                                else PORTF.OUTSET = PS2_Data_bm;
                _delay_us(10);

                /* Issue Clock falling edge */
                clock = 0;
                PORTF.OUTCLR = PS2_Clock_bm;
            } else
            {  /* Clock is low */
                /* Issue Clock rising edge */
                clock = 1;
                PORTF.OUTSET = PS2_Clock_bm;
                /* Increment Clock count */
                clockCount++;
            }
            break;

        case 10:
            if (clock)
            { /* Clock is high so set the odd parity bit */   
                /* Output stop bit (high) */
                PORTF.OUTSET = PS2_Data_bm;
                _delay_us(10);

                /* Issue Clock falling edge */
                clock = 0;
                PORTF.OUTCLR = PS2_Clock_bm;
            } else
            {  /* Clock is low */
                /* Issue Clock rising edge */
                clock = 1;
                PORTF.OUTSET = PS2_Clock_bm;
                /* Increment Clock count */
                clockCount++;
            }
            break;

        default: clockCount++;
            
    }
	if (clockCount > IntervalCount) 
    {
		clockCount = 0;
        parityCount = 0;
        scanCode = 0;
    }
}

/*
 * Main Application
 */
int main(void)
{
    /* MCC defined System Setup (initialize) */
    SYSTEM_Initialize();

    /* Setup Timer Interrupt Handler routine */
    Timer->TimeoutCallbackRegister(TCA0_OverflowInterrupt);
   
    /* Initialize key switch arrays to switches Off / Zero de-bounce count */    
    for (uint8_t r = 0; r < MatrixRows; r++)
        for (uint8_t c = 0; c < MatrixCols; c++)
        {    
            KeyswitchDebounce[r][c] = 0;
            KeyswitchReleased[r][c] = true;
        }
    
    /* Let's do this forever! */
    while(1)
    {

        scanKeyboard();
        
    }    
}