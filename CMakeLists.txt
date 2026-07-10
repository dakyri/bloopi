cmake_minimum_required(VERSION 3.31)
set(CMAKE_CXX_STANDARD 17)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if (WIN32)
	message(STATUS "Windoes!")
elseif (UNIX)
	message(STATUS "Nix!")
	list(APPEND CMAKE_MODULE_PATH "/usr/local")
	message(STATUS "Cmake module path ${CMAKE_MODULE_PATH}")
else()
	message(STATUS "Build for ${CMAKE_SYSTEM_NAME}")

	if(CMAKE_SYSTEM_NAME MATCHES "Linux")
		list(APPEND CMAKE_MODULE_PATH "/usr/local")
	endif()
endif()
project(bloopi)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(HAS_WIRING_PI FALSE)

find_package(Boost 1.74 COMPONENTS program_options REQUIRED)
find_package(spdlog 1.15.2 REQUIRED)
find_package(nlohmann_json 3.6.0 REQUIRED)
find_package(RtMidi 7.0.0 REQUIRED)

if (UNIX)
	find_path(WIRING_PI_INC_PATH "wiringPi.h")
	find_library(WIRING_PI_LIB NAMES wiringPi)
	if ((WIRING_PI_INC_PATH STREQL "WIRING_PI_INC_PATH-NOTFOUND") OR (WIRING_PI_LIB STREQUAL "WIRING_PI_LIB-NOTFOUND"))
		message(STATUS "No wiring pi")
	else ()
		message(STATUS "Wiring at ${WIRING_PI_INC_PATH} for include and ${WIRING_PI_LIB} for lib")
		set(HAS_WIRING_PI TRUE)
	endif()
endif()

include_directories(include)

if (HAS_WIRING_PI)
	message(STATUS "Found WiringPi. Adding gpio code ...")
	set(EXTRA_SOURCES "bloo_gpio.cpp")
endif()

add_executable(${PROJECT_NAME}
	bloopi.cpp
	bloopi_hub.cpp
	midi_worker.cpp
	${EXTRA_SOURCES}
)

target_link_libraries(${PROJECT_NAME}
	Boost::program_options
	#Boost::coroutine
	#Boost::thread
	#Boost::regex
	#nlohmann_json::nlohmann_json
	spdlog::spdlog
	RtMidi::rtmidi
)

if (HAS_WIRING_PI)
	target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_WIRING_PI)
	target_include_directories(${PROJECT_NAME} PUBLIC ${WIRING_PI_INC_PATH})
	target_link_libraries(${PROJECT_NAME} ${WIRING_PI_LIB})
endif()
target_compile_definitions(${PROJECT_NAME} PUBLIC SPDLOG_HEADER_ONLY)


if (WIN32)
	target_include_directories(${PROJECT_NAME}
		PUBLIC	"C:/Program Files (x86)/RtMidi/include"
				"C:/Program Files (x86)/spdlog/include"
				"C:/Program Files (x86)/oscpp/include"
            	"C:/src/embedded/mw-embedded-lib/PCF8591mw"
            	"C:/src/embedded/mw-embedded-lib/PCF8574mw"
            	"C:/src/embedded/mw-embedded-lib/pi2c"
			   )
	add_definitions(-D_WIN32_WINNT=0x0A00 -DFMT_UNICODE=0) # 0x0A00 = win 10
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
else()
	target_include_directories(${PROJECT_NAME}
		PUBLIC	"${CMAKE_CURRENT_SOURCE_DIR}/../mw-embedded-lib/PCF8591mw"
				"${CMAKE_CURRENT_SOURCE_DIR}/../mw-embedded-lib/PCF8574mw"
				"${CMAKE_CURRENT_SOURCE_DIR}/../mw-embedded-lib/pi2c"
	)
endif()


