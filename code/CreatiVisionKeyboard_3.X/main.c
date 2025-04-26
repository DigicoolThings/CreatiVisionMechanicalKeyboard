/*
 * CreatiVision Controller PS/2 Keyboard 
 * -------------------------------------
 * 
 * This work is licensed under GNU General Public License v3.0
 * 
 * Version: 3.0
 * 
 * Author:  Greg@DigicoolThings.com            
 * Date: March 2025
 * 
 * PS/2 Keyboard for the CreatiVision Controller (or other PS/2 Hosts).
 * 
 * NOTE: Requires PCB v3.0 or higher (v1.x or v2.x PCBs require modification).
 * 
 * Written for 28 pin AVR EA series (DA should work also). 
 * e.g. AVR32EA28, AVR64EA128, AVR128EA28
 * 
 * Implements PS/2 Keyboard bidirectional interface.
 * 
 * Code was developed in MPLAB X v6.20
 * MCC is used to generate initialize code for AVR device settings, as below.
 *  - Clock Control Pre-scaler Disabled (20MHz Internal Clock)
 *  - Reset Pin (PF6) configured as "RESET pin"
 *  - UPDI Pin (PF7) set to "UPDI"
 *  - Global Interrupt Enabled
 *  - PA0 - PA7, PD0 - PD7, PC0 - PC3, PF0 - PF1 GPIO defined as Inputs,
 *      with Pull-ups enabled. 
 *  - Timer TCA0 driver added
 *      - System Clock (20MHz)
 *          16 bit Timer,
 *          Requested Timeout 40us.
 *      - Enable Overflow Interrupt
 * 
 * Change Log
 * ----------
 * v3.0 - Initial release.
 *    
 */
#include "mcc_generated_files/system/system.h"
#include "util/atomic.h"
#include "util/delay.h"

const struct TMR_INTERFACE *Timer = &TCA0_Interface;

/*
 * Our Keyboard matrix is 8x8 using PORTA (Rows) & PORTD (Columns)
 */
#define MatrixRows 8
#define MatrixCols 8

/*
 * DebounceCount is how many Keyboard Scans for a de-bounce interval.
 */
#define DebounceCount 20

/*
 * DataToClockDelay defines delay in microseconds between Data transition
 * (or sampling), and clock edge transition.
 */
#define DataToClockDelay 10

/*
 * Scan Code to be sent prior to key Scan Code on key release.
 */
#define ReleaseScanCode 0xF0

/*
 * Scan Code to be sent prior to Extended key Scan Code.
 */
#define ExtendedScanCode 0xE0

/*
 * PS/2 Scan Codes based on keyboard key matrix Row / Column.
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
 * PS/2 Keyboard ScanCode Transmission Buffer
 * Rotating buffer containing the ScanCodes to send.
 */
#define PS2_ScanCodeBuffer_Size 128
static volatile uint8_t PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_Size];
static volatile uint8_t PS2_ScanCodeBuffer_Start = 0;
static volatile uint8_t PS2_ScanCodeBuffer_End   = 0;

/*
 * PS/2 Host Command Receive Buffer
 * Rotating buffer containing the Host Commands / Data received.
 */
#define PS2_CommandBuffer_Size 128
static volatile uint8_t PS2_CommandBuffer[PS2_CommandBuffer_Size];
static volatile uint8_t PS2_CommandBuffer_Start = 0;
static volatile uint8_t PS2_CommandBuffer_End   = 0;

/*
 * PS/2 PORT PIN Bit Mask (bm) Definitions (PORTF is currently used)
 */
static const uint8_t PS2_Clock_bm  = PIN0_bm;
static const uint8_t PS2_Data_bm = PIN1_bm;

/*
 * Function to Add a code to send, to the scanCodeBuffer
 */
static void scanCodeBufferAdd(uint8_t addCode) 
{
    ATOMIC_BLOCK(ATOMIC_FORCEON) 
    {
        PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_End] = addCode;

        if (++PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Size)
            PS2_ScanCodeBuffer_End = 0;

        /* If buffer is now full, drop oldest value */
        if ((PS2_ScanCodeBuffer_End == PS2_ScanCodeBuffer_Start)
            && (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size))
                PS2_ScanCodeBuffer_Start = 0;
    }    
}

/*
 * Function to Scan our Keyboard matrix
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
                        if ((PS2_KeyScanCode[r][c] == 0x6B) || (PS2_KeyScanCode[r][c] == 0x74))
                        {
                            /* It's an extended Scan Code */
                            scanCodeBufferAdd(ExtendedScanCode);
                        }    
                        if (keySwitchStatus)
                        {
                            /* Key was released so first send Release Scan Code */
                            scanCodeBufferAdd(ReleaseScanCode);
                        }
                        /* Send Key Scan Code */
                        scanCodeBufferAdd(PS2_KeyScanCode[r][c]);
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
 * Function to Process a received Command / Data byte
 */
static void processCommand(void) 
{
    uint8_t commandCode;

    /* See if there is a command (or data) in the CommandBuffer */ 
    if (PS2_CommandBuffer_Start != PS2_CommandBuffer_End)
    {  /* There is a command received! */
        commandCode = PS2_CommandBuffer[PS2_CommandBuffer_Start];

        /* Remove the retrieved command from the buffer! */
        if (++PS2_CommandBuffer_Start == PS2_CommandBuffer_Size)
            PS2_CommandBuffer_Start = 0;

        /* Process the command! */
        switch (commandCode)
        {
            /* Send appropriate responses to the relevant commands. */
           case 0xFF: /* Reset and self-test */
                /* First send Acknowledge to Host 0xFA */
                scanCodeBufferAdd(0xFA);

                /* Send Self-test is successfully passed to Host 0xAA */
                scanCodeBufferAdd(0xAA);
                break;

            case 0xF2: /* Identify (Request Device ID) */
                /* First send Acknowledge to Host 0xFA */
                scanCodeBufferAdd(0xFA);

                /* Then send Device ID for Keyboards to Host 0xAB83 */
                    /* Send 0xAB */
                scanCodeBufferAdd(0xAB);
                    /* Send 0x83 */
                scanCodeBufferAdd(0x83);
                break;

            /* Just Acknowledge any other valid command or data byte received! */
            default:  
                /* Send Acknowledge only to Host 0xFA */
                scanCodeBufferAdd(0xFA);
                
            /* NOTE: We are not specifically dealing with commands like
             *  "Set LEDs" (0xED) and the following Data byte(s), as we have
             *  no LEDs to set!
             * Therefore, we are just acknowledging these types of commands
             *  (and data bytes).  
             * In the future, we could handle these by retaining a "last command"
             * variable, to enable appropriately handling of following Data bytes.
             */ 
        }
    }
}

/*
 * Timer Interrupt - INTERRUPT SERVICE ROUTINE!
 * Interrupt is called every half PS/2 clock cycle, for creating PS/2 communications
 */
void TCA0_OverflowInterrupt(void) 
{
    static uint8_t clock = 1;       /* send clock high = 1, send clock low = 0 */
  	static uint8_t clockCount = 0;  /* count of clock cycles */
    static uint8_t parityCount = 0; /* parity bit calculation */
    static uint8_t scanCode = 0;    /* scanCode being sent, command being received */
    static uint8_t sendMode = 1;    /* Keyboard send = 1, Keyboard receive = 0 */
    static uint8_t clockInput = 0;  /* sampled PS/2 Clock line high = 1, low = 0 */
    static uint8_t dataInput = 0;   /* sampled PS/2 Data line high = 1, low = 0 */

    /* sample the actual PS/2 clock and data lines */
    clockInput = (PORTF.IN & PS2_Clock_bm) ? 1 : 0; 
    dataInput = (PORTF.IN & PS2_Data_bm) ? 1 : 0; 
    
    /* perform action based on which clock cycle during a send or receive */
    switch (clockCount) 
    {   /* clockCount 0 means we aren't sending or receiving a byte yet */
        case 0: /* ensure idle state before sending */
            if (clockInput)
            { /* PS/2 clockInput is high (not Inhibiting bus!) */
                /* set sendMode based on Host PS/2 Data line state 
                   if dataInput is low then it's a Host RQS else we can send */
                sendMode = dataInput;

                /* Increment Clock count */
                clockCount++; /* proceed if bus is idle or Host RQS */
            } else 
            { /* PS/2 clockInput was low (from Host), so it is Inhibiting bus */
                /* set sendMode based on Host PS/2 Data line state 
                   if dataInput is low then it's a Host RQS else we can send (when not Inhibited). */
                /* Note: Host RQS shouldn't really happen here, unless due to Host signal transition timing */  
                sendMode = dataInput;
            }
            break;
        
        case 1: /* start communication countCount. */
            if ((clock) && (clockInput))
            { /* Clock & PS/2 Clock line is high (idle state) */   
                if (sendMode)
                {   /* we're in send mode, so see if we have a scanCode to send */ 
                    if (PS2_ScanCodeBuffer_Start != PS2_ScanCodeBuffer_End)
                    {  /* There is a scan code to send! */
                        scanCode = PS2_ScanCodeBuffer[PS2_ScanCodeBuffer_Start];
                        parityCount = 0;

                        /* Output the start bit (low) */
                        PORTF.DIRSET = PS2_Data_bm;

                        
                        _delay_us(DataToClockDelay);
                        /* issue Clock falling edge */
                        clock = 0;
                        PORTF.DIRSET = PS2_Clock_bm;
                    } else
                    { /* No character to send, so just reset clockCount to try again! */
                        clockCount = 0;
                    }
                } else
                {   /* We're in Host receive mode! */
                    /* initialize (zero) the command byte and parity calculation. */
                    scanCode = 0;
                    parityCount = 0;

                    _delay_us(DataToClockDelay);
                    /* Issue Clock falling edge */
                    clock = 0;
                    PORTF.DIRSET = PS2_Clock_bm;
                }    
            } else
            {   /* clock or PS/2 clockInput is low */
                /* clock is high but PS/2 clockInput is low */
                if ((clock) && (clockInput == 0))
                { /* PS/2 Host inhibit sending interrupt, so abort send! */
                    /* Release Data line (high) */
                    PORTF.DIRCLR = PS2_Data_bm;
                    /* Reset clockCount for Host RTS check */
                    clockCount = 0;
                } else
                {  /* clock is low */
                    _delay_us(DataToClockDelay);
                    /* Issue Clock rising edge */
                    clock = 1;
                    PORTF.DIRCLR = PS2_Clock_bm;

                    /* Increment Clock count */
                    clockCount++;
                }
            }
            break;
        
        case 2 ... 9: /* data bits */
            if ((clock) && (clockInput))
            { /* clock & PS/2 clockInput is high so set or read data bit */   
                if (sendMode)
                {  /* if sendMode set data bit */   
                    if ((scanCode & 0x01) == 1)
                    {
                        PORTF.DIRCLR = PS2_Data_bm;
                        parityCount++;
                    } else PORTF.DIRSET = PS2_Data_bm;
                    /* Shift so ready for next data bit */
                    scanCode >>= 1;
                } else
                {  /* Host Receive mode so read bit */
                    dataInput = (PORTF.IN & PS2_Data_bm) ? 0x80 : 0; 
                    if (dataInput > 0) parityCount++;
                    scanCode |= dataInput;
                    if (clockCount < 9) scanCode >>= 1;
                }
                _delay_us(DataToClockDelay);
                /* Issue Clock falling edge */
                clock = 0;
                PORTF.DIRSET = PS2_Clock_bm;
            } else
            {  /* clock is high but PS/2 clockInput low */
                if ((clock) && (clockInput == 0))
                { /* PS/2 Host inhibit sending interrupt, so abort send! */
                    /* Release Data line (high) */
                    PORTF.DIRCLR = PS2_Data_bm;
                    /* Reset clockCount for Bus inhibit / Host RTS check */
                    clockCount = 0;
                } else
                {  /* Clock is low */
                    _delay_us(DataToClockDelay);
                    /* Issue Clock rising edge */
                    clock = 1;
                    PORTF.DIRCLR = PS2_Clock_bm;

                    /* Increment Clock count */
                    clockCount++;
                }
            }    
            break;
            
        case 10: /* send parity bit / receive host receive parity bit */
            if ((clock) && (clockInput))
            { /* Clock & PS/2 Clock line is high so set the odd parity bit */   
                if (sendMode)
                {  /* sendMode so set Parity bit (Odd) */
                    if (parityCount % 2) PORTF.DIRSET = PS2_Data_bm;
                    else PORTF.DIRCLR = PS2_Data_bm;
                } else    
                {  /* Host Receive mode read parity bit */
                    dataInput = (PORTF.IN & PS2_Data_bm) ? 1 : 0; 
                    if ((parityCount % 2) != dataInput)
                    { /* Valid Parity received */

                        /* We just received a Host Command, so clear ScanCode Buffer */
                        PS2_ScanCodeBuffer_Start = 0;
                        PS2_ScanCodeBuffer_End   = 0;

                        PS2_CommandBuffer[PS2_CommandBuffer_End] = scanCode;

                        if (++PS2_CommandBuffer_End == PS2_CommandBuffer_Size)
                            PS2_CommandBuffer_End = 0;

                        /* If buffer is now full, drop oldest value */
                        if ((PS2_CommandBuffer_End == PS2_CommandBuffer_Start)
                            && (++PS2_CommandBuffer_Start == PS2_CommandBuffer_Size))
                                PS2_CommandBuffer_Start = 0;
                        
                    }    
                }    
                
                _delay_us(DataToClockDelay);
                /* Issue Clock falling edge */
                clock = 0;
                PORTF.DIRSET = PS2_Clock_bm;
            } else
            {  /* clock is high but PS/2 clockInput is low */
                if ((clock) && (clockInput == 0))
                { /* PS/2 Host inhibit sending interrupt, so abort send! */
                    /* Release Data line (high) */
                    PORTF.DIRCLR = PS2_Data_bm;
                    /* Reset clockCount for Host RTS check */
                    clockCount = 0;
                } else
                {  /* Clock is low */
                    _delay_us(DataToClockDelay);
                    /* Issue Clock rising edge */
                    clock = 1;
                    PORTF.DIRCLR = PS2_Clock_bm;

                    /* Increment Clock count */
                    clockCount++;
                }    
            }
            break;

        case 11: /* send stop bit or host receive Acknowledge bit */
            /* Note: We don't check for the Host pulling clock low here
             * as some Host's pull clock low straight after the parity bit,
             * so we just go ahead with sending the expected stop / ack bit  */
            if (clock)
            { /* clock is high (PS/2 clockInput is supposed to be high) */   
                if (sendMode)
                {
                    /* Output stop bit (high) */
                    PORTF.DIRCLR = PS2_Data_bm;
                } else   
                {
                    /* Output Ack bit (low) */
                    PORTF.DIRSET = PS2_Data_bm;
                }
                _delay_us(DataToClockDelay);
                /* Issue Clock falling edge */
                clock = 0;
                PORTF.DIRSET = PS2_Clock_bm;
            } else
            {  /* Clock is low */
                _delay_us(DataToClockDelay);
                /* Issue Clock rising edge */
                clock = 1;
                PORTF.DIRCLR = PS2_Clock_bm;
                /* Release Data line (high) */
                PORTF.DIRCLR = PS2_Data_bm;

                if (sendMode)
                {    
                    /* Now that the ScanCode is sent, remove it from the buffer! */
                    if (++PS2_ScanCodeBuffer_Start == PS2_ScanCodeBuffer_Size)
                        PS2_ScanCodeBuffer_Start = 0;
                }

                /* Increment Clock count */
                clockCount++;
            }    
            break;
            
        /* we've just finished a packet so reset clockCount to start again. */    
        default: 
            clockCount = 0;
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

    /* The following is initialized by MCC, but we also do it here for clarity! */
    /* Initialize PS/2 Port as inputs (PS/2 bus idle state) */ 
    PORTF.DIRCLR = PS2_Clock_bm;
    PORTF.DIRCLR = PS2_Data_bm;
    /* Initialize PS/2 Port output registers bits to low (for pulling bus lines low) */ 
    PORTF.OUTCLR = PS2_Clock_bm;
    PORTF.OUTCLR = PS2_Data_bm;
    
    /* Let's do this forever! */
    while(1)
    {
        scanKeyboard();
        processCommand();
    }    
}
