#include "bloo_gpio.h"
#include "mw_midi.h"

#include <spdlog/spdlog.h>

#include <PCF8574mw.h>
#include <PCF8591mw.h>
#include <pi2c.h>

#ifdef HAS_WIRING_PI
#include <wiringSerial.h>
#endif

using midi = mw_midi::cmd;
using spdlog::error;

#ifndef HAS_WIRING_PI

#include <chrono>

int millis() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
#endif

/*!
 * wraps an common anode rgb led on the given bit of an 8574
 */
class Lamp8574 {
public:
	Lamp8574(PCF8574<Pi2c> &d, uint8_t b): device(d), atbit(b), baseColor(Lamp8574::color::green), unblink_ms(0) {}
	enum color {
		black = 0b111,
		blue = 0b011,
		red = 0b110,
		green = 0b101,
		white = 0b000
	};

	void setColor(uint8_t c, uint32_t blink_t=0) {
		if (unblink_ms == 0) {
			device.writeOutputs(c << atbit);
		}
		if (blink_t > 0) {
			unblink_ms = millis() + blink_t;
		}
	}

	void checkBlink() {
		if (millis() > unblink_ms) {
			unblink_ms = 0;
			setColor(baseColor);
		}
	}

protected:
	PCF8574<Pi2c> & device;
	const uint8_t atbit;
	const uint8_t baseColor;
	unsigned long unblink_ms;
};

constexpr uint8_t buttonStateI2CAdr = 0x38;
constexpr uint8_t adcEnableI2CAdr = 0x39;
constexpr uint8_t adcI2CAdr = 0x48;

constexpr uint8_t debounceDelay_ms = 10;
constexpr uint16_t longPressTime_ms = 500;
constexpr uint8_t pedalChangeMinTime_ms = 50;

PCF8574<Pi2c> buttonStates(buttonStateI2CAdr);
PCF8574<Pi2c> adcEnable(adcEnableI2CAdr);
PCF8591<Pi2c> adc(adcI2CAdr);

Lamp8574 statusLamp(adcEnable, 4);


/*

#define LATCH(X) ((config::mode_t)(X|mod::latch))

struct button buttons[] = {
	{{ 0, mod::ctrl, 32, 127,		mod::latch, 0, 0}},
	{{ 0, mod::note, 33, 80,			mod::latch, 0, 0}},
	{{ 0, LATCH(mod::ctrl), 34, 80,	mod::ctrl, 34, 120}},
	{{ 0, mod::prog, 35, 33,			mod::latch, 0, 0}},
	{{ 0, mod::ctrl, 36, 120,		mod::latch, 0, 0}},
	{{ 0, mod::note, 37, 80,			LATCH(mod::keypress), 37, 100}},
	{{ 0, mod::start, 38, 120,		mod::latch, 0, 0}},
	{{ 0, mod::nieko, 39, 120,		mod::latch, 0, 0}},
};
constexpr uint8_t nButtons = sizeof(buttons)/sizeof(button);
using btst8 = button::state_t;
struct pedal pedals[] = {
	{{ 0, mod::ctrl, 0, 127, 85,	0, 1, mod::ctrl, 0, 127,
									{12, 0, 0, 0, 0, 0, 0, 0} }},
	{{ 0, mod::ctrl, 0, 127, 86, 	mod::all_btn,	mod::btn_chn, mod::ctrl, 0, 127,
									{86, 86, 86, 86, 86, 86, 86, 86} }},
	{{ 0, mod::keypress, 0, 127, 0, mod::all_btn,	mod::btn_chn, mod::bend, 0, 127,
									{mod::nc, 0, mod::nc, mod::nc, mod::nc, 0, mod::nc, mod::nc} }}
};
constexpr uint8_t nPedals = sizeof(pedals)/sizeof(pedal);
*/


/*!
 * our main hardware handler. responsible for various things on the I2C bus, and the UART.
 *	- foot buttons
 *	- expression pedals
 *	- hardware MIDI
 *	- maybe eventually cv in and out
 *	- maybe some status leds
 *	- maybe other analog voltage measuremnets like LDRs
 *	- other things like accelerometers, are possibly connected wirelessly, and converted to a wireless stream by MC
 */
BlooGPIO::BlooGPIO(msg::q_t& _inQ, msg::q_t& _midiOutQ)
	: inQ(_inQ), midiOutQ(_midiOutQ), isRunning(false)
{
}

bool BlooGPIO::start() {
	if (!isRunning.exchange(true)) {
		hwThread = std::thread([this]() { hwRunner(); });
	}
	return isRunning;
}

/*!
 * stop the worker thread and wait until it completes. then clean up the dongle
 */
void BlooGPIO::stop()
{
	if (isRunning.exchange(false)) {
		inQ.disableWait();
		inQ.enable(false);
		if (hwThread.joinable()) {
			hwThread.join();
		}
	}
}

 void BlooGPIO::sendButtonOnMidi(uint8_t typ, uint8_t chan, uint8_t v1, uint8_t v2) {
	uint8_t cmd = mod::nieko;
	switch (typ) {
		case mod::prog:
			cmd = midi::prog;
			break;
		case mod::ctrl:
			cmd = midi::ctrl;
			break;
		case mod::keypress:
			cmd = midi::keyPress;
			break;
		case mod::chanpress:
			cmd = midi::chanPress;
			break;
		case mod::note:
			cmd = midi::noteOn;
			break;
		case mod::bend:
			cmd = midi::bend;
			break;
		case mod::start:
			cmd = midi::start;
			break;
		case mod::stop:
			cmd = midi::stop;
			break;
	}
	if (cmd) {
		midiOutQ.push(std::make_shared<msg::MidiMsg>(uint8_t(cmd | chan), v1, v2)); // send it also to the viirtual ports
		sendUARTMidi(cmd|chan, v1, v2); // send that to an actual midi device
		statusLamp.setColor(Lamp8574::red, 100);
	}
};

void BlooGPIO::sendButtonOffMidi(uint8_t typ, uint8_t chan, uint8_t v1) {
	uint8_t cmd = mod::nieko;
	uint8_t v2 = 0;
	switch (typ) {
		case mod::ctrl:
			cmd = midi::ctrl | chan;
			break;
		case mod::bend:
			cmd = midi::bend | chan;
			v1 = 0x20;
			break;
		case mod::note:
			cmd = midi::noteOff | chan;
			break;
		case mod::start:
			cmd = midi::stop;
			break;
		case mod::stop:
			cmd = midi::start;
		break;
	}
	if (cmd) {
		midiOutQ.push(std::make_shared<msg::MidiMsg>(uint8_t(cmd | chan), v1, v2)); // send it also to the viirtual ports
		sendUARTMidi(cmd|chan, v1, v2); // send that to an actual midi device
		statusLamp.setColor(Lamp8574::red, 100);
	}
}


void BlooGPIO::checkPanelButton(const uint8_t which, const bool pressed) {

	long theTime = millis();
	
	auto & b{buttons[which]};

	if (pressed != b.lastPressState) {
		b.lastPressState = pressed;
		b.lastBounceTime_ms = theTime; //set the current time
	}
	if (theTime > debounceDelay_ms + b.lastBounceTime_ms) {
		if (pressed) {
			if (!(b.state & btst8::pressed)) {
				b.state |= btst8::pressed;
				b.pressedTime_ms = theTime;
				const auto btype = b.mode & mod::type;
				b.active_type = btype;

				if (b.mode & mod::latch){
					b.state |= (btst8::latched|btst8::latching);
				}
				sendButtonOnMidi(btype, b.channel, b.val1, b.val2);
				//lamps.setLamp(which, (b.state & btst8::latched)? lmpBtLatchCol: lmpBtDownCol);
			} else if (!(b.state & btst8::longPressed) && b.pressedTime_ms + longPressTime_ms < theTime) {
				b.state |= btst8::longPressed;
				const auto btype = b.long_mode & mod::type;
				if (b.long_mode & mod::latch){
					b.state |= (btst8::latched|btst8::latching);
				}
				if (btype > b.active_type) {
					b.active_type = btype;
					b.active_val1 = b.long_val1;
				}
				sendButtonOnMidi(btype, b.channel, b.long_val1, b.long_val2);
				//lamps.setLamp(which, (b.state & btst8::latched)? lmpBtLatchCol: lmpBtDownCol);
			}
		} else {
			if (b.state & btst8::pressed) {
				if (b.state & btst8::latching) {
					b.state &= ~btst8::latching;
				} else if (b.state & btst8::latched){
					b.state &= ~btst8::latched;
				}
				b.state &= ~btst8::pressed;	
				const auto btype = b.active_type & mod::type;

				sendButtonOffMidi(btype, b.channel, b.val1);
				//lamps.setLamp(which, b.state & btst8::latched? lmpBtLatchCol: lmpBtOpenCol);
			}
		}
	}
}


/*!
 *
 */
uint8_t BlooGPIO::checkShiftButton(uint8_t shift_btn, uint8_t alt_chan, uint8_t &chan, uint8_t alt_ctrl[], uint8_t &ctrlVal1) {
	if (shift_btn == mod::no_shft) {
		return mod::no_shft;
	}
	if (shift_btn != mod::all_btn) {
		if (shift_btn < buttons.size() && buttons[shift_btn].active()) {
			ctrlVal1 = alt_ctrl[0];
			chan = alt_chan;
			return shift_btn;
		}
	}
	for (uint8_t i=0; i<buttons.size(); i++) {
		if (alt_ctrl[i] != mod::nc && buttons[i].active()) {
			ctrlVal1 = alt_ctrl[i];
			chan = (alt_chan == mod::btn_chn? alt_chan: buttons[i].channel);
			return i;
		}
	}
	return mod::no_shft;
}


/*!
 * we might need some low pass filtering here.
 * 'cv' is the raw voltage reading in the range [0, 255]
 */
void BlooGPIO::checkPedal(const uint8_t which, const uint8_t cv) {
	auto &ped{pedals[which]};
	int8_t chg = cv - ped.lastCV;

	if (chg != 0) {
		auto theTime = millis();
		auto potChangeT_ms = theTime - ped.lastChangeTime_ms;
		if (potChangeT_ms > pedalChangeMinTime_ms) {
			ped.lastCV2 = ped.lastCV;
			ped.lastChangeTime_ms = theTime;

			uint8_t cmd = mod::nieko;
			uint8_t mode = ped.mode;
			uint8_t ctrlVal2;
			uint8_t ctrlVal1 = ped.ctrl;
			uint8_t chan = ped.chan;
			float factor = ped.factor;
			
			if (checkShiftButton(ped.shift_btn, ped.alt_chan, chan, ped.alt_ctrl, ctrlVal1) != mod::no_shft) {
				mode = ped.alt_mode;
				factor = ped.alt_factor;
			}
			
			if (mode != mod::bend) {
				ctrlVal2 = static_cast<uint8_t>(cv * factor); // factor <= 0.5
			} else {
				uint16_t full_bend = cv * factor; // bend has a large factor
				ctrlVal2 = static_cast<uint8_t>(full_bend & 0x7f);
				ctrlVal1 = static_cast<uint8_t>((full_bend >> 7) & 0x7f);
			}
			switch (mode) {
				case mod::ctrl:
					cmd = midi::ctrl;
					break;
				case mod::chanpress:
					cmd = midi::chanPress;
					break;
				case mod::keypress:
					cmd = midi::keyPress;
					break;
				case mod::bend:
					// range is 0–16383, with center value 8192
					cmd = midi::bend;
					break;
			}
			if (cmd) {
#ifdef ENABLE_SPI
				spiMidiOut.addToBuf((cmd|chan), ctrlVal1, ctrlVal2);
#endif
				//midiA.send(static_cast<midi::MidiType>(cmd), ctrlVal1, ctrlVal2, chan); // send that to an actual midi device
				if (potChangeT_ms > 200) {
					statusLamp.setColor(Lamp8574::red, 50);
				}
			}
		}
	}
}


/*!
 * local hardware processing loop
 */
void BlooGPIO::hwRunner()
{
#if defined(HAS_WIRING_PI) && defined(HAS_SERIAL_MIDI)
	// we also need to adjust clock multipliers in the pi to bring this down to 32k
	uartFd = serialOpen("/dev/ttyAMA0", 38400);
#endif
	while (isRunning) {
		// check our stat lamp. Maybe it's blink is off and it will be reset to the default color
		statusLamp.checkBlink();

		/*
		if (do_close) {
			sendButtonOffMidi(close_mode, close_chan, close_val1);
			do_close = false;
		}
		*/

		/*
		if (set_tempo) {
			globalTempo = incoming_tempo;
			beatLength_s = 60 / globalTempo;
			set_tempo = false;
		}*/

		auto theTime = millis();

		// process incoming midi data, sending anything useful onto the main midi handler
#if defined(HAS_WIRING_PI) && defined(HAS_SERIAL_MIDI)
		const auto [avail, cmd, v1, v2] = recvUARTMidi();
		if (avail) {
			switch (cmd & 0xf0) {
				case midi::noteOn:
				case midi::noteOff:
				case midi::ctrl:
				case midi::prog:
				case midi::bend:
				case midi::chanPress:
				case midi::keyPress:
				case midi::start:
				case midi::stop:
				case midi::cont:
					midiOutQ.push(std::make_shared<msg::MidiMsg>(cmd, v1, v2)); // send it also to the viirtual ports
					statusLamp.setColor(Lamp8574::blue, 50);
					break;

				case midi::clock:
#ifdef INTERNAL_CLOCK
					if (isMidiMasterClock) {
						isMidiMasterClock = false;
						cancelMidiMasterClock = true;
					}
					if (lastTickMillis == 0) {
						lastTickMillis = theTime;
					} else {
						auto tickDur = theTime - lastTickMillis;
						uint8_t nCached;
						if (clockCacheHead < 0) {
							clockCacheTail = clockCacheHead = 0;
							clockDurCache[0] = runningDurTotal = tickDur;
							nCached = 1;
						} else {
							auto f = clockCacheTail - clockCacheHead - 1;
							if (f == 0 || f == -nCachedClockTicks) { // full
								runningDurTotal -= clockDurCache[clockCacheTail];
								if (++clockCacheTail >= nCachedClockTicks) clockCacheTail = 0;
								nCached = nCachedClockTicks;
							} else {
								if (f < 0) f += nCachedClockTicks; 
								nCached = nCachedClockTicks - f;
							}
							if (++clockCacheHead >= nCachedClockTicks) clockCacheHead = 0;
							clockDurCache[clockCacheHead] = tickDur;
							runningDurTotal += tickDur;
						}
						auto aveBeatLen_ms = runningDurTotal;
						if (nCached != 24) aveBeatLen_ms *= 24.0 / nCached;
						globalTempo = 60000.0 / aveBeatLen_ms; 
					}
#endif
					break;
			}
		}
#endif

#ifdef INTERNAL_CLOCK
		if (startMidiMasterClock) {
			isMidiMasterClock = true;
			nextTickMillis = theTime;
		}

		if (isMidiMasterClock) {
			if (theTime >= nextTickMillis) {
				midiA.sendRealTime(midi::Clock);
				nextTickMillis = nextTickMillis + (beatLength_s * 1000 / 24.0);
			}
		}
#endif

		const uint8_t currentButtons = buttonStates.getInputs();
		uint8_t mask = 1;
		for (uint8_t i = 0; i < buttons.size(); ++i) {
			const bool pressed = (currentButtons & mask) != 0;
			checkPanelButton(i, pressed);
			mask <<= 1;
		}
		const uint8_t currentAdcEnables = (~adcEnable.getInputs()) & 0x7;
		mask = 1;
		for (uint8_t i = 0; i < 3; ++i) {
			//		uint8_t value0=adc.adRead(i); print_hex_byte(value0, false);
			if (currentAdcEnables & mask) {
				auto val = adc.adRead(i);
				checkPedal(i, val);
			}
			mask <<= 1;
		}

		// Now send everything that has come from the spi bus to an actual midi device
		// This includes all the button presses, accelerometers and pedals
#if defined(HAS_WIRING_PI) && defined(HAS_SERIAL_MIDI)
		while (!inQ.empty()) {
			auto optMsg = inQ.front();
			if (optMsg.second) {		
				const auto &msg = optMsg.first;
				try {
					if (msg->type == msg::typ::midi) {
						const auto &mmsg = std::reinterpret_pointer_cast<msg::MidiMsg>(msg)->midi;
						sendUARTMidi(mmsg.cmd, mmsg.val1, mmsg.val2);
						statusLamp.setColor(Lamp8574::red, 50);
					} else if (msg->type == msg::typ::midi_list) {
						const auto &mlmsg = std::reinterpret_pointer_cast<msg::MidiListMsg>(msg)->midi;
						for (const auto &m: mlmsg) {
							sendUARTMidi(m.cmd, m.val1, m.val2);
							statusLamp.setColor(Lamp8574::red, 50);
						}
					}
				} catch (const std::exception& e) {
					error("MidiWorker() gets exception: {}", e.what());
				}
				inQ.remove(optMsg.first);
			}
		}
#endif

		/*

		if (rescan_requested) { // look around for a new i2c device
			rescan_requested = false;
			if (accelerometer.begin())
			{
				accelerometer.setRange(adxl345::kRange16G);
			}
		}*/
	}
#if defined(HAS_WIRING_PI) && defined(HAS_SERIAL_MIDI)
	serialClose(uartFd);
#endif
	
}

void BlooGPIO::sendUARTMidi(uint8_t cmd, uint8_t v1, uint8_t v2)
{
#ifdef HAS_WIRING_PI
	serialPutchar(uartFd, cmd);
#endif
	switch (cmd & 0xf0) {
	case midi::noteOn:
	case midi::noteOff:
	case midi::ctrl:
	case midi::bend:
	case midi::keyPress:
#ifdef HAS_WIRING_PI
		serialPutchar(uartFd, v1);
		serialPutchar(uartFd, v2);
#endif
		break;

	case midi::chanPress:
	case midi::prog:
#ifdef HAS_WIRING_PI
		serialPutchar(uartFd, v1);
#endif
		break;

	case midi::start:
	case midi::stop:
	case midi::cont:
		break;

	case midi::clock:
		break;
	}

}

std::tuple<bool, uint8_t, uint8_t, uint8_t> BlooGPIO::recvUARTMidi()
{
	uint8_t cmd = 0, v1 = 0, v2 = 0;
	int avail = 0;

#ifdef HAS_WIRING_PI
	avail = serialDataAvail(uartFd);
#endif
	if (hasIncompleteCmd) {
		cmd = currentCmd;
	} else {
		if (avail == 0) {
			return {false, 0, 0, 0};
		}
#ifdef HAS_WIRING_PI
		cmd = serialGetchar(uartFd);
		--avail;
#endif
	}
	switch (cmd & 0xf0) {
	case midi::noteOn:
	case midi::noteOff:
	case midi::ctrl:
	case midi::bend:
	case midi::keyPress:
		if (avail >= 2) {
#ifdef HAS_WIRING_PI
			v1 = serialGetchar(uartFd);
			v2 = serialGetchar(uartFd);
			hasIncompleteCmd = false;
			return {true, cmd, v1, v2};
#else
#endif
		}
		break;

	case midi::chanPress:
	case midi::prog:
		if (avail >= 1) {
#ifdef HAS_WIRING_PI
			v1 = serialGetchar(uartFd);
			hasIncompleteCmd = false;
			return {true, cmd, v1, 0};
#else
#endif
		}
		break;

	case midi::start:
	case midi::stop:
	case midi::cont:
		hasIncompleteCmd = false;
		return {true, cmd, 0, 0};

	case midi::clock:
		hasIncompleteCmd = false;
		return {true, cmd, 0, 0};
	}

	hasIncompleteCmd = true;
	currentCmd = cmd;
	return {false, 0, 0, 0};
}
