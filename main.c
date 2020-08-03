// Copyright (c) 2015-16, Joe Krachey
// All rights reserved.
//
// Redistribution and use in source or binary form, with or without modification, 
// are permitted provided that the following conditions are met:
//
// 1. Redistributions in source form must reproduce the above copyright 
//    notice, this list of conditions and the following disclaimer in 
//    the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "main.h"
#include "lcd.h"
#include "timers.h"
#include "ps2.h"
#include "launchpad_io.h"
#include "HW3_images.h"

char group[] = "Jeremy";
char individual_1[] = "Chang Won Choi";
char individual_2[] = "Evan Williams";

///////////////////////////
// Global declared next //
/////////////////////////

volatile bool debounce_int = false;
volatile bool joystick_int = false;
volatile bool missle_int = false;
volatile bool ADC_start_int = false;
volatile bool LEDR_int = false;
volatile bool LEDG_int = false;
volatile bool meteor_create_int = false;
volatile bool meteor_int = false;

ADC0_Type  *myADC;

left_right_t joystick_left_right;
up_down_t joystick_up_down;
plane_t plane;

struct missle * m_head = NULL;
struct missle * m_tail = NULL;
struct meteor * o_head = NULL;
struct meteor * o_tail = NULL;

//*****************************************************************************
//*****************************************************************************
void initialize_hardware(void)
{
	
	initialize_serial_debug();
	
	//// setup lcd GPIO, config the screen, and clear it ////
	lcd_config_gpio();
  lcd_config_screen();
	lcd_clear_screen(LCD_COLOR_BLACK);
	
	//// setup the timers ////
	gp_timerA_config_16(TIMER0_BASE, TIMER_TAMR_TAMR_PERIOD, false, true);
	gp_timerB_config_16(TIMER0_BASE, TIMER_TBMR_TBMR_PERIOD, false, true);
	gp_timerA_set_ticks(TIMER0_BASE, 2500, 199);
	gp_timerB_set_ticks(TIMER0_BASE, 5000, 199);
	
	//// setup GPIO for LED drive ////
	lp_io_init();
	lp_io_clear_pin(RED_BIT);
	lp_io_clear_pin(BLUE_BIT);
	lp_io_clear_pin(GREEN_BIT);
	
	//// Setup ADC to convert on PS2 joystick using SS2 and interrupts ////
	ps2_SS2_initialize();
}


//*****************************************************************************
//*****************************************************************************

/******************************************************************************
 * Timer A interrupt handler
 * Interrupted every 10ms
 * Sets flags that will be handled in main
 *****************************************************************************/
void TIMER0A_Handler(void) {
	
	//Sets flag for SW1
	debounce_int = true;
	//Sets flag for missle update
	missle_int = true;
	//Sets flag for LEDR
	LEDR_int = true;
	//Clear timer A interrupt
	TIMER0-> ICR |= TIMER_ICR_TATOCINT;
}

/******************************************************************************
 * Timer B interrupt handler
 * Interrupted every 20ms
 * Sets flags that will be handled in main
 *****************************************************************************/
void TIMER0B_Handler(void) {
	
	//Sets flag for ADC conversion
	ADC_start_int = true;
	//Sets flag for LEDG
	LEDG_int = true;
	
	meteor_create_int = true;
	
	meteor_int = true;
	//Clear timer B interrupt
	TIMER0-> ICR |= TIMER_ICR_TBTOCINT;
}

/******************************************************************************
 * ADC SS2 interrupt handler
 * Interrupted when the conversion in SS2 is complete
 * Sets flags that will be handled in main
 *****************************************************************************/
void ADC0SS2_Handler(void) {
	//Sets flag for joystick value used in plane update
	joystick_int = true;
	//Stop conversions in SS2
	myADC->PSSI &= ~ADC_PSSI_SS2;
	//Clears ADC SS2 interrupt
	myADC->ISC = ADC_ISC_IN2;
}

/******************************************************************************
 * Calculates whether the plane should move left, right, or not move
 * based on value received from the joystick
 *
 * Parameters:
 *  ps2_x - uint32_t x_value of the joystick
 *
 *****************************************************************************/
static void ps2_lr(uint32_t ps2_x) {
	//Default: idle
	joystick_left_right = IDLE_lr;
	//if joystick x value is less than 1/4 its max value then move right
	if (ps2_x < 0x3FF) {
		joystick_left_right = RGHT;
	}
	//if joystick x value is greater than 3/4 its max value then move right
	else if (ps2_x > 0xBFD) {
		joystick_left_right = LFT;
	}
}

/******************************************************************************
 * Calculates whether the plane should move up, down, or not move
 * based on value received from the joystick
 *
 * Parameters:
 *  ps2_y - uint32_t y_value of the joystick
 *
 *****************************************************************************/
static void ps2_ud(uint32_t ps2_y) {
	//Default: idle
	joystick_up_down = IDLE_ud;
	//if joystick y value is less than 1/4 its max value then move down
	if (ps2_y < 0x3FF) {
		joystick_up_down = DOWN;
	}
	//if joystick y value is greater than 3/4 its max value then move up
	else if (ps2_y > 0xBFD) {
		joystick_up_down = UP;
	}
}

/******************************************************************************
 * Updates the position of the plane
 *****************************************************************************/
static void update_plane() {
	
	//If the plane should go left and there is room then move left by 1 pixel
	if (joystick_left_right == LFT && (plane.x_loc - PLANE_WIDTH/2) > 0) {
		plane.x_loc--;
	}
	//If the plane should go right and there is room then move right by 1 pixel
	else if (joystick_left_right == RGHT && (plane.x_loc + PLANE_WIDTH/2 + 1) < COLS) {
		plane.x_loc++;
	}
	//If the plane should go up and there is room then move up by 1 pixel
	if (joystick_up_down == UP && (plane.y_loc - PLANE_HEIGHT/2) > 0) {
		plane.y_loc--;
	}
	//If the plane should go down and there is room then move down by 1 pixel
	else if (joystick_up_down == DOWN && (plane.y_loc + PLANE_HEIGHT/2) < ROWS) {
		plane.y_loc++;
	}
		
}

/******************************************************************************
 * Updates the position of the missles
 *****************************************************************************/
static void update_missles() {

	struct missle *current;
	//current initially is the head for linked list
	current = m_head;
	
	//Traverse through the linked list
	while (current != NULL) {
		//Move the missle up by 1 pixel
		current->y_loc--;
		//If the missle reaches the top remove it
		if ((current->y_loc - MISSLE_HEIGHT/2) < 0) {
			remove_missle(current);
		}
		//Move current to next item
		current = current->nxt;
	}
}

/******************************************************************************
 * Draws the missles
 *****************************************************************************/
static void draw_missles() {
	
	struct missle *current;
	//current initially is the head for linked list
	current = m_head;
	
	//Traverse through the linked list
	while (current != NULL) {
		//Draw the missle
		lcd_draw_image(current->x_loc, MISSLE_WIDTH, current->y_loc, MISSLE_HEIGHT, missleBitmap, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
		//Move current to next item
		current = current->nxt;
	}
}

void add_meteor() {
	struct meteor *new_meteor;

	new_meteor = malloc(sizeof(struct meteor));

	new_meteor->x_loc = (rand()%(240-METEOR_WIDTH)) + METEOR_WIDTH/2;

	new_meteor->y_loc = METEOR_HEIGHT/2;
	
	new_meteor->x_speed = (rand()%5)-1;
	
	new_meteor->y_speed = (rand()%3)+1;
	
	new_meteor->meteor_x_speed_cnt = 0;
	
	new_meteor->meteor_y_speed_cnt = 0;

	new_meteor->nxt = NULL;
		
	if (o_head == NULL) {
		o_head = new_meteor;
	}
	
	if (o_tail != NULL) {
		o_tail->nxt = new_meteor;
	}

	o_tail = new_meteor;
	
	lcd_draw_image(new_meteor->x_loc, METEOR_WIDTH, new_meteor->y_loc, METEOR_HEIGHT, meteorBitmap, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

bool remove_meteor(struct meteor* del_meteor) {
	struct meteor *current = o_head;
	struct meteor *next;
	
	if (o_head == NULL) {
		return false;
	}
	if (current == del_meteor) {
		o_head = current->nxt;
		if (o_tail == current) {
			o_tail = NULL;
		}
		lcd_draw_image(del_meteor->x_loc, METEOR_WIDTH, del_meteor->y_loc - 1, METEOR_HEIGHT, meteorErase, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
		free(del_meteor);
		return true;
	}
	while (current->nxt != NULL) {
		if (current->nxt == del_meteor) {
			if (current->nxt == o_tail) {
				o_tail = current;
			}
			next = current->nxt->nxt;
			current->nxt = next;
			lcd_draw_image(del_meteor->x_loc, METEOR_WIDTH, del_meteor->y_loc - 1, METEOR_HEIGHT, meteorErase, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
			free(del_meteor);
			return true;
		}
		current = current->nxt;
	}
	return false;
}

static void update_meteors() {

	struct meteor *current;
	
	current = o_head;
	
	while (current != NULL) {
		current->meteor_x_speed_cnt++;
		current->meteor_y_speed_cnt++;
		
		if (current->meteor_y_speed_cnt == current->y_speed) {
			current->meteor_y_speed_cnt = 0;
			current->y_loc++;
			if ((current->y_loc + METEOR_HEIGHT/2) > ROWS) {
				remove_meteor(current);
			}
		}
		
		if (current->meteor_x_speed_cnt == current->x_speed) {
			current->meteor_x_speed_cnt = 0;
			if (current->x_speed < 0) {
				current->x_loc--;
				if ((current->x_loc - METEOR_WIDTH/2) < 0) {
					remove_meteor(current);
				}
			}
			else if (current->x_speed > 0) {
				current->x_loc++;
				if ((current->x_loc + METEOR_WIDTH/2) > COLS) {
					remove_meteor(current);
				}
			}
		}
		current = current->nxt;
	}
}

static void draw_meteors() {
	
	struct meteor *current;

	current = o_head;
	
	while (current != NULL) {

		lcd_draw_image(current->x_loc, METEOR_WIDTH, current->y_loc, METEOR_HEIGHT, meteorBitmap, LCD_COLOR_GRAY, LCD_COLOR_BLACK);

		current = current->nxt;
	}
}

/******************************************************************************
 * Add a missle
 *****************************************************************************/
void add_missle() {
	struct missle *new_missle;

	//If there is enough room for a missle to be created then add a missle
	if (plane.y_loc - PLANE_HEIGHT/2 - MISSLE_HEIGHT/2 > 0) {
		//allocate space for missle
		new_missle = malloc(sizeof(struct missle));
		//set x location
		new_missle->x_loc = plane.x_loc;
		//set y location
		new_missle->y_loc = plane.y_loc - PLANE_HEIGHT/2 - MISSLE_HEIGHT/2;
		//set next item
		new_missle->nxt = NULL;
		
		//If there is no missle in the list
		if (m_head == NULL) {
			m_head = new_missle;
		}
		//If there are at least one missle i the list
		if (m_tail != NULL) {
			m_tail->nxt = new_missle;
		}
		//Add missle to the list
		m_tail = new_missle;
		//Draw the new missle
		lcd_draw_image(new_missle->x_loc, MISSLE_WIDTH, new_missle->y_loc, MISSLE_HEIGHT, missleBitmap, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
	}
}

bool remove_missle(struct missle* del_missle) {
	struct missle *current = m_head;
	struct missle *next;
	
	if (m_head == NULL) {
		return false;
	}
	if (current == del_missle) {
		m_head = current->nxt;
		if (m_tail == current) {
			m_tail = NULL;
		}
		lcd_draw_image(del_missle->x_loc, MISSLE_WIDTH, del_missle->y_loc + 1, MISSLE_HEIGHT, missleErase, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
		free(del_missle);
		return true;
	}
	while (current->nxt != NULL) {
		if (current->nxt == del_missle) {
			if (current->nxt == m_tail) {
				m_tail = current;
			}
			next = current->nxt->nxt;
			current->nxt = next;
			lcd_draw_image(del_missle->x_loc, MISSLE_WIDTH, del_missle->y_loc + 1, MISSLE_HEIGHT, missleErase, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
			free(del_missle);
			return true;
		}
		current = current->nxt;
	}
	return false;
}

static bool sw1_debounce_fsm(void)
{
  static DEBOUNCE_STATES state = DEBOUNCE_ONE;
  bool pin_logic_level;
  
  pin_logic_level = lp_io_read_pin(SW1_BIT);
  
  switch (state)
  {
    case DEBOUNCE_ONE:
    {
      if(pin_logic_level)
      {
        state = DEBOUNCE_ONE;
      }
      else
      {
        state = DEBOUNCE_1ST_ZERO;
      }
      break;
    }
    case DEBOUNCE_1ST_ZERO:
    {
      if(pin_logic_level)
      {
        state = DEBOUNCE_ONE;
      }
      else
      {
        state = DEBOUNCE_2ND_ZERO;
      }
      break;
    }
    case DEBOUNCE_2ND_ZERO:
    {
      if(pin_logic_level)
      {
        state = DEBOUNCE_ONE;
      }
      else
      {
        state = DEBOUNCE_PRESSED;
      }
      break;
    }
    case DEBOUNCE_PRESSED:
    {
      if(pin_logic_level)
      {
        state = DEBOUNCE_ONE;
      }
      else
      {
        state = DEBOUNCE_PRESSED;
      }
      break;
    }
    default:
    {
      while(1){};
    }
  }
  
  if(state == DEBOUNCE_2ND_ZERO )
  {
    return true;
  }
  else
  {
    return false;
  }
}

int main(void)
{
	static uint32_t LEDR_cnt = 0;
	static uint32_t LEDG_cnt = 0;
	static uint32_t meteor_cnt = 0;
	
	static uint32_t ps2_x;
	static uint32_t ps2_y;

	bool sw1_pressed = false;
	bool LEDR_on = false;
	bool LEDG_on = false;
	
	myADC = (ADC0_Type *)PS2_ADC_BASE;
	
  initialize_hardware();

  put_string("\n\r");
  put_string("************************************\n\r");
  put_string("ECE353 - Spring 2018 HW3\n\r  ");
  put_string(group);
  put_string("\n\r     Name:");
  put_string(individual_1);
  put_string("\n\r     Name:");
  put_string(individual_2);
  put_string("\n\r");  
  put_string("************************************\n\r");

	//// Initialize Plane location and image ////
	plane.x_loc = COLS/2;
	plane.y_loc = ROWS/2;
	lcd_draw_image(COLS/2, PLANE_WIDTH, ROWS/2, PLANE_HEIGHT, planeBitmap, LCD_COLOR_BLUE2, LCD_COLOR_BLACK);
	
  // Reach infinite loop
  while(1){
		if (joystick_int) {
			joystick_int = false;
			ps2_x = myADC->SSFIFO2 & 0xFFF;
			ps2_y = myADC->SSFIFO2 & 0xFFF;
			ps2_lr(ps2_x);
			ps2_ud(ps2_y);
			update_plane();
		}
		if (ADC_start_int) {
			ADC_start_int = false;
			start_adc_SS2(PS2_ADC_BASE);
		}
		if (missle_int) {
			missle_int = false;
			update_missles();
		}
		if (debounce_int) {
			debounce_int = false;
			sw1_pressed = sw1_debounce_fsm();
			if (sw1_pressed) {
				add_missle();
			}
		}
		if (LEDR_int) {
			LEDR_int = false;
			LEDR_cnt++;
			if (LEDR_cnt == 20) {
				LEDR_cnt = 0;
				if (LEDR_on) {
					LEDR_on = false;
					lp_io_clear_pin(RED_BIT);
				}
				else {
					LEDR_on = true;
					lp_io_set_pin(RED_BIT);
				}
			}
		}
		if (LEDG_int){
			LEDG_int = false;
			LEDG_cnt++;
			if (LEDG_cnt == 20) {
				LEDG_cnt = 0;
				if (LEDG_on) {
					LEDG_on = false;
					lp_io_clear_pin(GREEN_BIT);
				}
				else {
					LEDG_on = true;
					lp_io_set_pin(GREEN_BIT);
				}
			}
		}
		if (meteor_create_int){
			int i = 0;
			int meteor_num = (rand()%3) + 1;
			meteor_create_int = false;
			meteor_cnt++;
			if (meteor_cnt == 100) {
				meteor_cnt = 0;
				for (i = 0; i < meteor_num; i++) {
					add_meteor();
				}
			}
		}
		if (meteor_int) {
			meteor_int = false;
			update_meteors();
		}
		
		lcd_draw_image(plane.x_loc, PLANE_WIDTH, plane.y_loc, PLANE_HEIGHT, planeBitmap, LCD_COLOR_BLUE2, LCD_COLOR_BLACK);

		draw_missles();
		
		draw_meteors();
  }		// end of while(1) loop
}
