#include <avr/io.h>
#include <avr/interrupt.h>

// ================= STATE =================
enum State { Off, Idle, Active, Error };
volatile State currentState = Off;

// ================= TIME =================
volatile unsigned long ms = 0;

// ================= INPUTS =================
bool resetButton, offButton, onButton;

// ================= SENSOR =================
int distance = -1;
int lastValidDistance = 0;
int invalidCount = 0;

// ================= LCD =================
#define LCD_RS (1 << PA0)
#define LCD_E  (1 << PA1)
#define LCD_D4 (1 << PA2)
#define LCD_D5 (1 << PA3)
#define LCD_D6 (1 << PA4)
#define LCD_D7 (1 << PA5)

// ================= UART =================
#define F_CPU 16000000UL
#define TBE 0x20

volatile unsigned char *myUCSR0A = (unsigned char*)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char*)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char*)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int*)0x00C4;
volatile unsigned char *myUDR0   = (unsigned char*)0x00C6;

// ================= TIMER =================
ISR(TIMER1_COMPA_vect)
{
  ms++;
}

// ================= UART =================
void uartInit(unsigned long baud)
{
  unsigned int ubrr = (F_CPU / 16 / baud) - 1;

  *myUCSR0A = 0x00;
  *myUCSR0B = (1 << 3) | (1 << 4);
  *myUCSR0C = (1 << 1) | (1 << 2);
  *myUBRR0  = ubrr;
}

void uartWrite(char c)
{
  while(!(*myUCSR0A & TBE));
  *myUDR0 = c;
}

void uartPrint(const char *s)
{
  while(*s)
    uartWrite(*s++);
}

void uartPrintInt(int n)
{
  char buf[10];
  int i = 0;

  if(n == 0)
  {
    uartWrite('0');
    return;
  }

  if(n < 0)
  {
    uartWrite('-');
    n = -n;
  }

  while(n > 0)
  {
    buf[i++] = (n % 10) + '0';
    n /= 10;
  }

  while(i--)
    uartWrite(buf[i]);
}

// ================= LCD LOW LEVEL =================
void tinyDelay()
{
  for(volatile int i = 0; i < 40; i++);
}

void lcdPulse()
{
  PORTA |= LCD_E;
  tinyDelay();
  PORTA &= ~LCD_E;
  tinyDelay();
}

void lcdSendNibble(unsigned char data)
{
  PORTA &= ~(LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);

  if(data & 1) PORTA |= LCD_D4;
  if(data & 2) PORTA |= LCD_D5;
  if(data & 4) PORTA |= LCD_D6;
  if(data & 8) PORTA |= LCD_D7;

  lcdPulse();
}

void lcdCmd(unsigned char cmd)
{
  PORTA &= ~LCD_RS;
  lcdSendNibble(cmd >> 4);
  lcdSendNibble(cmd & 0x0F);
}

void lcdData(unsigned char d)
{
  PORTA |= LCD_RS;
  lcdSendNibble(d >> 4);
  lcdSendNibble(d & 0x0F);
}

void lcdPrint(const char *s)
{
  while(*s)
    lcdData(*s++);
}

void lcdInit()
{
  DDRA |= LCD_RS | LCD_E | LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7;

  for(volatile long i = 0; i < 200000; i++);

  lcdCmd(0x33);
  lcdCmd(0x32);
  lcdCmd(0x28);
  lcdCmd(0x0C);
  lcdCmd(0x06);
  lcdCmd(0x01);
}

void lcdSetCursor(int r, int c)
{
  lcdCmd((r == 0 ? 0x80 : 0xC0) + c);
}

void lcdPrintInt(int n)
{
  char b[10];
  int i = 0;

  if(n == 0)
  {
    lcdData('0');
    return;
  }

  while(n > 0)
  {
    b[i++] = (n % 10) + '0';
    n /= 10;
  }

  while(i--)
    lcdData(b[i]);
}

// ================= SENSOR =================
int readDistance()
{
  unsigned long echo = 0;
  unsigned long timeout = 0;

  PORTF &= ~(1 << PF0);
  tinyDelay();
  PORTF |= (1 << PF0);
  tinyDelay();
  PORTF &= ~(1 << PF0);

  while(!(PINF & (1 << PF1)) && timeout++ < 30000);
  if(timeout >= 30000)
    return -1;

  while((PINF & (1 << PF1)) && echo++ < 30000);

  return echo / 58;
}

// ================= BUTTONS =================
void readButtons()
{
  resetButton = !(PINB & (1 << PB4));
  offButton   = !(PINB & (1 << PB5));
  onButton    = !(PINE & (1 << PE4));
}

// ================= STATE MACHINE =================
void updateState()
{
  if(offButton)
  {
    currentState = Off;
    return;
  }

  if(currentState == Off)
  {
    if(onButton)
      currentState = Idle;
    return;
  }

  if(currentState == Error)
  {
    if(resetButton)
    {
      currentState = Idle;
      invalidCount = 0;
    }
    return;
  }

  if(distance == -1)
  {
    invalidCount++;

    if(invalidCount > 2)
      currentState = Error;

    return;
  }

  if(lastValidDistance < 90)
    currentState = Active;
  else
    currentState = Idle;
}

// ================= LED =================
void updateLED()
{
  unsigned char s = 0;

  if(currentState == Idle)
    s = (1 << PH5);

  if(currentState == Active)
    s = (1 << PH4);

  if(currentState == Error)
    s = (1 << PH6);

  PORTH = (PORTH & ~((1<<PH4)|(1<<PH5)|(1<<PH6))) | s;
}

// ================= BUZZER =================
void updateBuzzer()
{
  static unsigned long prev = 0;
  static int state = 0;

  if(currentState != Active)
  {
    PORTE &= ~(1 << PE3);
    return;
  }

  int interval =
    (lastValidDistance < 20) ? 50 :
    (lastValidDistance < 50) ? 150 : 400;

  if(ms - prev >= interval)
  {
    prev = ms;
    state = !state;

    if(state)
      PORTE |= (1 << PE3);
    else
      PORTE &= ~(1 << PE3);
  }
}

// ================= LCD =================
void updateLCD()
{
  static unsigned long prev = 0;

  if(ms - prev < 300)
    return;

  prev = ms;

  lcdCmd(0x01);

  lcdSetCursor(0,0);

  if(currentState == Off)
    lcdPrint("STATE: OFF");
  else if(currentState == Idle)
    lcdPrint("STATE: IDLE");
  else if(currentState == Active)
    lcdPrint("STATE: ACTIVE");
  else
    lcdPrint("STATE: ERROR");

  lcdSetCursor(1,0);
  lcdPrint("D:");

  if(currentState == Active)
    lcdPrintInt(lastValidDistance);
  else
    lcdPrint("--");
}

// ================= LOG =================
unsigned long getSeconds()
{
  return ms / 1000;
}

void logSystem()
{
  static unsigned long last = 0;

  if(ms - last < 1000)
    return;

  last = ms;

  uartPrint("T:");
  uartPrintInt(getSeconds());
  uartPrint(" | ");

  if(currentState == Off) uartPrint("OFF");
  else if(currentState == Idle) uartPrint("IDLE");
  else if(currentState == Active) uartPrint("ACTIVE");
  else uartPrint("ERROR");

  uartPrint(" | D:");
  uartPrintInt(lastValidDistance);
  uartPrint("\r\n");
}

// ================= SETUP =================
void setup()
{
  uartInit(9600);
  lcdInit();

  DDRF |= (1 << PF0);
  DDRF &= ~(1 << PF1);

  DDRB &= ~((1<<PB4)|(1<<PB5));
  PORTB |= (1<<PB4)|(1<<PB5);

  DDRE &= ~(1<<PE4);
  PORTE |= (1<<PE4);

  DDRH |= (1<<PH4)|(1<<PH5)|(1<<PH6);
  DDRE |= (1<<PE3);

  TCCR1A = 0;
  TCCR1B = (1<<WGM12)|(1<<CS11)|(1<<CS10);
  OCR1A = 249;
  TIMSK1 |= (1<<OCIE1A);

  sei();

  currentState = Off;
}

// ================= LOOP =================
void loop()
{
  readButtons();

  if(currentState != Off)
  {
    distance = readDistance();

    if(distance == -1)
    {
      invalidCount++;

      if(invalidCount > 3)
        currentState = Error;
    }
    else
    {
      invalidCount = 0;
      lastValidDistance = distance;
    }
  }

  updateState();
  updateLED();
  updateBuzzer();
  updateLCD();
  logSystem();
}