#include "bloopi_hub.h"

#ifdef HAS_OSC
#include "osc_handler.h"
#include "osc_server.h"
#include "osc_worker.h"
#endif
#ifdef HAS_WS
#include "wsapi_handler.h"
#include "wsapi_worker.h"
#include "ws_server.h"
#endif
#include "spdlog/spdlog.h"

using spdlog::info;
using spdlog::debug;


/*!
 * \class BloopiHub
 * main wrapper for various service handlers: MIDI, hardware, OSC, and a web socket for control commands.
 * owns all the shared data structures (queues, results map), the server, the io context and the worker.
 */

/*!
 * create our hub
 *  \param serverPort uint16_t what is says on the box
 *	\param threadCount uint16_t number of threads to launch. if 0, we'll make a reasonable estimate
 */
BloopiHub::BloopiHub(std::string dst_osc_adr, uint16_t dst_osc_prt, uint16_t rcv_osc_port, uint16_t ws_port, uint16_t threadCount)
	: threadCount(threadCount > 0 ? threadCount : 1)
{
	info("Bloopi servers running on {} threads: OSC server on port {}, WS server on port {}.", threadCount, rcv_osc_port, ws_port);
#ifdef HAS_OSC
	oscParser = std::make_shared<oscapi::Processor>(spiInQ);
	oscServer = std::make_unique<OSCServer>(ioService, rcv_osc_port, oscParser);
	oscServer->set_current_destination(dst_osc_adr, dst_osc_prt);
	oscWorker = std::make_unique<OSCWorker>(*oscServer.get(), oscInQ);
#endif
#ifdef HAS_WS
	auto const ws_address = asio::ip::make_address("ws:://localhost");
	auto const ws_endpoint = tcp::endpoint(ws_address, ws_port);
	wsapiHandler = std::make_shared<WSApiHandler>(spiInQ, oscInQ, cmdQ, wsapi::results_t()); // TODO: really not sure what to do with these results
	wsServer = std::make_unique<WSServer>(ioService, ws_endpoint, wsapiHandler);
	wsapiWorker = std::make_unique<WSApiWorker>(cmdQ, wsapi::results_t());
#endif
	midiWorker = std::make_unique<MidiWorker>(gpioInQ, oscInQ, midiOutQ);
}

BloopiHub::~BloopiHub() = default;

/*!
 * sets up the server, starts the worker thread, and runs the io context on a thread pool.
 * does not return unless we've been specifically cancelled.
 */
void BloopiHub::run()
{
	gpioInQ.disableWait();
	oscInQ.enableWait();
#ifdef HAS_WS
	cmdQ.enableWait();
#endif
#ifdef HAS_OSC	
	oscServer->start();
	oscWorker->run();
#endif
#ifdef HAS_WS
	wsServer->start();
#endif
	info("Bloopi::run(): Servers started and worker running ;)");
#if defined(HAS_OSC) || defined(HAS_WS)
#ifdef SINGLE_THREADED_IO
	ioService.run();
#else
	std::vector<std::thread> ioThreads;
	for (int i = 0; i < threadCount; ++i) {
		ioThreads.emplace_back([this]() { ioService.run(); });
	}

	for (auto& thread : ioThreads) {
		if (thread.joinable()) thread.join();
	}
#endif
	info("Bloopi::run(): io_context threads joined and completed. :o");
#ifdef HAS_OSC
	oscWorker->stop();
#endif
#endif
	info("Bloopi::run() shut down successfully. :)");
}

/*!
 * just finish! probably not safely. not sure if we need this even
 */
void BloopiHub::stop()
{
#if defined(HAS_OSC) || defined(HAS_WS)
	ioService.stop(); // should be posted perhaps?
	// it would be polite to wait for all those loose threads in the local ioThreads vector. TODO: perhaps make the vector of threads a member so we can do that.
#endif
#ifdef HAS_OSC
	oscWorker->stop();
#endif
}