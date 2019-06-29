let ws = new WebSocket('ws://a.com/ws')//'ws://' + window.location.hostname + '/ws'
let ssid = document.getElementById("ssid");
let password = document.getElementById("password");
let device_name = document.getElementById("device_name");
let local_ip = document.getElementById("local_ip");
let gateway = document.getElementById("gateway");
let subnet = document.getElementById("subnet");
let wifi_list = document.getElementById("wifi_list");
let wifi_status = document.getElementById("wifi_status")
let device_name_message = document.getElementById("device_name_message");
let manual_ip_message = document.getElementById("manual_ip_message");

function checkIpInput(input){
	let valid = /^(?=.*[^\.]$)((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.?){4}$/.test(input.value);
	input.style.backgroundColor = valid ? "rgba(0, 255, 0, 0.5)" : "rgba(255, 0, 0, 0.5)";
	return valid;
}

ws.onopen = function(event){
};

ws.onclose = function(event){
	//ws.open();
};

ws.onmessage = function(event){
	let message = JSON.parse(event.data);
	switch (message.event) {
		case "scan_wifi_networks":
			switch (message.status) {
				case 0:
					break;
				case 1:
					let table = document.createElement("table");
					table.className = "table table-striped";
					for(let i=0; i<message.ssid.length; i++){
						let row = document.createElement("tr");
							let dot1 = document.createElement('td');
							dot1.innerText = message.ssid[i];
						row.appendChild(dot1);
							let dot2 = document.createElement('td');
								let progress = document.createElement('div');
								progress.className = "progress";
								progress.style.width = "100px";
									let progress_bar = document.createElement("div");
									progress_bar.className = "progress-bar progress-bar-stripped";
									progress_bar.style.width = (100 + message.rssi[i]).toString() + "%";
								progress.appendChild(progress_bar);
								dot2.appendChild(progress);
							row.appendChild(dot2);
							row.onclick = function(){
								ssid.value = message.ssid[i];
							};
						table.appendChild(row);
					}
					wifi_list.innerHTML = "";
					wifi_list.appendChild(table);
					break;
				case 2:
					let h = document.createElement('h4');
					h.innerText = "Searching . . .";
					wifi_list.appendChild(h);
					break
			}
			break;

		case "set_wifi_advanced":
			manual_ip_message.innerText = message.status ? "Success" : "Fail";
			manual_ip_message.className = message.status ? "alert alert-success" : "alert alert-danger";

			break;

		case "set_device_name":
			device_name_message.innerText = message.status ? "Name successfully set" : "Name set fail";
			device_name_message.className = message.status ? "alert alert-success" : "alert alert-danger";
			break;
		case "set_wifi_credentials":
			wifi_status.innerText = message.status ? "WiFi credentials successfully set" : "WiFi credentials set fail";
			wifi_status.className = message.status ? "alert alert-success" : "alert alert-danger";
			break;

		case "wifi_connected":
			wifi_status.innerText = "Connected to WiFi";
			wifi_status.className = "alert alert-success";
			break;
		case "wifi_auth_fail":
			wifi_status.innerText = "WiFi authentification fail";
			wifi_status.className = "alert alert-danger";
			break;
		case "wifi_disconnected":
			wifi_status.innerText = "Disconnected from WiFi";
			wifi_status.className = "alert alert-danger";
			break;
		case "dhcp_error":
			wifi_status.innerText = "Can not get IP";
			wifi_status.className = "alert alert-warning";
			break;
    }
};

document.getElementById("scan_button").onclick = function(){
	ws.send('{"event":"scan_wifi_networks"}');
};

document.getElementById("set_wifi_credentials").onclick = function(){
	ws.send(JSON.stringify({"event":"set_wifi_credentials", "ssid": ssid.value, "password": password.value}));
};

document.getElementById("set_device_name").onclick = function(){
	ws.send(JSON.stringify({"event": "set_device_name", "name": device_name.value}));
};

document.getElementById("set_manual_ip").onclick = function(){
	if(checkIpInput(local_ip) && checkIpInput(gateway) && checkIpInput(subnet)) {
		ws.send(JSON.stringify({
			"event": "set_wifi_advanced",
			"local_ip": local_ip.value,
			"gateway": gateway.value,
			"subnet": subnet.value
		}));
	}
};

document.getElementById("reboot_button").onclick = function () {
	ws.send(JSON.stringify({"event": "reboot_device"}));
};