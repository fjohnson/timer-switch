//#define DEBUG_ME 1
#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
#include <EnableInterrupt.h>

hd44780_I2Cexp lcd; // declare lcd object: auto locate & config expander chip

// LCD geometry
const int LCD_COLS = 16;
const int LCD_ROWS = 2;

//interrupt changed variables need to be volatile
volatile unsigned long countdown = 10;
volatile unsigned long action_duration = 5;

//Modes are 1) count down 2) action count down 3) editing count down length 4) editing action length
//editing can be done at any time; there are three buttons on this device, 1 for increment, 1 for dec,
//and 1 for entering edit mode and also changing edit cd/ac. 
boolean mode_cd = true;
boolean edit_mode = false;
boolean em_cd = false;
boolean em_ac = false;
boolean action_on = false;

unsigned long last_button_change = 0;
const int DEBOUNCE_INTERVAL = 150;
const int MIN_COUNTDOWN = 2; //2 second minimum for any cd/ac
const int MAX_COUNTDOWN = 32767;
const int INC_BTN = 8;
const int DEC_BTN = 7;
const int MODE_BTN = 4;
const int ACTION_PIN = 2;
//variables consigned for dealing with button inc/dec acceleration
int btn_acl = 1;
const int BTN_POLL_TIME = 1000; // poll every 1000 milliseconds 
int btn_last_poll = 0;
int btn_held_sec = 0;

void setup(){
    int status;

    Serial.begin(9600);
    pinMode(MODE_BTN,INPUT_PULLUP);  
    pinMode(DEC_BTN,INPUT_PULLUP);  
    pinMode(INC_BTN,INPUT_PULLUP);  
	pinMode(ACTION_PIN, OUTPUT);
	    		
    status = lcd.begin(LCD_COLS, LCD_ROWS);
	if(status) // non zero status means it was unsuccessful
	{
		status = -status; // convert negative status value to positive number

		// begin() failed so blink error code using the onboard LED if possible
		hd44780::fatalError(status); // does not return
	}

	enableInterrupt(4, edit_mode_func, FALLING);
    enableInterrupt(INC_BTN, inc_func, FALLING);
    enableInterrupt(DEC_BTN, dec_func, FALLING);
	
	
}

void inc_func(){
    if(!debounce()){
        return;
    }

    if(!edit_mode){
        return;
    }

    if(em_cd){
        countdown += check_limits_inc(countdown, btn_acl);
    }else if(em_ac){
        action_duration += check_limits_inc(action_duration, btn_acl);
    }
}

void dec_func(){
	if(!debounce()){
        return;
    }

    if(!edit_mode){
        return;
    }

	//Limit of 2 seconds for count down durations
    if(em_cd){
        countdown -= check_limits_dec(countdown, btn_acl);
    }else if(em_ac){
        action_duration -= check_limits_dec(action_duration, btn_acl);
    }
}

boolean debounce(){
    if(!last_button_change){
        last_button_change = millis();
    }

    if(millis() - last_button_change > DEBOUNCE_INTERVAL){
        last_button_change = millis();
        return true; // Okay, we treat this as a valid signal
    }
    return false;
}

//Enters edit mode, sets global variables to signal state
void edit_mode_func(){

    if(!debounce()){
        return;
    }
	
	//start by editing duration first
    if(!em_cd && !em_ac){
        em_cd = true;
    }
    else if(em_cd){
        em_cd = false;
        em_ac = true;
    }else if(em_ac){
        em_ac = false;
		//should edits be saved here?
    }

    if(em_cd || em_ac){
        edit_mode = true;
    }else{
        edit_mode = false;
    }
}

unsigned long check_limits_inc(unsigned long timer, int x){
	if(timer+x > MAX_COUNTDOWN) return 0;
	return x;
}

unsigned long check_limits_dec(unsigned long timer, int x){
	if((int)timer-x < MIN_COUNTDOWN) return 0;
	return x;
}

void button_acceleration(){
	
	if(!edit_mode){
		return;
	}
	
	//This is necessary to ensure that a button that was just pressed and inc/dec through an interrupt
	//is not inc/dec again. Here the debounce interval is used to wait at least this amount of time 
	//before registering the button again.
	if(millis() - last_button_change <= DEBOUNCE_INTERVAL){
		return;
	}
	
	//Here I have reversed the logic because when activated due to pullup resistors
	//a pin will register as a LOW
	int inc_pin = !digitalRead(INC_BTN);
	int dec_pin = !digitalRead(DEC_BTN);

	
	if(inc_pin || dec_pin){
				
		if(em_cd){
			if(inc_pin){
				countdown += check_limits_inc(countdown, btn_acl);
			}
			else if(dec_pin){
				countdown -= check_limits_dec(countdown, btn_acl);
			}
		}
		else if(em_ac){
			if(inc_pin){
				action_duration += check_limits_inc(action_duration, btn_acl);
			}
			else if(dec_pin){
				action_duration -= check_limits_dec(action_duration, btn_acl);
			}
		}
		
		//increase acceleration after 5 seconds, max at 30 sec
		btn_held_sec++;
		
		if(!(btn_held_sec % 30)){
			Serial.print(btn_held_sec);
			if(btn_acl < 1000){
				btn_acl=1000;
			}
		}
		else if(!(btn_held_sec % 5)){
			if(btn_acl < 100) {
				btn_acl*=10;
			}
		}
	}
	else{
		btn_acl = 1;
		btn_held_sec = 0;
	}
}

//updates lcd screen. checks to see if count down or action is taking place. simultaneously allows
//editing of duration.
void loop(){
    unsigned long time_st, time_cr, time_delta; 
    String msg;
		
    while(true){
        
		#ifdef DEBUG_ME
			Serial.setTimeout(1000);
			String input = Serial.readString();
			if(input == "i"){
				inc_func();
				}else if(input == "o"){
				dec_func();
				}else if(input == "p"){
				edit_mode_func();
			} 
			
			Serial.println("edit_mode: " + String(edit_mode) + " em_cd " + String(em_cd) + " em_ac" + String(em_ac));
			
			if(mode_cd){
				Serial.println("countdown: "+ String((countdown*1000 - time_delta) / 1000) + "/" + String(countdown));		
			}
			else{
				Serial.println("action: " + String( (action_duration*1000 - time_delta) / 1000) + "/" + String(action_duration));	
			}
		#endif
		
		    
        if(mode_cd){
            time_cr = millis();
            time_delta = time_cr - time_st;
            if(time_delta >= countdown*1000){
                mode_cd = false;
                time_st = millis();
            }
            else{
                msg = String("CD: ") + String((countdown*1000 - time_delta) / 1000);
                lcd.clear();
				lcd_write(msg, 0);
                
            }
        }

        if(!mode_cd){
            time_cr = millis();
            time_delta = time_cr - time_st;
            if(!action_on){
                action();
                action_on = true;
            }else{
                if(time_delta >= action_duration * 1000){
                    action_off();
                    action_on = false;
                    time_st = millis();
                    mode_cd = true;
                }else{
                    msg = String("Action(): " + String( (action_duration * 1000 - time_delta) / 1000));
                    lcd.clear();
					lcd_write(msg, 0);                   
                }
            }            
        }

        if(edit_mode){
            if(em_cd){
                msg = String("CD: ") + String(countdown); 
            }else{
                msg = String("Action: " + String(action_duration));
            }
            lcd_write(msg, 1);
            lcd.blink();
        }else{
            lcd.noBlink();
        }
        
		button_acceleration();
        delay(150);
    }
}

void lcd_write(String msg, int row){
    lcd.setCursor(0,row);
    lcd.print(msg);
}
void action(){
	digitalWrite(ACTION_PIN,1);
}

void action_off(){
	digitalWrite(ACTION_PIN,0);
}
