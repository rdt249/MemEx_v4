#include <msp430.h>
#include <string.h>
#include <stdlib.h>

/***************************** SETTINGS *****************************/

#define SLOTS   12  // max number of active devices

#define BAUD_RATE   1000000 // options: 9600, 115200, 230400, 460800, 1000000, 2000000
#define UART_BUFFER 64 // max length for incoming serial messages

/******************************** VERSION **********************************/

char *version = "4.1";
char *updated = "2021-11-11";

/* changes :
 *
 * changed sizes from bytes to kilobytes
 * enabled variable BAUD_RATE at compile time
 * changed status to update "awake" array instead of direct UART output
 * array functions now look at "awake" to skip unresponsive memories (saves time)
 * fixed bug that truncated memory size settings
 * sped up FILL by writing to all memories at once
 * added ADC function to read VRAM channels
 */

/*************************** GLOBAL VARIABLES ****************************/

char patterns[SLOTS] = {85,85,85,
                        85,85,85,
                        85,85,85,
                        85,85,85};
unsigned int sizes[SLOTS] = {1,1,1,
                             1,1,1,
                             1,1,1,
                             1,1,1};
unsigned char awake[SLOTS] = {0,0,0,0,0,0,0,0,0,0,0,0};
char uart_rx_buffer[UART_BUFFER];
unsigned int uart_rx_counter = 0;

/************************** LOOKUP TABLES *****************************/

// lookup table to speed up counting mismatches
char mismatches[256];
void lut_init()
{
    unsigned int i;
    unsigned char b;
    for(i = 0; i < 256; i++) // for each value (0 ~ 255)
    {
        char count = 0;
        for(b = 0; b < 8; b++) // for each bit
        {
            count += (i >> b) & 0x01;
        }
        mismatches[i] = count;
    }
}

// lookup table to speed up UART transmission of numbers
char *chars[256] = {"0","1","2","3","4","5","6","7","8","9","10","11","12","13","14",
                   "15","16","17","18","19","20","21","22","23","24","25","26","27",
                   "28","29","30","31","32","33","34","35","36","37","38","39","40",
                   "41","42","43","44","45","46","47","48","49","50","51","52","53",
                   "54","55","56","57","58","59","60","61","62","63","64","65","66",
                   "67","68","69","70","71","72","73","74","75","76","77","78","79",
                   "80","81","82","83","84","85","86","87","88","89","90","91","92",
                   "93","94","95","96","97","98","99","100","101","102","103","104",
                   "105","106","107","108","109","110","111","112","113","114","115",
                   "116","117","118","119","120","121","122","123","124","125","126",
                   "127","128","129","130","131","132","133","134","135","136","137",
                   "138","139","140","141","142","143","144","145","146","147","148",
                   "149","150","151","152","153","154","155","156","157","158","159",
                   "160","161","162","163","164","165","166","167","168","169","170",
                   "171","172","173","174","175","176","177","178","179","180","181",
                   "182","183","184","185","186","187","188","189","190","191","192",
                   "193","194","195","196","197","198","199","200","201","202","203",
                   "204","205","206","207","208","209","210","211","212","213","214",
                   "215","216","217","218","219","220","221","222","223","224","225",
                   "226","227","228","229","230","231","232","233","234","235","236",
                   "237","238","239","240","241","242","243","244","245","246","247",
                   "248","249","250","251","252","253","254","255"};

/******************************** CLOCKS *******************************/

void clk_init(void){
    FRCTL0 = FRCTLPW | NWAITS_1; // configure FRAM wait states for 16MHz
    CSCTL0_H = CSKEY >> 8;                    // Unlock CS registers
    CSCTL1 = DCOFSEL_4 | DCORSEL;            // Set DCO to 16MHz
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK; // SMCLK=MCLK=DCO, ACLK=VLO
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;     // Set all dividers
    CSCTL0_H = 0;                             // Lock CS registers
}

/******************************** GPIO *******************************/

void gpio_init(void)
{
    P2SEL0 |= BIT0 | BIT1;                    // USCI_A0 UART operation
    P2SEL1 &= ~(BIT0 | BIT1);

    P9SEL0 |= 0x0f; // ADC12 operation (A8 ~ A11)
    P9SEL1 |= 0x0f;

    P1DIR = 0;   // DATA IO pins
    P1REN = 0;

    P3DIR = 0xff;   // ADDRESS pins
    P4DIR = 0xff;
    P5DIR = 0xff;
    P3OUT = 0;
    P4OUT = 0;
    P5OUT = 0;

    P6DIR |= BIT0 | BIT1;   // WE and OE pins
    P6OUT |= BIT0 | BIT1;

    P7DIR = 0xff;   // CE pins
    P8DIR |= 0x07;
    P7OUT = 0xff;
    P8OUT |= 0x07;

    PM5CTL0 &= ~LOCKLPM5;                     // unlock control register
}

/***************************** ADC *******************************/

void adc_init(void)
{
    ADC12CTL0 = ADC12SHT0_2 | ADC12MSC | ADC12ON;   // 16-cycle sample timer, consecutive conversions
    ADC12CTL1 = ADC12SHP | ADC12CONSEQ_1;           // using sampling timer, sequence mode
    ADC12MCTL0 |= ADC12INCH_8;                      // MEM0 from channel A8
    ADC12MCTL1 |= ADC12INCH_9;                      // MEM1 from channel A9
    ADC12MCTL2 |= ADC12INCH_10;                     // MEM2 from channel A10
    ADC12MCTL3 |= ADC12INCH_11 | ADC12EOS;          // MEM3 from channel A11, end of sequence
    ADC12IER0 = ADC12IE3;                           // enable interrupts for MEM3
    ADC12CTL0 |= ADC12ENC;                          // enable conversion
}

/***************************** MEMORY ACCESS *******************************/

void set_addr(long addr)
{
    // break address into 3 bytes and set outputs
    P3OUT = (char) (addr & 0xff);
    P4OUT = (char) ((addr >> 8) & 0xff);
    P5OUT = (char) ((addr >> 16) & 0xff);
}

void set_read_mode(void)
{
    P1OUT = 0xff;   // pull-up resistors
    P1REN = 0xff;   // enable resistors
    P1DIR = 0;      // set IO pins as inputs
    P6OUT |= BIT0;  // raise WE pin
}

void set_write_mode(char data)
{
    P1DIR = 0xff;   // set IO pins as outputs
    P1OUT = data;   // set data
    P6OUT |= BIT1;  // raise OE pin
}

void write_to(char chip)
{
    if(chip < 8){P7OUT = 0xff ^ (0x01 << chip);}    // lower CE pin
    else{P8OUT = 0xff ^ (0x01 << (chip - 8));}
    P6OUT &= ~BIT0; // lower WE pin
    P6OUT |= BIT0;  // raise WE pin
    if(chip < 8){P7OUT = 0xff;} // raise CE pins
    else{P8OUT = 0xff;}
}

char read_from(char chip)
{
    if(chip < 8){P7OUT = 0xff ^ (0x01 << chip);}    // lower CE pin
    else{P8OUT = 0xff ^ (0x01 << (chip - 8));}
    P6OUT &= ~BIT1; // lower OE pin
    char data = P1IN;   // read data (make sure P1 is set as input)
    P6OUT |= BIT1;  // raise OE pin
    if(chip < 8){P7OUT = 0xff;} // raise CE pins
    else{P8OUT = 0xff;}
    return data;
}

/******************************* UART **********************************/

void uart_init(void)
{
    UCA0CTLW0 = UCSWRST;                      // Put eUSCI in reset
    UCA0CTLW0 |= UCSSEL__SMCLK;               // CLK = SMCLK = 16MHz

#if BAUD_RATE == 2000000
    UCA0BR0 = 8;
    UCA0BR1 = 0;
    UCA0MCTLW = 0;
#elif BAUD_RATE == 1000000
    UCA0BR0 = 16;
    UCA0BR1 = 0;
    UCA0MCTLW = 0;
#elif BAUD_RATE == 460800
    UCA0BR0 = 2;
    UCA0BR1 = 0;
    UCA0MCTLW = UCOS16 | UCBRF_2 | (0xbb << 8);
#elif BAUD_RATE == 230400
    UCA0BR0 = 4;
    UCA0BR1 = 0;
    UCA0MCTLW = UCOS16 | UCBRF_5 | (0x55 << 8);
#elif BAUD_RATE == 115200
    UCA0BR0 = 8;
    UCA0BR1 = 0;
    UCA0MCTLW = UCOS16 | UCBRF_10 | (0xf7 << 8);
#elif BAUD_RATE == 9600
    UCA0BR0 = 104;
    UCA0BR1 = 0;
    UCA0MCTLW = UCOS16 | UCBRF_2 | (0xd6 << 8);
#endif

    UCA0CTLW0 &= ~UCSWRST;                    // Initialize eUSCI
    UCA0IE |= UCRXIE;// | UCTXIE;                // Enable USCI_A0 RX interrupt
    //uart_tx_counter = 0;
    uart_rx_counter = 0;
}

void uart_tx(char data)
{         // writes a byte to serial comms
    while(!(UCA0IFG & UCTXIFG));
    UCA0TXBUF = data;
}

void uart_tx_string(char *string)
{   // writes a string to serial comms
    int length = strlen(string);    // find length of string
    while(length>0) // iterate over length of string
    {
        uart_tx(*string);   // tx one letter
        length--;   // decrement length
        *string++;  // increment pointer
    }
}

void uart_tx_number(long number)
{
    int digits = 0;
    if(number == 0){
        uart_tx('0');   // if number = 0, go ahead and send 0
    }
    long reverse = 0;
    while(number > 0){
        reverse = reverse * 10 + (number % 10);
        number /= 10;
        digits++;
    }
    char word_bank[10] = {'0','1','2','3','4','5','6','7','8','9'};
    while(digits > 0){
        uart_tx(word_bank[reverse % 10]);
        reverse /= 10;
        digits--;
    }
}

void uart_tx_char(char number)
{
    uart_tx_string(chars[number]);
}

void uart_clear(void)
{
    unsigned int i;
    for(i = 0; i < UART_BUFFER; i++)
    {
        uart_rx_buffer[i] = 0;
    }
    uart_rx_counter = 0;
}

/*************************** MEMORY FUNCTIONS ******************************/

void status(char chip)
// find status of memory
{
    const char pattern = 85;  // checkerboard pattern
    set_write_mode(pattern);    // get ready to write pattern
    set_addr(0);        // at address 0
    write_to(chip);    // write to first byte
    set_read_mode();    // get ready to read
    set_addr(0);        // at address 0
    char data = read_from(chip);    // read first byte
    if(data == pattern){awake[chip]=1;}  // print 1 if data matches pattern
    else{awake[chip]=0;} // print 0 if not
}

void fill(char chip, unsigned int size, char pattern)
// fill memory with given pattern
{
    unsigned long i;    // declare iterator
    unsigned long bytes = (unsigned long) 1024 * size;
    set_write_mode(pattern);    // get ready to write pattern
    for(i = 0; i < bytes; i++)   // for each byte in size
    {
        set_addr(i);    // set address
        write_to(chip); // write data to address
    }
}

void read(char chip, unsigned int size)
// read out memory to serial stream
{
    unsigned long i;    // declare iterator
    unsigned long bytes = (unsigned long) 1024 * size;
    char data;          // preallocate data
    set_read_mode();    // get ready to read
    for(i = 0; i < bytes; i++)
    {
        set_addr(i);
        data = read_from(chip);
        uart_tx_char(data);
        if(i < bytes - 1){uart_tx(',');}
    }
}

void check(char chip, unsigned int size, char pattern)
// read out mismatched bits onto serial stream
{
    unsigned long i;    // declare iterator
    unsigned long bytes = (unsigned long) 1024 * size;
    char data;          // preallocate data
    set_read_mode();    // get ready to read
    for(i = 0; i < bytes; i++)
    {
        set_addr(i);
        data = read_from(chip);
        uart_tx_char(data ^ pattern);
        if(i < bytes - 1){uart_tx(',');}
    }
}

void scan(char chip, unsigned int size, char pattern)
// print total mismatches bits to serial stream
{
    unsigned long i;    // declare iterator
    unsigned long bytes = (unsigned long) 1024 * size;
    char data;          // preallocate data
    unsigned long count = 0;    // preallocate flips
    set_read_mode();    // get ready to read
    for(i = 0; i < bytes; i++)
    {
        set_addr(i);
        data = read_from(chip);
        count += mismatches[data ^ pattern];
    }
    uart_tx_number(count);
}

/*************************** ARRAY FUNCTIONS ******************************/

void STATUS(void)
{
    uart_tx(':');
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        status(i);
        uart_tx_char(awake[i]);
        if(i < SLOTS - 1){uart_tx(',');}
    }
}

void FILL(unsigned int *size, char *pattern)
{
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        status(i);
        if(awake[i] == 1)
        {
            fill(i,size[i],pattern[i]);
        }
    }
}

void READ(unsigned int *size)
{
    set_read_mode();
    uart_tx_string(":\n");
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        status(i);
        if(awake[i] == 1)
        {
            read(i,size[i]);
        }
        uart_tx_string(";\n");
    }
}

void CHECK(unsigned int *size, char *pattern)
{
    uart_tx_string(":\n");
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        status(i);
        if(awake[i] == 1)
        {
            check(i,size[i],pattern[i]);
            uart_tx_string(";\n");
        }
        else
        {
            uart_tx_string(";\n");
        }
    }
}

void SCAN(unsigned int *size, char *pattern)
{
    uart_tx(':');
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        status(i);
        if(awake[i] == 1)
        {
            scan(i,size[i],pattern[i]);
        }
        if(i < SLOTS - 1){uart_tx(',');}
    }
}

/**************************** CONTROL FUNCTIONS ****************************/

void HOLD(void)
{
    P1DIR = 0xff;   // DATA IO pins
    P1OUT = 0;

    P3DIR = 0xff;   // ADDRESS pins
    P4DIR = 0xff;
    P5DIR = 0xff;
    P3OUT = 0;
    P4OUT = 0;
    P5OUT = 0;

    P6DIR |= BIT0 | BIT1;   // WE and OE pins
    P6OUT |= BIT0 | BIT1;

    P7DIR = 0xff;   // CE pins
    P8DIR |= 0x07;
    P7OUT = 0;
    P8OUT &= ~0x07;
}

void LIFT(void)
{
    gpio_init();
}

void SIZE(void)
{
    uart_tx(':');
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        uart_tx_number(sizes[i]);
        if(i < SLOTS - 1){uart_tx(',');}
    }
    uart_tx_string("\nnew size:");
    uart_clear();
    __bis_SR_register(LPM0_bits | GIE);
    if(strcmp(uart_rx_buffer,"\r\n") != 0)
    {
        char *token;
        for(i = 0; i < SLOTS; i++)
        {
            if(i == 0){token = strtok(uart_rx_buffer,",");}
            else{token = strtok(NULL,",");}
            sizes[i] = atoi(token);
            uart_tx_number(sizes[i]);
            if(i < SLOTS - 1){uart_tx(',');}
        }
    }
    else
    {
        for(i = 0; i < SLOTS; i++)
        {
            uart_tx_number(sizes[i]);
            if(i < SLOTS - 1){uart_tx(',');}
        }
    }
}

void PATTERN(void)
{
    uart_tx(':');
    unsigned char i;
    for(i = 0; i < SLOTS; i++)
    {
        uart_tx_number(patterns[i]);
        if(i < SLOTS - 1){uart_tx(',');}
    }
    uart_tx_string("\nnew pattern:");
    uart_clear();
    __bis_SR_register(LPM0_bits | GIE);
    if(strcmp(uart_rx_buffer,"\r\n") != 0)
    {
        char *token;
        for(i = 0; i < SLOTS; i++)
        {
            if(i == 0){token = strtok(uart_rx_buffer,",");}
            else{token = strtok(NULL,",");}
            patterns[i] = (char) atoi(token);
            uart_tx_number(patterns[i]);
            if(i < SLOTS - 1){uart_tx(',');}
        }
    }
    else
    {
        for(i = 0; i < SLOTS; i++)
        {
            uart_tx_number(patterns[i]);
            if(i < SLOTS - 1){uart_tx(',');}
        }
    }
}

void MENU(void)
{
    uart_tx_string(":\n\n");
    uart_tx_string("menu    : list and describe known commands\n");
    uart_tx_string("status  : determine which devices are responsive\n");
    uart_tx_string("fill    : fill devices with set pattern\n");
    uart_tx_string("read    : read contents of all devices\n");
    uart_tx_string("check   : check contents of all devices against set pattern\n");
    uart_tx_string("scan    : count total number of mismatches against set pattern\n");
    uart_tx_string("hold    : lower comm lines to let devices be held in bias conditions\n");
    uart_tx_string("lift    : restore comm lines to their nominal state\n");
    uart_tx_string("size    : read and set the size (in kB) of all devices\n");
    uart_tx_string("pattern : read and set the pattern (in decimal) of all devices\n");
    uart_tx_string("adc     : read the raw ADC value from all four VRAM channels\n");
    uart_tx_string("\nsoftware version: ");
    uart_tx_string(version);
    uart_tx('\n');
}

void ADC(void)
{
    uart_tx(':');
    ADC12CTL0 |= ADC12SC;    // start conversion
    __bis_SR_register(LPM0_bits | GIE); // wait for conversion to complete
    uart_tx_number(ADC12MEM0);
    uart_tx(',');
    uart_tx_number(ADC12MEM1);
    uart_tx(',');
    uart_tx_number(ADC12MEM2);
    uart_tx(',');
    uart_tx_number(ADC12MEM3);
}

/********************************* MAIN *****************************/

int main(void)
{
  WDTCTL = WDTPW | WDTHOLD;                 // Stop Watchdog

  clk_init();
  gpio_init();
  uart_init();
  adc_init();
  lut_init();

  char *command;

  while(1)
  {
      __bis_SR_register(LPM0_bits | GIE);
      command = strtok(uart_rx_buffer,"\r");
      uart_tx_string(command);
      if(strcmp(command,"menu") == 0){MENU();}
      else if(strcmp(command,"status") == 0){STATUS();}
      else if(strcmp(command,"fill") == 0){FILL(sizes,patterns);}
      else if(strcmp(command,"read") == 0){READ(sizes);}
      else if(strcmp(command,"check") == 0){CHECK(sizes,patterns);}
      else if(strcmp(command,"scan") == 0){SCAN(sizes,patterns);}
      else if(strcmp(command,"hold") == 0){HOLD();}
      else if(strcmp(command,"lift") == 0){LIFT();}
      else if(strcmp(command,"size") == 0){SIZE();}
      else if(strcmp(command,"pattern") == 0){PATTERN();}
      else if(strcmp(command,"adc") == 0){ADC();}
      else{uart_tx_string(":invalid command");}
      uart_tx_string(".\n");
      uart_clear();
  }
}

/***************************** INTERRUPTS ******************************/

#pragma vector=USCI_A0_VECTOR   // UART interrupt vector
__interrupt void USCI_A0_ISR(void)
{
    switch(UCA0IV)
    {
        case USCI_NONE: break;
        case USCI_UART_UCRXIFG: // RX flag
            uart_rx_buffer[uart_rx_counter] = UCA0RXBUF;
            if(uart_rx_buffer[uart_rx_counter] == '\n')
            {
                __bic_SR_register_on_exit(LPM0_bits | GIE);
            }
            uart_rx_counter++;
            break;
        case USCI_UART_UCTXIFG: break;
        case USCI_UART_UCSTTIFG: break;
        case USCI_UART_UCTXCPTIFG: break;
    }
}

#pragma vector = ADC12_VECTOR   // ADC interrupt vector
__interrupt void ADC12_ISR(void)
{
    ADC12IFGR0 = 0; // clear IFG
    __bic_SR_register_on_exit(LPM0_bits | GIE);
}
