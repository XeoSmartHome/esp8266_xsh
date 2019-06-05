/*
 Name:		esp8266_xsh.ino
 Created:	6/3/2019 5:35:53 PM
 Author:	Claudiu Neamtu
*/


#include <WebSocketsServer.h>
#include <TickerScheduler.h>
#include <FastLED.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>


#define LED_DATA_PIN D5

#define DNS_PORT 53
#define HTTP_PORT 80
#define WEB_SOCKET_PORT 81
#define MQTT_PORT 1883
#define MQTT_SSL_PORT 8883

#define MQTT_HOST "mqtt.com"

#define BUTTON_LONG_PRESS 4000
#define BUTTON_SHORT_PRESS 100

PROGMEM char INDEX_HTML[] = "<!DOCTYPE html>\n<html>\n<head>\n\t<title>XSH</title>\n</head>\n<body>\nSettings Page<br>\n<form action=\"/set_wifi_credentials\" method=\"POST\">\nSSID:<input name=\"ssid\" type=\"text\">\n<br>\nPassword:<input name=\"password\" type=\"text\">\n<br>\n<button type=\"sumbit\">Save</button>\n</form>\n\n</body>\n</html>";
PROGMEM char JAVASCRIPT_JS[] = "";
PROGMEM char STYLE_CSS[] = "";


IPAddress accesPointIp(1, 1, 1, 1);
IPAddress NET_MASK(255, 255, 255, 0);


class XSH_Device {
public:
	XSH_Device();
	~XSH_Device();

	void setName(const String name);
	String getName();

	void setLedPin(const uint8_t pin);
	uint8_t getLedPin();

	void setButtonPin(const uint8_t pin);
	uint8_t getButtonPin();

	void init();
	void start();
	void loop();

protected:
	CRGB* led;
	DNSServer* dnsServer;
	ESP8266WebServer* webServer;
	WebSocketsServer* webSocketServer;

	void setLedColor(CRGB color);

	// derivable methods:
	void handleButtonShortPress() {};

private:
	String _name = "Device";
	bool _debug = true;
	uint8_t _led_pin = D5;
	uint8_t _button_pin = D8;
	bool _config_mode_on = false;

	int _last_color;

	bool _button_last_state = false;
	unsigned long _button_press_moment;

	bool buttonIsPressed();
	void checkForButtonStateChanges();
	void handleButtonLongPress();

	TickerScheduler* taskScheduler;
	//String scanWiFiNetworks();

	void configModeStart();
	void configModeLoop();
	void configModeStop();

	void setWebServerEventHandlers();

	void setWifiEventHandlers();
	void onStationModeConnected(const WiFiEventStationModeConnected& event); // This event fires up when the chip has passed the password check of the connection part.
	void onStationModeDisconnected(const WiFiEventStationModeDisconnected&); // This event fires up when the chip detects that it is no longer connected to an AP
	void onStationModeAuthModeChanged(const WiFiEventStationModeAuthModeChanged&); // Fires up when the chip can’t log in with previously saved credentials.
	void onStationModeGotIP(const WiFiEventStationModeGotIP&); // This event fires up when the chip gets to its final step of the connection: getting is network IP address.
	void onStationModeDHCPTimeout(void); // This event fires when the microcontroller can’t get an IP address, from a timeout or other errors.

	void setWebSocketEventHandlers();
	void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

};


// PUBLIC: -------------------------------------------------------------------------------------------------------------------------------------

XSH_Device::XSH_Device() {}


XSH_Device::~XSH_Device() {}


void XSH_Device::setName(const String name) {
	this->_name = name;
}


String XSH_Device::getName() {
	return this->_name;
}


void XSH_Device::setLedPin(const uint8_t pin) {
	this->_led_pin = pin;
}


uint8_t XSH_Device::getLedPin() {
	return this->_led_pin;
}


void XSH_Device::setButtonPin(const uint8_t pin) {
	this->_button_pin = pin;
}


uint8_t XSH_Device::getButtonPin() {
	return this->_button_pin;
}


void XSH_Device::init() {
	this->led = new CRGB[1];
	this->dnsServer = new DNSServer();
	this->webServer = new ESP8266WebServer(HTTP_PORT);
	this->webSocketServer = new WebSocketsServer(WEB_SOCKET_PORT);
	this->taskScheduler = new TickerScheduler(5);
}


void XSH_Device::start() {

	pinMode(_button_pin, INPUT_PULLUP);
	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(led, 1);
	setLedColor(CRGB::Black);

	WiFi.hostname(_name);
	char *name;
	_name.toCharArray(name, 128);
	WiFi.softAP(name);
	WiFi.softAPConfig(accesPointIp, accesPointIp, NET_MASK);
	delay(500);

	// set event handlers
	setWifiEventHandlers();
	setWebServerEventHandlers();
	setWebSocketEventHandlers();

	WiFi.mode(WIFI_STA);
	WiFi.begin();
}


void XSH_Device::loop() {
	checkForButtonStateChanges();
	if (_config_mode_on)
		configModeLoop();

	taskScheduler->update();

}


// PROTECTED: -------------------------------------------------------------------------------------------------------------------------------------

void XSH_Device::setLedColor(CRGB color) {
	led[0] = color;
	FastLED.show();
}



// PRIVATE: ---------------------------------------------------------------------------------------------------------------------------------------


// This event fires up when device begin config mode
void XSH_Device::configModeStart() {
	if (_debug)
		Serial.println("Start config mode");

	taskScheduler->add(0, 1000, [this](void *) {
		setLedColor(CRGB::Blue);
	}, nullptr, false);

	WiFi.mode(WIFI_AP_STA);
	WiFi.begin();

	dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
	dnsServer->setTTL(0);
	dnsServer->start(DNS_PORT, "*", accesPointIp);
	webServer->begin();
}


// This event fires up when device is in config mode
void XSH_Device::configModeLoop() {
	dnsServer->processNextRequest();
	webServer->handleClient();
	webSocketServer->loop();
}


// This event fires up when device close config mode
void XSH_Device::configModeStop() {
	if (_debug)
		Serial.println("Stop config mode");

	setLedColor(CRGB::Black);

	webServer->stop();
	dnsServer->stop();
	WiFi.mode(WIFI_STA);
}


void XSH_Device::setWifiEventHandlers() {
	static WiFiEventHandler _e1 = WiFi.onStationModeConnected([this](const WiFiEventStationModeConnected& event) {
		onStationModeConnected(event);
	});
	static WiFiEventHandler _e2 = WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected& event) {
		onStationModeDisconnected(event);
	});
	static WiFiEventHandler _e3 = WiFi.onStationModeAuthModeChanged([this](const WiFiEventStationModeAuthModeChanged& event) {
		onStationModeAuthModeChanged(event);
	});
	static WiFiEventHandler _e4 = WiFi.onStationModeGotIP([this](const WiFiEventStationModeGotIP& event) {
		onStationModeGotIP(event);
	});
	static WiFiEventHandler _e5 = WiFi.onStationModeDHCPTimeout([this]() {
		onStationModeDHCPTimeout();
	});
}


// This event fires up when the chip has passed the password check of the connection part.
void XSH_Device::onStationModeConnected(const WiFiEventStationModeConnected& event) {
	Serial.println("Conncted to: " + event.ssid);
}


// This event fires up when the chip detects that it is no longer connected to an AP
void XSH_Device::onStationModeDisconnected(const WiFiEventStationModeDisconnected& event) {
	Serial.println("Disconncted from: " + event.ssid);
}


// Fires up when the chip can’t log in with previously saved credentials.
void XSH_Device::onStationModeAuthModeChanged(const WiFiEventStationModeAuthModeChanged& event) {

}


// This event fires up when the chip gets to its final step of the connection: getting is network IP address.
void XSH_Device::onStationModeGotIP(const WiFiEventStationModeGotIP& event) {

}


// This event fires when the microcontroller can’t get an IP address, from a timeout or other errors.
void XSH_Device::onStationModeDHCPTimeout() {

}


bool XSH_Device::buttonIsPressed() {
	return digitalRead(_button_pin);
}


void  XSH_Device::checkForButtonStateChanges() {
	// trebuie reparata putin
	if (buttonIsPressed()) {
		if (!_button_last_state) {
			_button_last_state = true;
			_button_press_moment = millis();
		}
		else if (millis() - _button_press_moment > BUTTON_LONG_PRESS) {
			_button_press_moment = millis();
			handleButtonLongPress();
		}
	}
	else if (_button_last_state) {
		_button_last_state = false;
		unsigned long press_time = millis() - _button_press_moment;
		if (BUTTON_SHORT_PRESS < press_time && press_time < BUTTON_LONG_PRESS)
			handleButtonShortPress();
	}
}


void XSH_Device::handleButtonLongPress() {
	Serial.println("LONG PRESS");
	_config_mode_on = !_config_mode_on;

	if (_config_mode_on) {
		configModeStart();
	}
	else {
		configModeStop();
	}
}


void XSH_Device::setWebServerEventHandlers() {
	webServer->onNotFound([this]() {
		webServer->sendHeader("Location", "/", true);
		webServer->send(302, "text/plain", "");
	});

	webServer->on("/", HTTP_GET, [this]() {
		webServer->send_P(200, "text/html", INDEX_HTML);
	});

	webServer->on("/style.css", HTTP_GET, [this]() {
		webServer->send_P(200, "text/css", STYLE_CSS);
	});

	webServer->on("/javascript.js", HTTP_GET, [this]() {
		webServer->send_P(200, "text/javascript", JAVASCRIPT_JS);
	});

	webServer->on("/set_wifi_credentials", HTTP_POST, [this]() {
		if (webServer->hasArg("ssid") && webServer->hasArg("password")) {
			String ssid = webServer->arg("ssid");
			String password = webServer->arg("password");

			//WiFi.begin(ssid, password);
			webServer->send(200, "text/html", "ok");

			if (_debug) {
				Serial.println("SSID set to: " + ssid);
				Serial.println("Password set to: " + password);
			}
		}
		else {
			webServer->send(400, "text/html", "bad request");
		}
	});
}

void XSH_Device::setWebSocketEventHandlers() {
	webSocketServer->onEvent(this->onWebSocketEvent);
}


void XSH_Device::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {

	switch (type) {
	case WStype_DISCONNECTED:
		Serial.printf("[%u] Disconnected!\n", num);
		break;

	case WStype_CONNECTED:
		IPAddress ip = webSocketServer->remoteIP(num);
		Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

		// send message to client
		webSocketServer->sendTXT(num, "Connected");
		break;

	case WStype_TEXT:
		Serial.printf("[%u] get Text: %s\n", num, payload);

		if(webSocketServer->)

		break;

	case WStype_BIN:
		//pass
		break;

		// Fragmentation / continuation opcode handling
		// case WStype_FRAGMENT_BIN_START:
	case WStype_FRAGMENT_TEXT_START:
		//fragmentBuffer = (char*)payload;
		break;
	case WStype_FRAGMENT:
		//fragmentBuffer += (char*)payload;
		break;
	case WStype_FRAGMENT_FIN:
		//fragmentBuffer += (char*)payload;
		break;
	}

}





XSH_Device device;


// the setup function runs once when you press reset or power the board
void setup() {
	device.init();
	device.start();
}

// the loop function runs over and over again until power down or reset
void loop() {
	device.loop();
}

