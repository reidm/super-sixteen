#include "AnalogIO.h"
#include "Buttons.h"
#include "Calibrate.h"
#include "Dac.h"
#include "Display.h"
#include "Encoder.h"
#include "LEDMatrix.h"
#include "Memory.h"
#include "Sequencer.h"
#include "Variables.h"
#include "Pinout.h"
#include "Ui.h"
#include <Arduino.h>
#include <avr/pgmspace.h>


namespace supersixteen{

Buttons buttons;
AnalogIo analogIo;
Display display;
Encoder encoder;
LedMatrix ledMatrix;
Memory memory;

//initialized by main.cpp
Calibration *calibrationVar2;
Sequencer *sequencerVar2;
Dac *dacVar2;

bool shift_state = false;
bool record_mode = false;
bool repeat_mode = false;
bool saving = false;

const byte SEQUENCE_MODE = 0;
const byte CALIBRATE_MODE = 1;
const byte LOAD_MODE = 2;
const byte SAVE_MODE = 3 ;
const byte EDIT_PARAM_MODE = 4;

const byte PARAM_BARS = 8;
const byte PARAM_STEPS = 9;
const byte PARAM_SCALE = 10;
const byte PARAM_SWING = 11;
const byte PARAM_TRANSPOSE = 12;

byte current_param = PARAM_BARS;
byte ui_mode = SEQUENCE_MODE;
byte calibration_step = 0;
byte current_patch = 1;
byte selected_patch = 1;
char scalename[10];



void Ui::init(Calibration& calibration, Dac& dac, Sequencer& sequencer){
    pinMode(CS0_PIN, OUTPUT);
	pinMode(CS1_PIN, OUTPUT);
	pinMode(CS2_PIN, OUTPUT);
	pinMode(CS3_PIN, OUTPUT);
	digitalWrite(CS0_PIN, HIGH);
	digitalWrite(CS1_PIN, HIGH);
	digitalWrite(CS2_PIN, HIGH);
	digitalWrite(CS3_PIN, HIGH);

	calibrationVar2 = &calibration;
    dacVar2         = &dac;
    sequencerVar2   = &sequencer;
    analogIo.init(sequencer);
    buttons.init();
    display.init();
	encoder.init();
    ledMatrix.init(display, sequencer);
	if (!memory.init(sequencer)) {
		display.setDisplayAlpha("MEM");
		return;
	}
	initializeSequenceMode();
}

void Ui::poll(){
	if (saving) {
		finishSaving();
	}
    buttons.poll();
	uint16_t value = 0;
	buttons.getQueuedEvent(value);
    if (value) {
        int button_pressed = value & 0x00FF; //use last 8 bits for button number
		bool button_state = (value & 0x0100) >> 8; // use first 8 bits (one of them anyway) for button state
		onButtonToggle(button_pressed, button_state);
        //buttons.button_toggled = false; // reset state for next poll
    }

    int incrementAmount = encoder.poll();
    if (abs(incrementAmount) > 0) {
        onEncoderIncrement(incrementAmount); //encoder.getIncrementAmount());
    }

	if (ui_mode == SEQUENCE_MODE) {
		analogIo.poll();
		//if (!record_mode) {
			if (record_mode || analogIo.paramChanged()){
				display.setDisplayNum(analogIo.getDisplayNum());
			}
		//}
	}
}

void Ui::multiplex(){
	ledMatrix.multiplexLeds(); //this also updates the seven segment display on a shared serial line, messy i know
	ledMatrix.blinkStep();
}

void Ui::onSaveButton(bool state) {
	if (state) { //only toggle on input
		if (ui_mode == CALIBRATE_MODE) {
			ui_mode = SEQUENCE_MODE;
			calibrationVar2->writeCalibrationValues();
			initializeSequenceMode();
		} else if (ui_mode == SAVE_MODE) {

			//actually save the patch!!
			byte saveStatus = memory.save(selected_patch);
			current_patch = selected_patch;
			if (saveStatus == 1 || saveStatus == 2) { //saved
				saving = true;
				//TODO display something, like rapid flashing?
				display.blinkDisplay(true, 100, 5);
				ui_mode = SEQUENCE_MODE;
			} else if (saveStatus == 0) {
				display.setDisplayAlpha("ERR");
			}
		} else { //sequencing, presumably
			if (saving) return; //disallow double-presses


			ui_mode = SAVE_MODE;
			selected_patch = current_patch;
			display.setDisplayNum(selected_patch);
			display.blinkDisplay(true, 300, 0);
		}
	}
}

void Ui::finishSaving(){
	if (memory.finishSaving()) {
		saving = false;
	}
}


void Ui::onLoadButton(bool state) {
	if (state) { //only toggle on input
		if (saving) return;

		if (ui_mode == LOAD_MODE) {
			//actually load patch
			if (memory.load(selected_patch)) {
				display.blinkDisplay(true, 100, 5);
			} else {
				display.setDisplayAlpha("ERR");
			}
			ui_mode = SEQUENCE_MODE;
			current_patch = selected_patch;
			ledMatrix.setMatrixFromSequencer();
			//TODO set sequencer current step by length of active sequence!
		} else if (ui_mode == SEQUENCE_MODE){
			ui_mode = LOAD_MODE;
			selected_patch = current_patch;
			display.setDisplayNum(selected_patch);
			display.blinkDisplay(true, 300, 0);
		}
	}
}

void Ui::onButtonToggle(int button, bool button_state) {
	if (button < 16) { //inside button grid
        //display.setDisplayNum(button);
        if (button_state) {
			if (ui_mode == CALIBRATE_MODE) {
				updateCalibration(button);
			} else {
				if (shift_state) {
					shiftFunction(button);
				} else {
					selectStep(button);
				}
			}
        } else {
			        //display.setDisplayNum(button*-1);
		}
    } else {
        switch (button-8) {
		case SHIFT_PIN:  onShiftButton(button_state); break;
		case PLAY_PIN:   onPlayButton(button_state); break;
		case LOAD_PIN:   onLoadButton(button_state); break;
		case SAVE_PIN:   onSaveButton(button_state); break;
		case GLIDE_PIN:  onGlideButton(button_state); break;
		case RECORD_PIN: onRecButton(button_state); break;
		case REPEAT_PIN: onRepeatButton(button_state); break;
		//default: display.setDisplayNum(button);
        }
		display.setDecimal(!button_state);
    }
}

void Ui::onShiftButton(bool button_state){
	shift_state = button_state;
	if (button_state) {
		cancelSaveOrLoad();
	}
}

void Ui::shiftFunction(int button) {
	if (button < 8) {
		//todo implement multi-bar sequence
	} else {
		switch (button) {
			case 14: break;//clear sequence; break;
			//case 13: initializeCalibrationMode(); break;
			case PARAM_BARS: //select bars?
			case PARAM_STEPS:
			case PARAM_SCALE:
			case PARAM_SWING:
			case PARAM_TRANSPOSE:
			  ui_mode = EDIT_PARAM_MODE;
			  current_param = button;
			  onEncoderIncrement(0);


		}
	}
}

void Ui::onEncoderIncrement(int increment_amount) {
	if (ui_mode == CALIBRATE_MODE) {
		display.setDisplayNum(calibrationVar2->incrementCalibration(increment_amount, calibration_step));
        updateCalibration(calibration_step);
	} else if (ui_mode == SAVE_MODE || ui_mode == LOAD_MODE) {
		selected_patch += increment_amount;
		if (selected_patch < 1) selected_patch = 99;
		if (selected_patch > 99) selected_patch = 1;
		display.setDisplayNum(selected_patch);
	} else if (ui_mode == EDIT_PARAM_MODE) {
		int param = 0;
		if (current_param == PARAM_SCALE) {
			param = sequencerVar2->incrementScale(increment_amount);
			strcpy_P(scalename, (char *)pgm_read_word(&(scale_names[param])));  // Necessary casts and dereferencing, just copy (for PROGMEM keywords in flash)
			display.setDisplayAlpha(scalename);
		} else {
			switch(current_param) {
				case PARAM_BARS:  param = sequencerVar2->incrementBars(increment_amount); break;
				case PARAM_STEPS: param = sequencerVar2->incrementSteps(increment_amount); break;
				case PARAM_SWING: param = sequencerVar2->incrementSwing(increment_amount); break;
				case PARAM_TRANSPOSE: param = sequencerVar2->incrementTranspose(increment_amount); break;
			}
			display.setDisplayNum(param);
		}
	} else {//sequencing, presumably
		display.setDisplayNum(sequencerVar2->incrementTempo(increment_amount));
	}
}

void Ui::onGlideButton(bool state){
	if (state) {
		//display.setDisplayAlpha("GLD");
		buttons.setGlideLed(sequencerVar2->toggleGlide());
	}
}

void Ui::onPlayButton(bool state){
	if (shift_state) {
		sequencerVar2->onReset();
	}
	if (state && isSequencing()) {
		cancelSaveOrLoad();
		sequencerVar2->onPlayButton();
	}
}

void Ui::onRecButton(bool state){
	if (isSequencing()){
		record_mode = state;
		sequencerVar2->setRecordMode(state);
	}
}

void Ui::onRepeatButton(bool state){
	if (isSequencing()){
		repeat_mode = state;
		sequencerVar2->setRepeatMode(state);
	}
}

void Ui::selectStep(int step){
	cancelSaveOrLoad();
	if (repeat_mode) {
		sequencerVar2->setRepeatLength(step+1);
		return;
	}
    sequencerVar2->selectStep(step);
	ledMatrix.setMatrixFromSequencer();
	//ledMatrix.blinkLed();
    analogIo.displaySelectedParam();
	display.setDisplayNum(analogIo.getDisplayNum());
	buttons.setGlideLed(sequencerVar2->getGlide());
}


void Ui::initializeCalibrationMode() {
	cancelSaveOrLoad();
	ui_mode = CALIBRATE_MODE;
	calibrationVar2->readCalibrationValues();
	updateCalibration(calibration_step);
	
}

void Ui::updateCalibration(int step) {
    if (step > 8) return;
    calibration_step = step;
	ledMatrix.reset();
	ledMatrix.toggleLed(step);
	display.setDisplayNum(calibrationVar2->getCalibrationValue(step));
	dacVar2->setOutput(0, GAIN_2, 1, calibrationVar2->getCalibratedOutput(step * 12));
}

bool Ui::isSequencing(){
	return (ui_mode != CALIBRATE_MODE);
}

void Ui::initializeSequenceMode(){
	ledMatrix.reset();
	ledMatrix.setMatrixFromSequencer();
	analogIo.displaySelectedParam();
	display.setDisplayNum(analogIo.getDisplayNum());
}

void Ui::onStepIncremented(){
	ledMatrix.setMatrixFromSequencer();
	ledMatrix.blinkCurrentStep();
	if (record_mode) {
		analogIo.recordCurrentParam();
	}
	sequencerVar2->setActiveNote();
}

bool Ui::cancelSaveOrLoad(){
	if (ui_mode == LOAD_MODE || ui_mode == SAVE_MODE || ui_mode == EDIT_PARAM_MODE) {
		display.blinkDisplay(false, 100, 0);
		ui_mode = SEQUENCE_MODE;
		analogIo.displaySelectedParam();
		display.setDisplayNum(analogIo.getDisplayNum());
		return true;
		
	}
	return false;
}

}