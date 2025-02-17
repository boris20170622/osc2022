
#include "gpio.h"
#include "uart.h"

/**
 * Set baud rate and characteristics (115200 8N1) and map to GPIO
 */
void uart_init()
{
    register unsigned int reg;

    /* initialize UART */
    *AUXENB     |= 1;       /* enable mini UART */
    *AUX_MU_CNTL_REG     = 0;       /* Disable transmitter and receiver during configuration. */

    *AUX_MU_IER_REG      = 0;       /* Disable interrupt */
    *AUX_MU_LCR_REG      = 3;       /* Set the data size to 8 bit. */
    *AUX_MU_MCR_REG      = 0;       /* Don’t need auto flow control. */
    *AUX_MU_BAUD     = 270;     /* 115200 baud */
    *AUX_MU_IIR_REG      = 6;       /* No FIFO */
    // *AUX_MU_IIR      = 0xc6;       /* No FIFO */

    /* map UART1 to GPIO pins */
    reg = *GPFSEL1;
    reg &= ~((7<<12)|(7<<15));  /* address of gpio 14, 15 */
    reg |=   (2<<12)|(2<<15);   /* set to alt5 */

    *GPFSEL1 = reg;

    *GPPUD = 0;                 /* enable gpio 14 and 15 */
    reg=150;
    while ( reg-- )
    { 
        asm volatile("nop"); 
    }
    
    *GPPUDCLK0 = (1<<14)|(1<<15);
    reg=150; 
    while ( reg-- )
    {
        asm volatile("nop");
    }
    
    *GPPUDCLK0 = 0;             /* flush GPIO setup */

    *AUX_MU_CNTL_REG = 3;           // Enable the transmitter and receiver.
}

/**
 * Send a character
 */
void uart_send(unsigned int c)
{
    /* Wait until we can send */
    do {
        
        asm volatile("nop");

    } while( ! ( *AUX_MU_LSR&0x20 ));
    
    /* write the character to the buffer */   
    *AUX_MU_IO = c;

    if ( c == '\n' ) 
    {
        do {
            
            asm volatile("nop");

        } while( ! ( *AUX_MU_LSR&0x20 ));
        
        *AUX_MU_IO = '\r';
    }
}

/**
 * Receive a character
 */
char uart_getc() {

    char r;
    
    /* wait until something is in the buffer */
    do{
        
        asm volatile("nop");
        
    } while ( ! ( *AUX_MU_LSR&0x01 ) );

    /* read it and return */
    r = ( char )( *AUX_MU_IO );

    /* convert carrige return to newline */
    return r == '\r' ? '\n' : r;
}

/**
 * Display a string
 */
void uart_puts(char *s)
{
    while( *s )
    {
        /* convert newline to carrige return + newline */
    
        //if(*s=='\n')
        //    uart_send('\r');

        uart_send(*s++);

    }
}

int uart_get_int(){
    char c;
    int num = 0;
    while((c = uart_getc())!='\n'){
        uart_send(c);
        num = num*10 + c - '0';
    }
    return num;
}