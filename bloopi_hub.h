#pragma once

#include "message.h"
#include "midi_worker.h"

#include <memory>

#if defined(HAS_OSC) || defined(HAS_WS)
#include <boost/asio/io_service.hpp>
#endif

#ifdef HAS_WS
#include "wsapi_cmd.h"

class WSApiHandler;
class WSServer;
class WSApiWorker;
#endif

#ifdef HAS_OSC
class OSCServer;
class OSCWorker;

namespace oscapi {
	class Processor;
}
#endif

class BloopiHub
{
public:
	BloopiHub(std::string dst_osc_adr, uint16_t dst_osc_prt, uint16_t rcv_osc_port, uint16_t ws_port, uint16_t threadCount = 1);
	~BloopiHub();

	void run();
	void stop();

private:
#if defined(HAS_OSC) || defined(HAS_WS)
	boost::asio::io_service ioService;
#endif
#ifdef HAS_OSC
	std::shared_ptr<oscapi::Processor> oscParser; //!<< we should be able to get away with sharing the one
	std::unique_ptr<OSCServer> oscServer;
	std::unique_ptr<OSCWorker> oscWorker;
#endif
#ifdef HAS_WS
	std::shared_ptr<WSApiHandler> wsapiHandler;
	std::unique_ptr<WSServer> wsServer;
	std::unique_ptr<WSApiWorker> wsapiWorker;
#endif
	std::unique_ptr<MidiWorker> midiWorker;

	msg::q_t hwInQ; //!< local hardware inputs I2C and UART
	msg::q_t oscInQ; //!< OSC input Q. TODO: we might not have osc enabled for a while but we'll keep it around
	msg::q_t midiOutQ; //!< out q for midi worker
#ifdef HAS_WS
	wsapi::cmdq_t cmdQ;
#endif

	uint16_t threadCount;
};