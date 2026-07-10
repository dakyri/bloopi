#pragma once

#include <thread>
#include <vector>
#include <tuple>
#include <atomic>

#include "message.h"
#include "hwcfg.h"

namespace hw_if {

using mod = config::mode_t;

/*!
 * structure representing a button state and possible actions
 * the corresponding config structure is a compact structure holding setup info
 */
struct button {
	enum state_t: uint8_t {
		open = 0,
		pressed = 0x01,
		longPressed = 0x02,
		latched = 0x80,
		latching = 0x40 // latch state = latched|latching when first engaged, so the next button up will keep the latch
	};

	button(config::button cfg)
		: lastBounceTime_ms(0), mode(cfg.mode), val1(cfg.val1), val2(cfg.val2),
		long_mode(cfg.long_mode), long_val1(cfg.long_val1), long_val2(cfg.long_val2),
		channel(cfg.chan), state(state_t::open), lastPressState(false) {}

	/*!
	 * might be called from an ISR
	 */
	void set(config::button &cfg, bool and_close) {
		if (and_close) {
		// if we have a button down that has a button up mode attached we need to set up an immediate button off
		// in the event we change button config at a shitty time
		/* 
			close_mode = mode;
			close_chan = channel;
			close_val1 = val1;
			do_close = true;
		*/
		}
		lastBounceTime_ms = 0;
		mode = cfg.mode;
		val1 = cfg.val1;
		val2 = cfg.val2;
		long_mode = cfg.long_mode;
		long_val1 = cfg.long_val1;
		long_val2 = cfg.long_val2;
		channel = cfg.chan;
		state = state_t::open;
		lastPressState = false;		
	}

	/*!
	 * in any kind of pressed or latched state 
	 */
	bool active() {
		return state | (state_t::latched | state_t::latching | state_t::pressed | state_t::longPressed); 	
	}

	long lastBounceTime_ms;
	long pressedTime_ms;
	uint8_t mode;
	uint8_t val1;
	uint8_t val2;
	uint8_t long_mode;
	uint8_t long_val1;
	uint8_t long_val2;
	uint8_t channel;
	uint8_t active_type;
	uint8_t active_val1;
	uint8_t state;
	bool lastPressState;
};

/*!
 * structure representing a pedal state and possible actions
 * the corresponding config structure is a compact structure holding setup info
 * the minval and maxval are [0, 127] for pedals and bends, just to simplify the config. bends will be scaled up
 * and spread across two bytes
 */
struct pedal {
	pedal(config::pedal cfg) {
		set(cfg);
	}

	void set(config::pedal &cfg) {
		lastChangeTime_ms = 0;
		lastCV = 255;
		lastCV2 = 255;
		ctrl = cfg.which;
		mode = cfg.mode;
		chan = cfg.chan;
		minVal = cfg.min_val;
		maxVal = cfg.max_val;
		factor = (maxVal - minVal + 1);
		factor /= (mode == mod::bend? 2.0 : 256.0);
		alt_mode = cfg.alt_mode;
		alt_chan = cfg.alt_chan;
		alt_minVal = cfg.alt_min_val;
		alt_maxVal = cfg.alt_max_val;
		auto p = alt_ctrl;
		for (const auto i: cfg.alt_which) {
			*p++ = i;
		}
		alt_factor = (alt_maxVal - alt_minVal + 1);
		alt_factor /= (alt_mode == mod::bend? 2.0 : 256.0);
	}

	long lastChangeTime_ms;
	float factor = 0.5;
	float alt_factor = 0.5;

	uint8_t shift_btn;

	uint8_t mode;
	uint8_t ctrl;
	uint8_t chan;
	uint8_t minVal;
	uint8_t maxVal;

	uint8_t alt_mode;
	uint8_t alt_chan;
	uint8_t alt_minVal;
	uint8_t alt_maxVal;
	uint8_t alt_ctrl[config::nButtons];

	uint8_t lastCV;
	uint8_t lastCV2;
};

using btst8 = button::state_t;

};

class BlooGPIO {
public:
	BlooGPIO(msg::q_t& _inQ, msg::q_t& _midiInQ);
	~BlooGPIO();

	bool start();
	void stop();

	using btst8 = hw_if::button::state_t;
	using mod = config::mode_t;

protected:
	std::thread hwThread;
	std::atomic<bool> isRunning;

	msg::q_t& inQ; //!< input queue for hardware to go out. atm this is all MIDI
	msg::q_t& midiOutQ; //!< incoming h/w events are converted to midi and added here

	std::vector<hw_if::button> buttons;
	std::vector<hw_if::pedal> pedals;

	int uartFd=-1;
	uint8_t currentCmd = 0;
	bool hasIncompleteCmd = false;

	void hwRunner();
	void sendUARTMidi(uint8_t cmd, uint8_t v1=0, uint8_t v2=0);
	std::tuple<bool, uint8_t, uint8_t, uint8_t> recvUARTMidi();
	void sendButtonOnMidi(uint8_t typ, uint8_t chan, uint8_t v1, uint8_t v2);
	void sendButtonOffMidi(uint8_t typ, uint8_t chan, uint8_t v1);
	void checkPanelButton(const uint8_t which, const bool pressed);
	uint8_t checkShiftButton(uint8_t shift_btn, uint8_t alt_chan, uint8_t &chan, uint8_t alt_ctrl[], uint8_t &ctrlVal1);
	void checkPedal(const uint8_t which, const uint8_t cv);
};