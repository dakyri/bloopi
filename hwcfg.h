#pragma once

#include <stdint.h>

namespace config {
	enum mode_t: uint8_t {
		nieko = 0,
		prog = 1,
		ctrl = 2,
		chanpress = 3,
		keypress = 4,
		note = 5,
		bend = 6,
		start = 7,
		stop = 8,
		type = 0x0f,
		latch = 0x10,

		// in button field of pedal config
		no_shft = 0xff, // no alt mode or shift button 
		all_btn = 0xf0, // shift mode on all buttons
		// in channel field of pedal config
		btn_chn = 0x80, // shift mode uses the button midi channel
		// in the alt_which button mapping
		nc = 0xff // shift mode doesn't use this button
	};

constexpr uint8_t nButtons = 4;

#pragma pack(push, 1)
	struct button {
		uint8_t chan;
		uint8_t mode;
		uint8_t val1;
		uint8_t val2;
		uint8_t long_mode;
		uint8_t long_val1;
		uint8_t long_val2;
	};

	struct pedal {
		uint8_t chan;			// midi channel
		uint8_t mode;			// actually. ctrl or maybe pressure or even note
		uint8_t min_val;		// some part of the range 0-127
		uint8_t max_val;
		uint8_t which;			// controller or maybe note number
		uint8_t shift_button;	// which panel button, if any, alters it.
		uint8_t alt_chan;		// if shift button => alternate channel
		uint8_t alt_mode;		// if shift button => actually. ctrl or maybe pressure or even note
		uint8_t alt_min_val;	// if shift button => some part of the range 0-127
		uint8_t alt_max_val;
		uint8_t alt_which[nButtons];	// if shift button => controller or maybe note number
	};

	/* in the end we may be able to connect to accelerometers and such as wireless devices by bluetooth  */
	struct xlrm8r {
		uint8_t device_id;		// i2c id is the only way we have to distinguish theese
		uint8_t chan;			// midi channel
		uint8_t enable_button;	// which panel button, if any, enables it.
		uint8_t mode_x;			// pitch, actually. ctrl or maybe pressure or even note
		int8_t	min_ambit_x;	// ambit in range -127 -> 127 ... probably -45 -> +45  is the most useful
		int8_t	max_ambit_x;
		uint8_t min_x_val;		// some part of the range 0-127
		uint8_t max_x_val;
		uint8_t which_x[nButtons];	// controller or maybe note number
		uint8_t mode_y;			// roll, actually. ctrl or maybe pressure or even note
		int8_t	min_ambit_y;	// ambit in range -127 -> 127 ... probably -45 -> +45  is the most useful
		int8_t	max_ambit_y;
		uint8_t min_y_val; 		// some part of the range 0-127
		uint8_t max_y_val;
		uint8_t which_y[nButtons];	// controller or maybe note number
	};

#pragma pack(pop)

};