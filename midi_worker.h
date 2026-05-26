#pragma once

#include "message.h"

#include <atomic>
#include <thread>
#include <memory>

class RtMidiIn;
class RtMidiOut;

class MidiWorker
{
public:
	MidiWorker(msg::q_t &_spiInQ, msg::q_t &_oscInQ, msg::q_t& _midiInQ);
	~MidiWorker();

	void run();
	void stop();

	void scanPorts();
	void openPorts();

	bool hasVirtualPorts();
private:
	void runner();
	void sendMIDI(msg::midi_t m);
private:
	std::atomic<bool> isRunning;
	std::thread myThread;

	std::unique_ptr<RtMidiIn> midiIn;
	std::unique_ptr<RtMidiOut> midiOut;
	
	std::vector<std::string> midiInPorts;
	std::vector<std::string> midiOutPorts;
	
	msg::q_t& hwInQ; //!< from locally connected hardware on the pi's hardware port
	msg::q_t& oscInQ; //!< stretch goal.
	//! we might need some special here also for bluetooth devices if a custom bt midi device isn't recognised by the system
	msg::q_t& midiOutQ;
};