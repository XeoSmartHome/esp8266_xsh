/*
 Name:		esp8266_xsh.ino
 Created:	6/3/2019 5:35:53 PM
 Author:	Claudiu Neamtu
*/

#include <FS.h>
#include <EEPROM.h>
#include <AsyncMqttClient.h>
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

/*#if ASYNC_TCP_SSL_ENABLED
//#define MQTT_SECURE true
//#define MQTT_SERVER_FINGERPRINT {0x7e, 0x36, 0x22, 0x01, 0xf9, 0x7e, 0x99, 0x2f, 0xc5, 0xdb, 0x3d, 0xbe, 0xac, 0x48, 0x67, 0x5b, 0x5d, 0x47, 0x94, 0xd2}
#define MQTT_PORT 8883
#else*/
#define MQTT_PORT 1883
//#endif
#define MQTT_SERVER_URL "broker.hivemq.com"

#define WEB_SOCKET_URL "/ws"

#define BUTTON_LONG_PRESS 4000
#define BUTTON_SHORT_PRESS 100


#define SUCCESS 1
#define FAIL 0

#define TASK_SCHEDULER_LED 0


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
	void setLedColor(CRGB color);

	// send mqtt message to cloud
	void sendMessage(const char* message);
	void sendMessage(String* message);

	// derivable methods:
	virtual void onButtonShortPress() = 0;
	virtual void onStart() = 0;
	virtual void onLoop() = 0;
	virtual void onMessage(char* payload, size_t len) = 0;

private:
	CRGB* led;
	DNSServer* dnsServer;
	AsyncWebServer* webServer;
	AsyncWebSocket* webSocketServer;
	AsyncMqttClient* mqttClient;

	bool _debug = true;

	struct Settings{
		bool dhcp;
		IPAddress local_ip;
		IPAddress gateway;
		IPAddress subnet_mask;
		char device_name[WL_SSID_MAX_LENGTH + 1];
	};

	Settings _settings;
	void loadSettings();
	void saveSettings();

	// Led
	uint8_t _led_pin = D5;
	uint8_t _led_last_color;
	
	// Button
	uint8_t _button_pin = D8;
	bool _button_last_state = false;
	unsigned long _button_press_moment;
	bool buttonIsPressed();
	void checkForButtonStateChanges();
	void handleButtonLongPress();

	TickerScheduler* taskScheduler;

	// Config mode
	bool _config_mode_on = false;
	void configModeStart();
	void configModeLoop();
	void configModeStop();

	// WiFi handlers
	void setWifiEventHandlers();
	void onStationModeConnected(const WiFiEventStationModeConnected& event); // This event fires up when the chip has passed the password check of the connection part.
	void onStationModeDisconnected(const WiFiEventStationModeDisconnected&); // This event fires up when the chip detects that it is no longer connected to an AP
	void onStationModeAuthModeChanged(const WiFiEventStationModeAuthModeChanged&); // Fires up when the chip can’t log in with previously saved credentials.
	void onStationModeGotIP(const WiFiEventStationModeGotIP&); // This event fires up when the chip gets to its final step of the connection: getting is network IP address.
	void onStationModeDHCPTimeout(void); // This event fires when the microcontroller can’t get an IP address, from a timeout or other errors.

	// WebServer handlers
	void setWebServerEventHandlers();

	// WebSocket handlers
	void setWebSocketEventHandlers();
	void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
	void onWebSocketTextDataEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, String data, size_t len);
	void onFiWiScanRequest();

	// MQTTclient handlers 
	void setMqttEventHandlers();
	void onMqttConnect(bool sessionPresent);
	void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
	void onMqttSubscribe(uint16_t packetId, uint8_t qos);
	void onMqttUnsubscribe(uint16_t packetId);
	void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
	void onMqttPublish(uint16_t packetId);
};


// PUBLIC: -------------------------------------------------------------------------------------------------------------------------------------

XSH_Device::XSH_Device() {}


XSH_Device::~XSH_Device() {}


void XSH_Device::setName(const String name) {
	strcpy(this->_settings.device_name, name.c_str());
}


String XSH_Device::getName() {
	return this->_settings.device_name;
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
	this->mqttClient = new AsyncMqttClient();
}


void XSH_Device::start() {

	pinMode(_button_pin, INPUT_PULLUP);
	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(led, 1);
	setLedColor(CRGB::Black);

	SPIFFS.begin();

	// set event handlers
	setWifiEventHandlers();
	setMqttEventHandlers();
	setWebServerEventHandlers();
	setWebSocketEventHandlers();
	delay(1000);

	loadSettings();

	WiFi.hostname(_settings.device_name);
	WiFi.softAP(_settings.device_name);

	WiFi.softAPConfig(accesPointIp, accesPointIp, NET_MASK);
	delay(500);

	WiFi.mode(WIFI_STA);
	if (!_settings.dhcp) {
		WiFi.config(_settings.local_ip, _settings.gateway, _settings.subnet_mask, _settings.gateway);
	}
	WiFi.begin();

	onStart();
}


void XSH_Device::loop() {
	checkForButtonStateChanges();
	if (_config_mode_on)
		configModeLoop();

	onLoop();
	taskScheduler->update();
}


// PROTECTED: -------------------------------------------------------------------------------------------------------------------------------------


void XSH_Device::setLedColor(CRGB color) {
	led[0] = color;
	FastLED.show();
}


void XSH_Device::sendMessage(const char* message) {
	mqttClient->publish("", 2, false, message);
	// TODO: set topic
};


void XSH_Device::sendMessage(String* message) {
	sendMessage(message->c_str());
}


// PRIVATE: ---------------------------------------------------------------------------------------------------------------------------------------


void XSH_Device::loadSettings() {
	EEPROM.begin(1024);
	EEPROM.get(0, _settings);
	EEPROM.end();
}


void XSH_Device::saveSettings() {
	EEPROM.begin(1024);
	EEPROM.put(0, _settings);
	EEPROM.end();
}


// This event fires up when device begin config mode
void XSH_Device::configModeStart() {
	if (_debug)
		Serial.println("Start config mode");

	setLedColor(CRGB::Blue);
	_led_last_color = 0;
	taskScheduler->add(TASK_SCHEDULER_LED, 500, [this](void *) {
		if (_led_last_color)
			setLedColor(CRGB::Black);
		else
			setLedColor(CRGB::Blue);
		_led_last_color = !_led_last_color;
	}, nullptr, false);

	//WiFi.disconnect();
	WiFi.mode(WIFI_AP_STA);

	dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
	dnsServer->setTTL(0);

	dnsServer->start(DNS_PORT, "*", accesPointIp);
	webServer->begin();
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
	taskScheduler->remove(TASK_SCHEDULER_LED);

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
	mqttClient->disconnect();
	if (_config_mode_on) {
		webSocketServer->textAll(R"({"event":"wifi_disconnected"})");
	}
}


// Fires up when the chip can’t log in with previously saved credentials.
void XSH_Device::onStationModeAuthModeChanged(const WiFiEventStationModeAuthModeChanged& event) {
	if (_config_mode_on) {
		webSocketServer->textAll(R"({"event":"wifi_auth_fail"})");
	}
}


// This event fires up when the chip gets to its final step of the connection: getting is network IP address.
void XSH_Device::onStationModeGotIP(const WiFiEventStationModeGotIP& event) {
	if (_config_mode_on) {
		webSocketServer->textAll(R"({"event":"wifi_connected"})");
		mqttClient->connect();
	}else{
		setLedColor(CRGB::Green);
		taskScheduler->add(TASK_SCHEDULER_LED, 1000, [this](void*) {
			setLedColor(CRGB::Black);
			mqttClient->connect();
			taskScheduler->remove(TASK_SCHEDULER_LED);
		}, nullptr, false);
	}
}


// This event fires when the microcontroller can’t get an IP address, from a timeout or other errors.
void XSH_Device::onStationModeDHCPTimeout() {
	if (_config_mode_on) {
		webSocketServer->textAll(R"({"event":"wifi_dhcp_error"})");
	}
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
			onButtonShortPress();
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

	webServer->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("max-age=600");
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
		//client->printf("Hello Client %u :)", client->id());
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
		//the whole message is in a single frame and we got all of it's data
		if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
			for (size_t i = 0; i < info->len; i++)
				message += (char)data[i];
			onWebSocketTextDataEvent(server, client, message, len);
		}
		break;
	}

}


void XSH_Device::onWebSocketTextDataEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, String data, size_t len){
	DynamicJsonDocument request_doc(2048);
	DynamicJsonDocument response_doc(1024);
	deserializeJson(request_doc, data);

	Serial.println(data);

	String event = request_doc["event"];
	Serial.println(event);

	response_doc["event"] = event;
	String response;

	if (event == "scan_wifi_networks") {
		onFiWiScanRequest();
		response_doc["status"] = 2; // searching status
	}
	else
	if (event == "set_wifi_credentials") {
		const char *ssid = request_doc["ssid"];
		const char *password = request_doc["password"];

		if (ssid != NULL && password != NULL) {
			_settings.dhcp = true;
			saveSettings();
			response_doc["status"] = SUCCESS;
			WiFi.begin(ssid, password);
		}else
			response_doc["status"] = FAIL;
	}
	else
	if (event == "set_device_name") {
		const char* name = request_doc["name"];

		if (name != NULL) {
			strcpy(_settings.device_name, name);
			saveSettings();
			response_doc["status"] = SUCCESS;
		}else
			response_doc["status"] = FAIL;
	}
	else
	if (event == "set_wifi_advanced") {
		String s_local_ip = request_doc["local_ip"];
		String s_gate_way = request_doc["gateway"];
		String s_subnet = request_doc["subnet"];

		if (s_local_ip != NULL && s_gate_way != NULL && s_subnet != NULL) {

			IPAddress local_ip = stringToIpAdress(s_local_ip);
			IPAddress gateway = stringToIpAdress(s_gate_way);
			IPAddress subnet = stringToIpAdress(s_subnet);

			_settings.dhcp = false;
			_settings.local_ip = local_ip;
			_settings.gateway = gateway;
			_settings.subnet_mask = subnet;
			saveSettings();
			WiFi.config(local_ip, gateway, subnet);

			response_doc["status"] = SUCCESS;
		} else {
			response_doc["status"] = FAIL;
		}
	}
	else
	if (event == "reboot_device") {
		ESP.restart();
		//TODO: this causes a wdt reset and esp8266 crashs.
	}

	serializeJson(response_doc, response);
	client->text(response);
}


void XSH_Device::onFiWiScanRequest() { //scan for available wifi networks and send response to web socket client
	if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
		WiFi.scanNetworksAsync([&](int networks) {
			DynamicJsonDocument doc(2024);
			doc["event"] = "scan_wifi_networks";
			doc["status"] = SUCCESS;

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
				* 2 : ENC_TYPE_TKIP - WPA / PSK 
				* 4 : ENC_TYPE_CCMP - WPA2 / PSK 
				* 5 : ENC_TYPE_WEP - WEP 
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


// begin of MQTT event handlers
void XSH_Device::setMqttEventHandlers() {
	mqttClient->onConnect([this](bool sessionPresent) {
		onMqttConnect(sessionPresent);
	});
	mqttClient->onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
		onMqttDisconnect(reason);
	});
	mqttClient->onSubscribe([this](uint16_t packetId, uint8_t qos) {
		onMqttSubscribe(packetId, qos);
	});
	mqttClient->onUnsubscribe([this](uint16_t packetId) {
		onMqttUnsubscribe(packetId);
	});
	mqttClient->onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
		onMqttMessage(topic, payload, properties, len, index, total);
	});
	mqttClient->onPublish([this](int16_t packetId) {
		onMqttPublish(packetId);
	});

	mqttClient->setServer(MQTT_SERVER_URL, MQTT_PORT);

	/*#if ASYNC_TCP_SSL_ENABLED
		mqttClient.setSecure(true);
	#endif*/
}


void XSH_Device::onMqttConnect(bool sessionPresent) {
	if (!_config_mode_on){
		setLedColor(CRGB::Purple);
		taskScheduler->add(TASK_SCHEDULER_LED, 1000, [this](void*) {
			setLedColor(CRGB::Black);
			taskScheduler->remove(TASK_SCHEDULER_LED);
		}, nullptr, false);
	}

	mqttClient->publish("claudiu", 2, true, "hello from ESP8266");
}


void XSH_Device::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {

}


void XSH_Device::onMqttSubscribe(uint16_t packetId, uint8_t qos) {

}


void XSH_Device::onMqttUnsubscribe(uint16_t packetId) {

}


void XSH_Device::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	onMessage(payload, len);
}


void XSH_Device::onMqttPublish(uint16_t packetId) {

}

// end of MQTT event handlers


IPAddress stringToIpAdress(const char *string) {
	unsigned short a, b, c, d;
	sscanf(string, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
	return IPAddress(a, b, c, d);
}


IPAddress stringToIpAdress(String string) {
	return stringToIpAdress(string.c_str());
}



class MyDevice : public XSH_Device {
	// Inherited via XSH_Device
	virtual void onStart() override{

	}
	virtual void onLoop() override{
		
	}
	virtual void onMessage(char* payload, size_t len) override{
		
	}
	virtual void onButtonShortPress() override{

	}
};

MyDevice device;

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
