/*
 Name:		esp8266_xsh.ino
 Created:	6/3/2019 5:35:53 PM
 Author:	Claudiu Neamtu
*/


#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <TickerScheduler.h>
#include <FastLED.h>
#include <ESP8266WiFi.h>


#define LED_DATA_PIN D5

#define DNS_PORT 53
#define HTTP_PORT 80
#define WEB_SOCKET_PORT 81
#define MQTT_PORT 1883
#define MQTT_SSL_PORT 8883

#define WEB_SOCKET_URL "/ws"

#define MQTT_HOST "mqtt.com"

#define BUTTON_LONG_PRESS 4000
#define BUTTON_SHORT_PRESS 100

// R"()" -> raw literal - google it
PROGMEM char INDEX_HTML[] = "<!DOCTYPE html>\n<html>\n<head>\n\t<title>XSH</title>\n</head>\n<body>\nSettings Page<br>\n<form action=\"/set_wifi_credentials\" method=\"POST\">\nSSID:<input name=\"ssid\" type=\"text\">\n<br>\nPassword:<input name=\"password\" type=\"text\">\n<br>\n<button type=\"sumbit\">Save</button>\n</form>\n\n</body>\n</html>";
PROGMEM char JAVASCRIPT_JS[] = R"()";
PROGMEM char STYLE_CSS[] = R"()";


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
	AsyncWebServer* webServer;
	AsyncWebSocket* webSocketServer;

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

	void onFiWiScanRequest();

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
	void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
	void onWebSocketTextDataEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, String data, size_t len);

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
	this->webServer = new AsyncWebServer(HTTP_PORT);
	this->webSocketServer = new AsyncWebSocket(WEB_SOCKET_URL);
	this->taskScheduler = new TickerScheduler(5);
}


void XSH_Device::start() {

	pinMode(_button_pin, INPUT_PULLUP);
	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(led, 1);
	setLedColor(CRGB::Black);

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
	//webSocketServer->begin();
}


// This event fires up when device is in config mode
void XSH_Device::configModeLoop() {
	dnsServer->processNextRequest();
}


// This event fires up when device end config mode
void XSH_Device::configModeStop() {
	if (_debug)
		Serial.println("Stop config mode");

	setLedColor(CRGB::Black);

	//webServer->stop();
	dnsServer->stop();
	//webSocketServer->close();

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

	webServer->onNotFound([this](AsyncWebServerRequest* request) {
		request->redirect("/");
	});

	webServer->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
		request->send_P(200, "text/html", INDEX_HTML);
	});

	webServer->on("/style.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
		request->send_P(200, "text/html", STYLE_CSS);
	});

	webServer->on("/javascript.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
		request->send_P(200, "text/html", JAVASCRIPT_JS);
	});

	webServer->on("/set_wifi_credentials", HTTP_POST, [this](AsyncWebServerRequest* request) {
		if (request->hasArg("ssid") && request->hasArg("password")) {
			String ssid = request->arg("ssid");
			String password = request->arg("password");

			//WiFi.begin(ssid, password);
			request->send(200, "text/html", "ok");

			if (_debug) {
				Serial.println("SSID set to: " + ssid);
				Serial.println("Password set to: " + password);
			}
		}
		else {
			request->send(400, "text/html", "bad request");
		}
	});


}

void XSH_Device::setWebSocketEventHandlers() {
	webSocketServer->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
		onWebSocketEvent(server, client, type, arg, data, len);
	});
	webServer->addHandler(webSocketServer);
}


void XSH_Device::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {

	switch (type) {
	case WS_EVT_CONNECT:
		Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
		client->printf("Hello Client %u :)", client->id());
		client->ping();
		break;

	case WS_EVT_DISCONNECT:
		Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
		break;

	case WS_EVT_ERROR:
		Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
		break;

	case WS_EVT_PONG:
		Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
		break;

	case WS_EVT_DATA:
		AwsFrameInfo* info = (AwsFrameInfo*)arg;
		String message = "";
		if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) { //the whole message is in a single frame and we got all of it's data
			for (size_t i = 0; i < info->len; i++)
				message += (char)data[i];
			onWebSocketTextDataEvent(server, client, message, len);
		}
		break;
	}

}


void XSH_Device::onWebSocketTextDataEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, String data, size_t len){
	DynamicJsonDocument request_doc(2024);
	deserializeJson(request_doc, data);

	Serial.println(data);

	String event = request_doc["event"];
	
	Serial.println(event);

	if (event == "scan_wifi_networks") {
		onFiWiScanRequest();
		return;
	}
	if (event == "set_wifi_credentials") {
		const char *ssid = request_doc["ssid"];
		const char *password = request_doc["password"];

		if (ssid != NULL && password != NULL) {
			WiFi.begin(ssid, password);
			client->text("success_message");
		}
		return;
	}
	if (event == "set_device_name") {
		const char* name = request_doc["name"];

		if (name != NULL) {
			WiFi.hostname(name);
			WiFi.softAP(name);

			client->text("success_message");
		}
		return;
	}
	if (event == "set_wifi_advenced") {
		String s_local_ip = request_doc["local_ip"];
		String s_gate_way = request_doc["gateway"];
		String s_subnet = request_doc["subnet"];

		if (s_local_ip != NULL && s_gate_way != NULL && s_subnet != NULL) {

			IPAddress local_ip = stringToIpAdress(s_local_ip);
			IPAddress gateway = stringToIpAdress(s_gate_way);
			IPAddress subnet = stringToIpAdress(s_subnet);

			WiFi.config(local_ip, gateway, subnet);

			client->text("success_message");
		}
		return;
	}

}


void XSH_Device::onFiWiScanRequest() { //scan for available wifi networks and send response to web socket client
	if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
		WiFi.scanNetworksAsync([&](int networks) {
			DynamicJsonDocument doc(2024);
			doc["event"] = "scan_wifi_networks";

			String ssid;
			uint8_t encryptionType;
			int32_t rssi;
			uint8_t* bssid;
			int32_t channel;
			bool isHidden;

			JsonArray ssidArray = doc.createNestedArray("ssid");
			JsonArray encryptionTypeArray = doc.createNestedArray("encryption_type");
			JsonArray rssiArray = doc.createNestedArray("rssi");
			JsonArray bssidArray = doc.createNestedArray("bssid");
			JsonArray chanelArray = doc.createNestedArray("chanel");
			JsonArray isHidenArray = doc.createNestedArray("is_hiden");

			for (int this_network = 0; this_network < networks; this_network++) {
				WiFi.getNetworkInfo(this_network, ssid, encryptionType, rssi, bssid, channel, isHidden);
				ssidArray.add(ssid);
				/*
				Function returns a number that encodes encryption type as follows:
				* 5 : ENC_TYPE_WEP - WEP 
				* 2 : ENC_TYPE_TKIP - WPA / PSK 
				* 4 : ENC_TYPE_CCMP - WPA2 / PSK 
				* 7 : ENC_TYPE_NONE - open network 
				* 8 : ENC_TYPE_AUTO - WPA / WPA2 / PSK
				*/
				encryptionTypeArray.add(encryptionType);
				rssiArray.add(rssi);
				//bssidArray.add(bssid); // TODO: transform mac adress from pointer to string
				chanelArray.add(channel);
				isHidenArray.add(isHidden);
			}

			String response;
			serializeJson(doc, response);
			//Serial.println(response);

			webSocketServer->textAll(response);
		}, true);
	}
}


IPAddress stringToIpAdress(const char *string) {
	unsigned short a, b, c, d;
	sscanf(string, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
	return IPAddress(a, b, c, d);
}


IPAddress stringToIpAdress(String string) {
	return stringToIpAdress(string.c_str());
}



XSH_Device device;


// the setup function runs once when you press reset or power the board
void setup() {
	Serial.begin(115200);
	device.init();
	device.start();
}

// the loop function runs over and over again until power down or reset
void loop() {
	device.loop();
}

