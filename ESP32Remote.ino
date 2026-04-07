#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "USBHIDSystemControl.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include <PubSubClient.h>
#include <time.h>
#include <vector>

#define FW_VERSION "1.3.0"

// Forward declarations (fonctions définies plus bas, appelées en amont)
bool macroRun(const String& name);
std::vector<int> macroGetSlots();
void publishHADiscovery();

// ── Config persistante (NVS) ──────────────────────────────────────────────────

Preferences prefs;

// Wi-Fi
char cfg_ssid      [64] = "Team27wrt";
char cfg_password  [64] = "MaCléWifiDeMalade";

// IP fixe
bool cfg_static_ip      = false;
char cfg_ip   [16]      = "";           // ex. "192.168.1.50"
char cfg_gw   [16]      = "";           // ex. "192.168.1.1"
char cfg_mask [16]      = "255.255.255.0";
char cfg_dns  [16]      = "8.8.8.8";

// MQTT
char cfg_mqtt_host [64] = "192.168.15.251";
int  cfg_mqtt_port      = 1883;
char cfg_mqtt_user [32] = "";
char cfg_mqtt_pass [32] = "";
char cfg_mqtt_id   [32] = "esp32-remote";
char cfg_mqtt_sub  [64] = "esp32-remote/cmd";
char cfg_mqtt_pub  [64] = "esp32-remote/status";

// Appareil
char cfg_hostname  [32] = "ESP32-Remote";

const char* AP_SSID = "ESP32-Remote-Setup";
const char* AP_PASS = "esp32setup";

// ── Helpers IP ────────────────────────────────────────────────────────────────

// Parse "a.b.c.d" → IPAddress. Retourne INADDR_NONE (0.0.0.0) si invalide.
IPAddress parseIP(const char* s) {
  IPAddress ip;
  if (!ip.fromString(s)) ip = INADDR_NONE;
  return ip;
}

bool isValidIP(const char* s) {
  IPAddress ip;
  return strlen(s) >= 7 && ip.fromString(s) && ip != INADDR_NONE;
}

// ── NVS ──────────────────────────────────────────────────────────────────────

void loadConfig() {
  prefs.begin("remote", true);
  prefs.getString("ssid",      cfg_ssid,      sizeof(cfg_ssid));
  prefs.getString("password",  cfg_password,  sizeof(cfg_password));
  cfg_static_ip = prefs.getBool("static_ip", false);
  prefs.getString("ip",        cfg_ip,        sizeof(cfg_ip));
  prefs.getString("gw",        cfg_gw,        sizeof(cfg_gw));
  prefs.getString("mask",      cfg_mask,      sizeof(cfg_mask));
  prefs.getString("dns",       cfg_dns,       sizeof(cfg_dns));
  prefs.getString("mqtt_host", cfg_mqtt_host, sizeof(cfg_mqtt_host));
  cfg_mqtt_port = prefs.getInt("mqtt_port", cfg_mqtt_port);
  prefs.getString("mqtt_user", cfg_mqtt_user, sizeof(cfg_mqtt_user));
  prefs.getString("mqtt_pass", cfg_mqtt_pass, sizeof(cfg_mqtt_pass));
  prefs.getString("mqtt_id",   cfg_mqtt_id,   sizeof(cfg_mqtt_id));
  prefs.getString("mqtt_sub",  cfg_mqtt_sub,  sizeof(cfg_mqtt_sub));
  prefs.getString("mqtt_pub",  cfg_mqtt_pub,  sizeof(cfg_mqtt_pub));
  prefs.getString("hostname",  cfg_hostname,  sizeof(cfg_hostname));
  prefs.end();
  Serial.println("[Config] chargée");
}

void saveConfig() {
  prefs.begin("remote", false);
  prefs.putString("ssid",      cfg_ssid);
  prefs.putString("password",  cfg_password);
  prefs.putBool  ("static_ip", cfg_static_ip);
  prefs.putString("ip",        cfg_ip);
  prefs.putString("gw",        cfg_gw);
  prefs.putString("mask",      cfg_mask);
  prefs.putString("dns",       cfg_dns);
  prefs.putString("mqtt_host", cfg_mqtt_host);
  prefs.putInt   ("mqtt_port", cfg_mqtt_port);
  prefs.putString("mqtt_user", cfg_mqtt_user);
  prefs.putString("mqtt_pass", cfg_mqtt_pass);
  prefs.putString("mqtt_id",   cfg_mqtt_id);
  prefs.putString("mqtt_sub",  cfg_mqtt_sub);
  prefs.putString("mqtt_pub",  cfg_mqtt_pub);
  prefs.putString("hostname",  cfg_hostname);
  prefs.end();
  Serial.println("[Config] sauvegardée");
}

// ── Objets globaux ────────────────────────────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
WebServer    server(80);
DNSServer    dnsServer;

bool apMode = false;

// ── Mode AP ───────────────────────────────────────────────────────────────────

void startAPMode() {
  apMode = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  bool ok = (strlen(AP_PASS) >= 8) ? WiFi.softAP(AP_SSID, AP_PASS) : WiFi.softAP(AP_SSID);
  Serial.println(ok ? "[AP] " + String(AP_SSID) + " → 192.168.4.1" : "[AP] Erreur !");
  dnsServer.start(53, "*", WiFi.softAPIP());
}

// ── HID ───────────────────────────────────────────────────────────────────────

USBHIDConsumerControl ConsumerControl;
USBHIDSystemControl   SystemControl;
USBHIDKeyboard        Keyboard;

void pressMediaKey(uint16_t u)  { ConsumerControl.press(u); delay(100); ConsumerControl.release(); delay(50); }
void pressSystemKey(uint16_t u) { SystemControl.press(u);   delay(100); SystemControl.release();   delay(50); }
void pressKeyboardKey(uint8_t k){ Keyboard.press(k);         delay(100); Keyboard.release(k);       delay(50); }

void redirectHome() { server.sendHeader("Location","/",true); server.send(302,"text/plain",""); }
void sendMediaKey(uint16_t u)   { pressMediaKey(u);    redirectHome(); }
void sendSystemKey(uint16_t u)  { pressSystemKey(u);   redirectHome(); }
void sendKeyboardKey(uint8_t k) { pressKeyboardKey(k); redirectHome(); }

// ── Macros ────────────────────────────────────────────────────────────────────

void antenne() {
  pressMediaKey(CONSUMER_CONTROL_CONFIGURATION);
  for (int i=0;i<4;i++) pressKeyboardKey(KEY_UP_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN);
  for (int i=0;i<8;i++) pressKeyboardKey(KEY_UP_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN);
}
void miracast() {
  pressMediaKey(CONSUMER_CONTROL_CONFIGURATION);
  for (int i=0;i<4;i++) pressKeyboardKey(KEY_UP_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN);
  for (int i=0;i<8;i++) pressKeyboardKey(KEY_UP_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN);
}
void son() {
  pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); delay(500);
  pressKeyboardKey(KEY_UP_ARROW); pressKeyboardKey(KEY_UP_ARROW); pressKeyboardKey(KEY_UP_ARROW);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); delay(500);
  pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); delay(500);
  pressKeyboardKey(KEY_RETURN); delay(500);
  for (int i=0;i<6;i++) pressKeyboardKey(KEY_UP_ARROW);
}
void sonOriginal()        { son(); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }
void sonDivertissement()  { son(); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }
void sonMusique()         { son(); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }
void sonMusiqueSpatiale() { son(); for(int i=0;i<4;i++) pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }
void sonDialogue()        { son(); for(int i=0;i<5;i++) pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }
void sonPersonnel()       { son(); for(int i=0;i<6;i++) pressKeyboardKey(KEY_DOWN_ARROW); pressKeyboardKey(KEY_RETURN); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); }

void volZero() { for (int i=0;i<21;i++) pressMediaKey(CONSUMER_CONTROL_VOLUME_DECREMENT); }
void volHuit()  { volZero(); for (int i=0;i<8;i++) pressMediaKey(CONSUMER_CONTROL_VOLUME_INCREMENT); }
void setVolume(int level) {
  if (level<0) level=0; if (level>20) level=20;
  for (int i=0;i<20;i++) pressMediaKey(CONSUMER_CONTROL_VOLUME_DECREMENT);
  for (int i=0;i<level;i++) pressMediaKey(CONSUMER_CONTROL_VOLUME_INCREMENT);
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

bool dispatchCommand(const String& cmd, bool httpMode) {
  auto done = [&](const char* label) {
    if (httpMode) redirectHome();
    else { if (mqtt.connected()) mqtt.publish(cfg_mqtt_pub,label); Serial.printf("[MQTT] %s\n",label); }
  };
  if (cmd=="up")        { pressKeyboardKey(KEY_UP_ARROW);                done("up");        return true; }
  if (cmd=="down")      { pressKeyboardKey(KEY_DOWN_ARROW);              done("down");      return true; }
  if (cmd=="left")      { pressKeyboardKey(KEY_LEFT_ARROW);              done("left");      return true; }
  if (cmd=="right")     { pressKeyboardKey(KEY_RIGHT_ARROW);             done("right");     return true; }
  if (cmd=="enter")     { pressKeyboardKey(KEY_RETURN);                  done("enter");     return true; }
  if (cmd=="space")     { pressKeyboardKey(KEY_SPACE);                   done("space");     return true; }
  if (cmd=="esc")       { pressMediaKey(CONSUMER_CONTROL_BACK);          done("esc");       return true; }
  if (cmd=="volup")     { pressMediaKey(CONSUMER_CONTROL_VOLUME_INCREMENT); done("volup");  return true; }
  if (cmd=="voldown")   { pressMediaKey(CONSUMER_CONTROL_VOLUME_DECREMENT); done("voldown");return true; }
  if (cmd=="mute")      { pressMediaKey(HID_USAGE_CONSUMER_MUTE);        done("mute");      return true; }
  if (cmd=="playpause") { pressMediaKey(CONSUMER_CONTROL_PLAY_PAUSE);    done("playpause"); return true; }
  if (cmd=="next")      { pressMediaKey(CONSUMER_CONTROL_SCAN_NEXT);     done("next");      return true; }
  if (cmd=="previous")  { pressMediaKey(CONSUMER_CONTROL_SCAN_PREVIOUS); done("previous");  return true; }
  if (cmd=="home")      { pressMediaKey(CONSUMER_CONTROL_CONFIGURATION); done("home");      return true; }
  if (cmd=="power")     { pressSystemKey(SYSTEM_CONTROL_POWER_OFF);      done("power");     return true; }
  if (cmd=="wake")      { pressSystemKey(SYSTEM_CONTROL_WAKE_HOST);      done("wake");      return true; }
  if (cmd=="antenne")            { antenne();           done("antenne");           return true; }
  if (cmd=="miracast")           { miracast();          done("miracast");          return true; }
  if (cmd=="volhuit")            { volHuit();           done("volhuit");           return true; }
  // chiffres 0–9
  if (cmd.length()==6 && cmd.startsWith("digit")) {
    char c = cmd[5];
    if (c>='0' && c<='9') { pressKeyboardKey(c); done(cmd.c_str()); return true; }
  }
  // macros nommées via MQTT : "macro:NomDeLaMacro"
  if (cmd.startsWith("macro:")) { macroRun(cmd.substring(6)); done(cmd.c_str()); return true; }
  if (cmd=="son")                { son();               done("son");               return true; }
  if (cmd=="sonoriginal")        { sonOriginal();       done("sonoriginal");       return true; }
  if (cmd=="sondivertissement")  { sonDivertissement(); done("sondivertissement"); return true; }
  if (cmd=="sonmusique")         { sonMusique();        done("sonmusique");        return true; }
  if (cmd=="sonmusiquespatiale") { sonMusiqueSpatiale();done("sonmusiquespatiale");return true; }
  if (cmd=="sondialogue")        { sonDialogue();       done("sondialogue");       return true; }
  if (cmd=="sonpersonnel")       { sonPersonnel();      done("sonpersonnel");      return true; }
  if (cmd.startsWith("vol:")) { setVolume(cmd.substring(4).toInt()); done(cmd.c_str()); return true; }
  if (cmd.startsWith("key:")) {
    String v=cmd.substring(4);
    pressMediaKey((v.startsWith("0x")||v.startsWith("0X"))?strtol(v.c_str(),nullptr,16):v.toInt());
    done(cmd.c_str()); return true;
  }
  if (!httpMode && mqtt.connected()) mqtt.publish(cfg_mqtt_pub,("unknown:"+cmd).c_str());
  return false;
}

// ── MQTT ──────────────────────────────────────────────────────────────────────

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String cmd=""; for (unsigned int i=0;i<length;i++) cmd+=(char)payload[i];
  cmd.trim(); cmd.toLowerCase(); dispatchCommand(cmd,false);
}
// ── NTP ──────────────────────────────────────────────────────────────────────

void ntpSync() {
  configTime(3600, 3600, "pool.ntp.org", "time.google.com");
  Serial.print("[NTP] Sync");
  struct tm t;
  for (int i=0;i<20;i++) { if (getLocalTime(&t,500)) { Serial.printf(" OK — %02d:%02d\n",t.tm_hour,t.tm_min); return; } Serial.print("."); }
  Serial.println(" timeout");
}

// ── MQTT Discovery ────────────────────────────────────────────────────────────

// Bloc "device" commun à toutes les entités
String haDevice() {
  return String("{\"ids\":[\"") + cfg_mqtt_id + "\"],"
    "\"name\":\"" + cfg_hostname + "\","
    "\"mdl\":\"ESP32 HID Remote\","
    "\"mf\":\"DIY\","
    "\"sw\":\"" FW_VERSION "\"}";
}

// Publie un message de découverte HA — retente si trop grand pour le buffer
void haPub(const String& component, const String& objId, const String& payload) {
  String topic = "homeassistant/" + component + "/" + String(cfg_mqtt_id) + "/" + objId + "/config";
  mqtt.setBufferSize(512);
  if (!mqtt.publish(topic.c_str(), payload.c_str(), true)) {
    Serial.printf("[HA] Pub trop grand pour %s (%d)\n", objId.c_str(), payload.length());
  }
}

// Bouton simple : commande publiée sur cfg_mqtt_sub
void haButton(const String& id, const String& name, const String& icon, const String& cmd) {
  String p = "{\"name\":\"" + name + "\","
    "\"uniq_id\":\"" + String(cfg_mqtt_id) + "_" + id + "\","
    "\"cmd_t\":\"" + cfg_mqtt_sub + "\","
    "\"pl_prs\":\"" + cmd + "\","
    "\"ic\":\"" + icon + "\","
    "\"dev\":" + haDevice() + "}";
  haPub("button", id, p);
}

// Capteur texte/état
void haSensor(const String& id, const String& name, const String& icon,
              const String& stateTopic, const String& unit="", const String& devClass="") {
  String p = "{\"name\":\"" + name + "\","
    "\"uniq_id\":\"" + String(cfg_mqtt_id) + "_" + id + "\","
    "\"stat_t\":\"" + stateTopic + "\","
    "\"val_tpl\":\"{{value_json." + id + "}}\"";
  if (unit.length())     p += ",\"unit_of_meas\":\"" + unit + "\"";
  if (devClass.length()) p += ",\"dev_cla\":\"" + devClass + "\"";
  if (icon.length())     p += ",\"ic\":\"" + icon + "\"";
  p += ",\"dev\":" + haDevice() + "}";
  haPub("sensor", id, p);
}

// Sensor binaire (online/offline)
void haBinarySensor(const String& id, const String& name, const String& devClass) {
  String lwt = String(cfg_mqtt_pub) + "/lwt";
  String p = "{\"name\":\"" + name + "\","
    "\"uniq_id\":\"" + String(cfg_mqtt_id) + "_" + id + "\","
    "\"stat_t\":\"" + lwt + "\","
    "\"pl_on\":\"online\",\"pl_off\":\"offline\","
    "\"dev_cla\":\"" + devClass + "\","
    "\"dev\":" + haDevice() + "}";
  haPub("binary_sensor", id, p);
}

void publishHADiscovery() {
  String tel = String(cfg_mqtt_pub) + "/telemetry";

  // ── Capteurs ────────────────────────────────────────────────────────────────
  haBinarySensor("tv_state",  "TV — État",     "connectivity");
  haSensor("rssi",    "Wi-Fi RSSI",    "mdi:wifi",          tel, "dBm",  "signal_strength");
  haSensor("uptime",  "Uptime",        "mdi:timer-outline", tel, "s",    "duration");
  haSensor("ip",      "Adresse IP",    "mdi:ip",            tel);
  haSensor("version", "Version FW",    "mdi:chip",          tel);
  haSensor("boots",   "Nbre boots",    "mdi:counter",       tel, "");

  // ── Boutons navigation ───────────────────────────────────────────────────────
  haButton("up",        "Haut",         "mdi:arrow-up",        "up");
  haButton("down",      "Bas",          "mdi:arrow-down",      "down");
  haButton("left",      "Gauche",       "mdi:arrow-left",      "left");
  haButton("right",     "Droite",       "mdi:arrow-right",     "right");
  haButton("enter",     "OK",           "mdi:check-circle",    "enter");
  haButton("esc",       "Retour",       "mdi:arrow-u-left-top","esc");
  haButton("space",     "Espace",       "mdi:keyboard-space",  "space");

  // ── Boutons système ──────────────────────────────────────────────────────────
  haButton("power",     "Power",        "mdi:power",           "power");
  haButton("home",      "Home",         "mdi:home",            "home");
  haButton("wake",      "Wake",         "mdi:monitor-shimmer", "wake");

  // ── Volume / audio ───────────────────────────────────────────────────────────
  haButton("volup",     "Volume +",     "mdi:volume-plus",     "volup");
  haButton("voldown",   "Volume -",     "mdi:volume-minus",    "voldown");
  haButton("mute",      "Mute",         "mdi:volume-mute",     "mute");
  haButton("volhuit",   "Volume 8",     "mdi:volume-medium",   "volhuit");

  // ── Lecture ──────────────────────────────────────────────────────────────────
  haButton("playpause", "Play/Pause",   "mdi:play-pause",      "playpause");
  haButton("next",      "Suivant",      "mdi:skip-next",       "next");
  haButton("previous",  "Précédent",    "mdi:skip-previous",   "previous");

  // ── Sources ──────────────────────────────────────────────────────────────────
  haButton("antenne",   "Antenne",      "mdi:antenna",         "antenne");
  haButton("miracast",  "Miracast",     "mdi:cast",            "miracast");

  // ── Modes son ────────────────────────────────────────────────────────────────
  // Pavé numérique
  for(int d=0;d<=9;d++) {
    String id = "digit"+String(d);
    String name = "Chiffre "+String(d);
    haButton(id, name, "mdi:numeric-"+String(d)+"-box", id);
  }

  haButton("sonoriginal",        "Son Original",         "mdi:speaker",         "sonoriginal");
  haButton("sondivertissement",  "Son Divertissement",   "mdi:movie-open",      "sondivertissement");
  haButton("sonmusique",         "Son Musique",          "mdi:music-note",      "sonmusique");
  haButton("sonmusiquespatiale", "Son Spatiale",         "mdi:surround-sound",  "sonmusiquespatiale");
  haButton("sondialogue",        "Son Dialogue",         "mdi:account-voice",   "sondialogue");
  haButton("sonpersonnel",       "Son Perso",            "mdi:tune",            "sonpersonnel");

  // Macros sauvegardées
  auto slots = macroGetSlots();
  for (int s : slots) {
    prefs.begin("macros", true);
    String n = prefs.getString(("mcr_n_"+String(s)).c_str(), "");
    prefs.end();
    if (n.length()) {
      String id = "macro_" + String(s);
      haButton(id, "⚡ "+n, "mdi:lightning-bolt", "macro:"+n);
    }
  }
  Serial.println("[HA] Discovery publiée");
}

// ── Télémétrie ────────────────────────────────────────────────────────────────

static uint32_t bootCount = 0;

void loadBootCount() {
  prefs.begin("remote", true);
  bootCount = prefs.getUInt("boots", 0);
  prefs.end();
  bootCount++;
  prefs.begin("remote", false);
  prefs.putUInt("boots", bootCount);
  prefs.end();
  Serial.printf("[Boot] #%u\n", bootCount);
}

void publishTelemetry() {
  if (!mqtt.connected()) return;
  String lwt = String(cfg_mqtt_pub) + "/lwt";
  mqtt.publish(lwt.c_str(), "online", true);

  String tel = String(cfg_mqtt_pub) + "/telemetry";
  String ip = WiFi.localIP().toString();
  String payload = "{\"rssi\":" + String(WiFi.RSSI())
    + ",\"uptime\":" + String(millis()/1000)
    + ",\"ip\":\"" + ip + "\""
    + ",\"version\":\"" FW_VERSION "\""
    + ",\"boots\":" + String(bootCount)
    + "}";
  mqtt.setBufferSize(256);
  mqtt.publish(tel.c_str(), payload.c_str(), false);
  Serial.printf("[Telemetry] RSSI=%d uptime=%lus boots=%u\n",
    WiFi.RSSI(), millis()/1000, bootCount);
}

// ── MQTT ─────────────────────────────────────────────────────────────────────

void mqttConnect() {
  if (mqtt.connected() || apMode || strlen(cfg_mqtt_host)==0) return;

  // LWT : publié automatiquement par le broker si l'ESP se déconnecte
  String lwt = String(cfg_mqtt_pub) + "/lwt";
  bool ok = (strlen(cfg_mqtt_user) > 0)
    ? mqtt.connect(cfg_mqtt_id, cfg_mqtt_user, cfg_mqtt_pass,
                   lwt.c_str(), 0, true, "offline")
    : mqtt.connect(cfg_mqtt_id, nullptr, nullptr,
                   lwt.c_str(), 0, true, "offline");

  if (ok) {
    mqtt.subscribe(cfg_mqtt_sub);
    Serial.println("[MQTT] Connecté");
    publishHADiscovery();
    publishTelemetry();
  } else {
    Serial.printf("[MQTT] Echec rc=%d\n", mqtt.state());
  }
}

// ── HTTP handlers macros ──────────────────────────────────────────────────────

#define HTTP_CMD(name,cmd) void handle_##name(){dispatchCommand(cmd,true);}
HTTP_CMD(volup,"volup") HTTP_CMD(voldown,"voldown") HTTP_CMD(mute,"mute")
HTTP_CMD(playpause,"playpause") HTTP_CMD(next,"next") HTTP_CMD(previous,"previous")
HTTP_CMD(home,"home") HTTP_CMD(power,"power") HTTP_CMD(wake,"wake")
HTTP_CMD(up,"up") HTTP_CMD(down,"down") HTTP_CMD(left,"left") HTTP_CMD(right,"right")
HTTP_CMD(enter,"enter") HTTP_CMD(space,"space") HTTP_CMD(esc,"esc")
HTTP_CMD(antenne,"antenne") HTTP_CMD(miracast,"miracast") HTTP_CMD(volhuit,"volhuit")
HTTP_CMD(digit0,"digit0") HTTP_CMD(digit1,"digit1") HTTP_CMD(digit2,"digit2") HTTP_CMD(digit3,"digit3") HTTP_CMD(digit4,"digit4") HTTP_CMD(digit5,"digit5") HTTP_CMD(digit6,"digit6") HTTP_CMD(digit7,"digit7") HTTP_CMD(digit8,"digit8") HTTP_CMD(digit9,"digit9")
HTTP_CMD(son,"son") HTTP_CMD(sonoriginal,"sonoriginal")
HTTP_CMD(sondivertissement,"sondivertissement") HTTP_CMD(sonmusique,"sonmusique")
HTTP_CMD(sonmusiquespatiale,"sonmusiquespatiale") HTTP_CMD(sondialogue,"sondialogue")
HTTP_CMD(sonpersonnel,"sonpersonnel")

// ── Page configuration ────────────────────────────────────────────────────────

void handleConfigGet() {
  // IP effective (ce qu'on a vraiment obtenu)
  String currentIP = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  String html = "<!DOCTYPE html><html lang='fr'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Config — ESP32 Remote</title><style>";
  html += ":root{--bg:#0f1117;--surface:#1a1d27;--surface2:#22263a;--border:rgba(255,255,255,0.07);";
  html += "--text:#e8eaf0;--muted:#6b7080;--accent:#4f8ef7;--green:#3ecf7a;--red:#f05a5a;--orange:#f5a623;--radius:14px;--radius-sm:10px;}";
  html += "*{box-sizing:border-box;margin:0;padding:0;}";
  html += "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100dvh;padding:16px 12px 40px;max-width:480px;margin:0 auto;}";
  html += ".header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px;}";
  html += ".header-title{font-size:18px;font-weight:700;}.header-sub{font-size:12px;color:var(--muted);margin-top:2px;}";
  html += "a.back{display:inline-flex;align-items:center;gap:6px;color:var(--muted);font-size:13px;text-decoration:none;padding:6px 12px;border:1px solid var(--border);border-radius:99px;transition:all .15s;}";
  html += "a.back:hover{color:var(--text);border-color:rgba(255,255,255,.2);}";
  html += ".banner-ap{background:rgba(245,166,35,.1);border:1px solid rgba(245,166,35,.3);border-radius:var(--radius);padding:14px 16px;margin-bottom:16px;font-size:13px;line-height:1.6;}";
  html += ".banner-ap strong{color:var(--orange);display:block;margin-bottom:4px;font-size:14px;}";
  html += ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:20px;margin-bottom:12px;}";
  html += ".card-title{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:16px;}";
  html += ".field{margin-bottom:14px;}label{display:block;font-size:12px;color:var(--muted);margin-bottom:5px;font-weight:500;}";
  html += "input[type=text],input[type=password],input[type=number]{width:100%;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:10px 14px;border-radius:var(--radius-sm);font-size:14px;outline:none;transition:border-color .15s;font-family:monospace;}";
  html += "input:focus{border-color:var(--accent);}";
  html += ".row2{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
  html += ".row2b{display:grid;grid-template-columns:1fr 90px;gap:8px;}";
  html += ".hint{font-size:11px;color:var(--muted);margin-top:4px;}";
  html += ".divider{border:none;border-top:1px solid var(--border);margin:4px 0 16px;}";

  // Toggle switch IP fixe
  html += ".toggle-row{display:flex;align-items:center;justify-content:space-between;padding:12px 0;border-bottom:1px solid var(--border);margin-bottom:16px;}";
  html += ".toggle-label{font-size:14px;font-weight:600;}";
  html += ".toggle-sub{font-size:11px;color:var(--muted);margin-top:2px;}";
  html += ".switch{position:relative;display:inline-block;width:44px;height:24px;}";
  html += ".switch input{opacity:0;width:0;height:0;}";
  html += ".slider{position:absolute;cursor:pointer;inset:0;background:var(--surface2);border:1px solid var(--border);border-radius:99px;transition:.2s;}";
  html += ".slider:before{position:absolute;content:'';height:16px;width:16px;left:3px;bottom:3px;background:var(--muted);border-radius:50%;transition:.2s;}";
  html += "input:checked+.slider{background:rgba(79,142,247,.25);border-color:rgba(79,142,247,.5);}";
  html += "input:checked+.slider:before{transform:translateX(20px);background:var(--accent);}";
  html += ".ip-fields{overflow:hidden;max-height:0;opacity:0;transition:max-height .3s ease,opacity .3s ease;}";
  html += ".ip-fields.open{max-height:400px;opacity:1;}";
  html += ".ip-current{font-size:12px;color:var(--muted);background:var(--surface2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 12px;margin-bottom:16px;font-family:monospace;}";
  html += ".ip-current span{color:var(--green);font-weight:600;}";

  html += ".btn-save{width:100%;background:var(--accent);color:#fff;border:none;padding:14px;border-radius:var(--radius-sm);font-size:15px;font-weight:700;cursor:pointer;transition:all .15s;margin-top:4px;}";
  html += ".btn-save:hover{background:#3a7de8;}.btn-save:active{transform:scale(.97);}";
  html += ".btn-reset{width:100%;background:rgba(240,90,90,.12);color:var(--red);border:1px solid rgba(240,90,90,.25);padding:11px;border-radius:var(--radius-sm);font-size:13px;font-weight:600;cursor:pointer;margin-top:8px;transition:all .15s;}";
  html += ".btn-reset:hover{background:rgba(240,90,90,.22);}";
  html += ".status-row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:16px;}";
  html += ".badge{font-size:11px;font-weight:600;padding:4px 10px;border-radius:99px;border:1px solid;}";
  html += ".badge-ok  {color:var(--green); border-color:rgba(62,207,122,.3); background:rgba(62,207,122,.1);}";
  html += ".badge-err {color:var(--red);   border-color:rgba(240,90,90,.3);  background:rgba(240,90,90,.1);}";
  html += ".badge-warn{color:var(--orange);border-color:rgba(245,166,35,.3); background:rgba(245,166,35,.1);}";
  html += ".badge-info{color:var(--accent);border-color:rgba(79,142,247,.3); background:rgba(79,142,247,.1);}";
  html += ".pwd-wrap{position:relative;}";
  html += ".eye-btn{position:absolute;right:12px;top:50%;transform:translateY(-50%);background:none;border:none;color:var(--muted);cursor:pointer;padding:4px;font-size:16px;}";
  html += "</style></head><body>";

  // Header
  html += "<div class='header'><div><div class='header-title'>⚙️ Configuration</div>";
  html += "<div class='header-sub'>ESP32 Remote</div></div>";
  if (!apMode) html += "<a class='back' href='/'>← Retour</a>";
  html += "</div>";

  // Bannière AP
  if (apMode) {
    html += "<div class='banner-ap'><strong>⚠️ Mode configuration — Wi-Fi non connecté</strong>";
    html += "Connecté au réseau <code>" + String(AP_SSID) + "</code>. Configurez le Wi-Fi puis sauvegardez.</div>";
  }

  // Statuts
  html += "<div class='status-row'>";
  if (apMode) {
    html += "<span class='badge badge-warn'>Mode AP</span>";
    html += "<span class='badge badge-info'>192.168.4.1</span>";
  } else {
    html += String("<span class='badge ") + (WiFi.status()==WL_CONNECTED?"badge-ok":"badge-err") + "'>";
    html += WiFi.status()==WL_CONNECTED ? "Wi-Fi ✓ " + WiFi.localIP().toString() : "Wi-Fi ✗";
    html += "</span>";
    // Indiquer si l'IP est statique ou DHCP
    if (WiFi.status()==WL_CONNECTED) {
      html += String("<span class='badge ") + (cfg_static_ip?"badge-info":"badge-warn") + "'>";
      html += cfg_static_ip ? "IP fixe" : "DHCP";
      html += "</span>";
    }
    html += String("<span class='badge ") + (mqtt.connected()?"badge-ok":"badge-warn") + "'>";
    html += mqtt.connected() ? "MQTT ✓" : "MQTT ✗";
    html += "</span>";
    html += String("<span class='badge badge-warn'>RSSI ") + WiFi.RSSI() + " dBm</span>";
  }
  html += "</div>";

  html += "<form method='POST' action='/config'>";

  // ── Wi-Fi ──────────────────────────────────────────────────────────────────
  html += "<div class='card'><div class='card-title'>📶 Wi-Fi</div>";
  html += "<div class='field'><label>SSID</label><input type='text' name='ssid' value='" + String(cfg_ssid) + "'></div>";
  html += "<div class='field'><label>Mot de passe</label><div class='pwd-wrap'>";
  html += "<input type='password' id='wpwd' name='password' value='" + String(cfg_password) + "'>";
  html += "<button type='button' class='eye-btn' onclick=\"togglePwd('wpwd',this)\">👁</button></div></div>";

  // ── IP fixe ────────────────────────────────────────────────────────────────
  html += "<div class='toggle-row'>";
  html += "<div><div class='toggle-label'>IP fixe</div><div class='toggle-sub'>Désactiver pour utiliser le DHCP</div></div>";
  html += String("<label class='switch'><input type='checkbox' id='static_chk' name='static_ip' value='1'")
        + (cfg_static_ip ? " checked" : "") + " onchange='toggleIP(this)'>";
  html += "<span class='slider'></span></label></div>";

  // IP actuelle (info)
  if (!apMode && WiFi.status()==WL_CONNECTED) {
    html += "<div class='ip-current'>IP actuelle : <span>" + WiFi.localIP().toString() + "</span>"
          + " &nbsp;·&nbsp; GW : <span>" + WiFi.gatewayIP().toString() + "</span>"
          + " &nbsp;·&nbsp; Masque : <span>" + WiFi.subnetMask().toString() + "</span></div>";
  }

  // Champs IP (cachés si DHCP)
  html += String("<div class='ip-fields") + (cfg_static_ip?" open":"") + "' id='ip-fields'>";
  html += "<div class='row2'>";
  html += "<div class='field'><label>Adresse IP</label><input type='text' name='ip' id='f_ip' placeholder='192.168.1.50' value='" + String(cfg_ip) + "'></div>";
  html += "<div class='field'><label>Passerelle</label><input type='text' name='gw' id='f_gw' placeholder='192.168.1.1' value='" + String(cfg_gw) + "'></div>";
  html += "</div>";
  html += "<div class='row2'>";
  html += "<div class='field'><label>Masque de sous-réseau</label><input type='text' name='mask' id='f_mask' placeholder='255.255.255.0' value='" + String(cfg_mask) + "'></div>";
  html += "<div class='field'><label>DNS</label><input type='text' name='dns' id='f_dns' placeholder='8.8.8.8' value='" + String(cfg_dns) + "'></div>";
  html += "</div>";
  html += "<div class='hint'>⚠️ Une IP invalide ou déjà utilisée forcera le mode AP au prochain démarrage.</div>";
  html += "</div>"; // ip-fields

  html += "</div>"; // card Wi-Fi

  // ── MQTT ───────────────────────────────────────────────────────────────────
  html += "<div class='card'><div class='card-title'>🔗 MQTT</div>";
  html += "<div class='field'><div class='row2b'>";
  html += "<div><label>Broker</label><input type='text' name='mqtt_host' value='" + String(cfg_mqtt_host) + "'></div>";
  html += "<div><label>Port</label><input type='number' name='mqtt_port' value='" + String(cfg_mqtt_port) + "' min='1' max='65535'></div>";
  html += "</div></div>";
  html += "<div class='field'><label>Client ID</label><input type='text' name='mqtt_id' value='" + String(cfg_mqtt_id) + "'></div>";
  html += "<div class='field'><label>Utilisateur <span style='color:var(--muted)'>(optionnel)</span></label>";
  html += "<input type='text' name='mqtt_user' value='" + String(cfg_mqtt_user) + "'></div>";
  html += "<div class='field'><label>Mot de passe MQTT</label><div class='pwd-wrap'>";
  html += "<input type='password' id='mpwd' name='mqtt_pass' value='" + String(cfg_mqtt_pass) + "'>";
  html += "<button type='button' class='eye-btn' onclick=\"togglePwd('mpwd',this)\">👁</button></div></div>";
  html += "<hr class='divider'>";
  html += "<div class='field'><label>Topic commandes (subscribe)</label><input type='text' name='mqtt_sub' value='" + String(cfg_mqtt_sub) + "'></div>";
  html += "<div class='field'><label>Topic feedback (publish)</label><input type='text' name='mqtt_pub' value='" + String(cfg_mqtt_pub) + "'></div>";
  html += "</div>";

  // ── Appareil ───────────────────────────────────────────────────────────────
  html += "<div class='card'><div class='card-title'>🏷️ Appareil</div>";
  html += "<div class='field'><label>Hostname (mDNS / OTA)</label>";
  html += "<input type='text' name='hostname' value='" + String(cfg_hostname) + "'>";
  html += "<div class='hint'>Accessible via http://" + String(cfg_hostname) + ".local</div></div>";
  html += "</div>";

  html += "<a href='/macros' style='display:block;text-align:center;background:rgba(124,92,252,.12);color:var(--accent2);border:1px solid rgba(124,92,252,.3);padding:11px;border-radius:var(--radius-sm);font-size:14px;font-weight:600;margin-bottom:8px;text-decoration:none;'>&#9889; Macros &mdash; G&eacute;rer les s&eacute;quences</a>";
  html += "<a href='/update' style='display:block;text-align:center;background:rgba(79,142,247,.12);color:var(--accent);border:1px solid rgba(79,142,247,.3);padding:11px;border-radius:var(--radius-sm);font-size:14px;font-weight:600;margin-bottom:8px;text-decoration:none;'>&#x1F504; Mise &agrave; jour firmware (OTA)</a>";
  html += "<button type='submit' class='btn-save'>💾 Sauvegarder &amp; Red&eacute;marrer</button>";
  html += "</form>";
  html += "<form method='POST' action='/config/reset'>";
  html += "<button type='submit' class='btn-reset'>🗑 Réinitialiser la configuration</button>";
  html += "</form>";

  html += "<script>";
  html += "function togglePwd(id,btn){const el=document.getElementById(id);const s=el.type==='password';el.type=s?'text':'password';btn.textContent=s?'🔒':'👁';}";
  html += "function toggleIP(cb){const f=document.getElementById('ip-fields');f.classList.toggle('open',cb.checked);}";
  // Validation légère avant submit
  html += R"js(
document.querySelector('form').addEventListener('submit',function(e){
  const chk=document.getElementById('static_chk');
  if(!chk.checked) return;
  const ipRe=/^(\d{1,3}\.){3}\d{1,3}$/;
  const fields=[['f_ip','Adresse IP'],['f_gw','Passerelle'],['f_mask','Masque'],['f_dns','DNS']];
  for(const [id,label] of fields){
    const v=document.getElementById(id).value.trim();
    if(!ipRe.test(v)){e.preventDefault();alert('Format invalide pour : '+label+'\nExemple : 192.168.1.1');return;}
  }
});
)js";
  html += "if(new URLSearchParams(location.search).get('saved'))alert('✓ Config sauvegardée — redémarrage en cours...');";
  html += "</script></body></html>";

  server.send(200,"text/html",html);
}

void handleConfigPost() {
  if (server.hasArg("ssid"))      strlcpy(cfg_ssid,      server.arg("ssid").c_str(),      sizeof(cfg_ssid));
  if (server.hasArg("password"))  strlcpy(cfg_password,  server.arg("password").c_str(),  sizeof(cfg_password));

  // IP fixe
  cfg_static_ip = server.hasArg("static_ip") && server.arg("static_ip") == "1";
  if (server.hasArg("ip"))   strlcpy(cfg_ip,   server.arg("ip").c_str(),   sizeof(cfg_ip));
  if (server.hasArg("gw"))   strlcpy(cfg_gw,   server.arg("gw").c_str(),   sizeof(cfg_gw));
  if (server.hasArg("mask")) strlcpy(cfg_mask, server.arg("mask").c_str(), sizeof(cfg_mask));
  if (server.hasArg("dns"))  strlcpy(cfg_dns,  server.arg("dns").c_str(),  sizeof(cfg_dns));

  // Si IP fixe activée mais champs vides/invalides → désactiver silencieusement
  if (cfg_static_ip && (!isValidIP(cfg_ip) || !isValidIP(cfg_gw))) {
    cfg_static_ip = false;
    Serial.println("[Config] IP fixe désactivée (valeurs invalides)");
  }

  if (server.hasArg("mqtt_host")) strlcpy(cfg_mqtt_host, server.arg("mqtt_host").c_str(), sizeof(cfg_mqtt_host));
  if (server.hasArg("mqtt_port")) cfg_mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("mqtt_user")) strlcpy(cfg_mqtt_user, server.arg("mqtt_user").c_str(), sizeof(cfg_mqtt_user));
  if (server.hasArg("mqtt_pass")) strlcpy(cfg_mqtt_pass, server.arg("mqtt_pass").c_str(), sizeof(cfg_mqtt_pass));
  if (server.hasArg("mqtt_id"))   strlcpy(cfg_mqtt_id,   server.arg("mqtt_id").c_str(),   sizeof(cfg_mqtt_id));
  if (server.hasArg("mqtt_sub"))  strlcpy(cfg_mqtt_sub,  server.arg("mqtt_sub").c_str(),  sizeof(cfg_mqtt_sub));
  if (server.hasArg("mqtt_pub"))  strlcpy(cfg_mqtt_pub,  server.arg("mqtt_pub").c_str(),  sizeof(cfg_mqtt_pub));
  if (server.hasArg("hostname"))  strlcpy(cfg_hostname,  server.arg("hostname").c_str(),  sizeof(cfg_hostname));

  saveConfig();
  server.sendHeader("Location","/config?saved=1",true);
  server.send(302,"text/plain","");
  delay(800); ESP.restart();
}

void handleConfigReset() {
  prefs.begin("remote",false); prefs.clear(); prefs.end();
  server.sendHeader("Location","/config",true);
  server.send(302,"text/plain","");
  delay(500); ESP.restart();
}

void handleCaptivePortal() {
  server.sendHeader("Location","http://192.168.4.1/config",true);
  server.send(302,"text/plain","");
}

// ── Page principale ───────────────────────────────────────────────────────────

void handleRoot() {
  server.send(200,"text/html", R"rawliteral(
<!DOCTYPE html><html lang="fr">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>ESP32 Remote</title>
<style>
  :root{--bg:#0f1117;--surface:#1a1d27;--surface2:#22263a;--border:rgba(255,255,255,0.07);
    --text:#e8eaf0;--muted:#6b7080;--accent:#4f8ef7;--accent2:#7c5cfc;
    --green:#3ecf7a;--orange:#f5a623;--red:#f05a5a;--radius:14px;--radius-sm:10px;}
  *{box-sizing:border-box;margin:0;padding:0;}html{-webkit-tap-highlight-color:transparent;}
  body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100dvh;padding:16px 12px 40px;max-width:420px;margin:0 auto;}
  .header{display:flex;align-items:center;justify-content:space-between;margin-bottom:24px;}
  .header-title{font-size:18px;font-weight:700;letter-spacing:-.3px;}
  .header-sub{font-size:12px;color:var(--muted);margin-top:2px;}
  .header-right{display:flex;align-items:center;gap:8px;}
  .badges{display:flex;gap:6px;align-items:center;}
  .badge{font-size:10px;font-weight:600;padding:3px 8px;border-radius:99px;border:1px solid;}
  .badge-http{color:var(--green);border-color:rgba(62,207,122,.3);background:rgba(62,207,122,.1);}
  .badge-mqtt{color:var(--accent);border-color:rgba(79,142,247,.3);background:rgba(79,142,247,.1);}
  .badge-mqtt.offline{color:var(--muted);border-color:var(--border);background:transparent;}
  .btn-config{display:inline-flex;align-items:center;justify-content:center;width:34px;height:34px;
    border-radius:50%;border:1px solid var(--border);background:var(--surface);color:var(--muted);
    text-decoration:none;font-size:16px;transition:all .15s;}
  .btn-config:hover{color:var(--text);border-color:rgba(255,255,255,.2);}
  #toast{position:fixed;top:16px;left:50%;transform:translateX(-50%) translateY(-60px);
    background:var(--surface2);color:var(--text);border:1px solid var(--border);
    padding:8px 20px;border-radius:99px;font-size:13px;font-weight:500;
    transition:transform .25s cubic-bezier(.34,1.56,.64,1),opacity .25s;
    opacity:0;z-index:99;white-space:nowrap;box-shadow:0 8px 24px rgba(0,0,0,.4);}
  #toast.show{transform:translateX(-50%) translateY(0);opacity:1;}
  .card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:12px;}
  .card-title{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:12px;}
  button{display:inline-flex;align-items:center;justify-content:center;gap:6px;border:none;cursor:pointer;
    font-family:inherit;font-weight:600;border-radius:var(--radius-sm);transition:all .12s ease;-webkit-user-select:none;user-select:none;}
  button:active{transform:scale(.92);}
  .row-top{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px;}
  .btn-power{background:rgba(240,90,90,.15);color:var(--red);border:1px solid rgba(240,90,90,.25);padding:12px;font-size:14px;width:100%;}
  .btn-home {background:rgba(79,142,247,.12);color:var(--accent);border:1px solid rgba(79,142,247,.2);padding:12px;font-size:14px;width:100%;}
  .media-row{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;}
  .btn-media{background:var(--surface2);color:var(--text);border:1px solid var(--border);padding:12px 6px;font-size:18px;width:100%;}
  .btn-media.play{background:rgba(62,207,122,.12);color:var(--green);border-color:rgba(62,207,122,.25);font-size:20px;}
  .vol-row{display:grid;grid-template-columns:1fr auto 1fr;gap:8px;align-items:center;}
  .btn-vol {background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:14px;font-size:20px;width:100%;}
  .btn-mute{background:rgba(245,166,35,.1);color:var(--orange);border:1px solid rgba(245,166,35,.2);padding:14px 18px;font-size:18px;border-radius:var(--radius-sm);}
  .dpad{display:grid;grid-template-columns:1fr 1fr 1fr;grid-template-rows:1fr 1fr 1fr;gap:6px;width:220px;margin:0 auto;}
  .btn-nav{background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:0;width:100%;aspect-ratio:1;font-size:20px;}
  .btn-ok {background:linear-gradient(135deg,var(--accent),var(--accent2));border:none;color:#fff;font-size:13px;font-weight:700;width:100%;aspect-ratio:1;letter-spacing:.5px;}
  .btn-esc{background:rgba(255,255,255,.04);border:1px solid var(--border);color:var(--muted);font-size:12px;font-weight:600;width:100%;aspect-ratio:1;}
  .btn-space{background:rgba(255,255,255,.04);border:1px solid var(--border);color:var(--muted);font-size:12px;width:100%;aspect-ratio:1;}
  .sources-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}
  .btn-source{background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:12px 10px;font-size:13px;width:100%;}
  .sound-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;}
  .card-title-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;}
  .card-title-row .card-title{margin-bottom:0;}
  .macro-mgmt{font-size:11px;color:var(--muted);text-decoration:none;padding:3px 8px;border:1px solid var(--border);border-radius:99px;transition:all .15s;}
  .macro-mgmt:hover{color:var(--text);border-color:rgba(255,255,255,.2);}
  .macro-home-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;}
  .btn-macro{background:rgba(124,92,252,.12);color:var(--accent2);border:1px solid rgba(124,92,252,.3);padding:11px 8px;font-size:13px;font-weight:600;width:100%;}
  .btn-macro:hover{background:rgba(124,92,252,.22);}
  .macro-home-empty{font-size:12px;color:var(--muted);text-align:center;padding:8px 0;}
  .macro-home-empty a{color:var(--accent);text-decoration:none;}
  .numpad{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;max-width:220px;margin:0 auto;}
  .btn-digit{background:var(--surface2);border:1px solid var(--border);color:var(--text);
    padding:0;aspect-ratio:1;font-size:20px;font-weight:700;border-radius:var(--radius-sm);
    font-family:'SF Mono',monospace;letter-spacing:0;transition:all .12s;width:100%;}
  .btn-digit:hover{background:rgba(255,255,255,.07);border-color:rgba(255,255,255,.2);}
  .btn-digit:active{transform:scale(.91);background:var(--accent);color:#fff;border-color:var(--accent);}
  .btn-digit-zero{grid-column:1/3;aspect-ratio:auto;padding:14px 0;}
  .btn-digit-del{background:rgba(240,90,90,.12);color:var(--red);border-color:rgba(240,90,90,.25);}
  .btn-sound{background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:10px 6px;font-size:12px;width:100%;line-height:1.3;}
  .sendkey-row{display:flex;gap:8px;}
  .input-key{flex:1;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:11px 14px;border-radius:var(--radius-sm);font-size:14px;font-family:monospace;outline:none;transition:border-color .15s;}
  .input-key:focus{border-color:var(--accent);}
  .btn-send{background:rgba(79,142,247,.15);color:var(--accent);border:1px solid rgba(79,142,247,.25);padding:11px 18px;font-size:13px;}
  .kbd-hint{font-size:11px;color:var(--muted);text-align:center;margin-top:20px;line-height:1.8;}
  kbd{background:var(--surface2);border:1px solid var(--border);border-radius:5px;padding:1px 6px;font-size:11px;font-family:monospace;}
</style></head><body>
<div id="toast"></div>
<div class="header">
  <div><div class="header-title">📺 ESP32 Remote</div><div class="header-sub">HTTP · MQTT · Clavier</div></div>
  <div class="header-right">
    <div class="badges">
      <span class="badge badge-http">HTTP ✓</span>
      <span class="badge badge-mqtt" id="mqtt-badge">MQTT</span>
    </div>
    <a class="btn-config" href="/config" title="Configuration">⚙️</a>
  </div>
</div>
<div class="card"><div class="card-title">Système</div>
  <div class="row-top">
    <button class="btn-power" onclick="send('/power')">⏻ Power</button>
    <button class="btn-home"  onclick="send('/home')">⌂ Home</button>
  </div>
</div>
<div class="card"><div class="card-title">Lecture</div>
  <div class="media-row">
    <button class="btn-media" onclick="send('/previous')">⏮</button>
    <button class="btn-media" onclick="send('/voldown')">🔉</button>
    <button class="btn-media play" id="btn-playpause" onclick="send('/playpause')">⏯</button>
    <button class="btn-media" onclick="send('/volup')">🔊</button>
    <button class="btn-media" onclick="send('/next')">⏭</button>
  </div>
  <div style="height:8px"></div>
  <div class="vol-row">
    <button class="btn-vol" id="btn-voldec" onclick="send('/voldown')">－</button>
    <button class="btn-mute" id="btn-mute" onclick="send('/mute')">🔇</button>
    <button class="btn-vol" id="btn-volinc" onclick="send('/volup')">＋</button>
  </div>
</div>
<div class="card"><div class="card-title">Navigation</div>
  <div class="dpad">
    <div></div><button class="btn-nav" id="btn-up" onclick="send('/up')">▲</button><div></div>
    <button class="btn-nav" id="btn-left" onclick="send('/left')">◀</button>
    <button class="btn-ok" id="btn-enter" onclick="send('/enter')">OK</button>
    <button class="btn-nav" id="btn-right" onclick="send('/right')">▶</button>
    <button class="btn-esc" id="btn-esc" onclick="send('/esc')">ESC</button>
    <button class="btn-nav" id="btn-down" onclick="send('/down')">▼</button>
    <button class="btn-space" id="btn-space" onclick="send('/space')">SPC</button>
  </div>
</div>
<div class="card"><div class="card-title">Sources</div>
  <div class="sources-grid">
    <button class="btn-source" onclick="send('/antenne')">📡 Antenne</button>
    <button class="btn-source" onclick="send('/miracast')">📲 Miracast</button>
  </div>
</div>
<div class="card" id="macro-card">
  <div class="card-title-row">
    <span class="card-title">⚡ Macros</span>
    <a href="/macros" class="macro-mgmt">⚙ Gérer</a>
  </div>
  <div id="home-macro-list"><div class="macro-home-empty">Aucune macro — <a href="/macros">Créer</a></div></div>
</div>
<div class="card"><div class="card-title">Mode sonore</div>
  <div class="sound-grid">
    <button class="btn-sound" onclick="send('/sonoriginal')">🎙️ Original</button>
    <button class="btn-sound" onclick="send('/sondivertissement')">🎬 Divertiss.</button>
    <button class="btn-sound" onclick="send('/sonmusique')">🎵 Musique</button>
    <button class="btn-sound" onclick="send('/sonmusiquespatiale')">🌐 Spatiale</button>
    <button class="btn-sound" onclick="send('/sondialogue')">💬 Dialogue</button>
    <button class="btn-sound" onclick="send('/sonpersonnel')">⚙️ Perso.</button>
  </div>
</div>
<div class="card"><div class="card-title">Touche HID personnalisée</div>
  <div class="sendkey-row">
    <input class="input-key" id="sendkey-input" type="text" placeholder="0x04 ou 65">
    <button class="btn-send" onclick="sendCustomKey()">Envoyer</button>
  </div>
</div>
<div class="card"><div class="card-title">🔢 Pavé numérique</div>
  <div class="numpad">
    <button class="btn-digit" id="btn-d7" onclick="send('/digit7','btn-d7')">7</button>
    <button class="btn-digit" id="btn-d8" onclick="send('/digit8','btn-d8')">8</button>
    <button class="btn-digit" id="btn-d9" onclick="send('/digit9','btn-d9')">9</button>
    <button class="btn-digit" id="btn-d4" onclick="send('/digit4','btn-d4')">4</button>
    <button class="btn-digit" id="btn-d5" onclick="send('/digit5','btn-d5')">5</button>
    <button class="btn-digit" id="btn-d6" onclick="send('/digit6','btn-d6')">6</button>
    <button class="btn-digit" id="btn-d1" onclick="send('/digit1','btn-d1')">1</button>
    <button class="btn-digit" id="btn-d2" onclick="send('/digit2','btn-d2')">2</button>
    <button class="btn-digit" id="btn-d3" onclick="send('/digit3','btn-d3')">3</button>
    <button class="btn-digit btn-digit-zero" id="btn-d0" onclick="send('/digit0','btn-d0')">0</button>
    <button class="btn-digit btn-digit-del" onclick="sendCustomKey_del()">⌫</button>
  </div>
</div>
<div class="kbd-hint">
  <kbd>↑↓←→</kbd> nav · <kbd>Entrée</kbd> OK · <kbd>Échap</kbd> retour<br>
  <kbd>+</kbd>/<kbd>-</kbd> Vol · <kbd>M</kbd> Mute · <kbd>P</kbd> Play · <kbd>H</kbd> Home
</div>
<script>
const keyMap={'ArrowUp':{url:'/up',btn:'btn-up'},'ArrowDown':{url:'/down',btn:'btn-down'},
  'ArrowLeft':{url:'/left',btn:'btn-left'},'ArrowRight':{url:'/right',btn:'btn-right'},
  'Enter':{url:'/enter',btn:'btn-enter'},'Escape':{url:'/esc',btn:'btn-esc'},
  ' ':{url:'/space',btn:'btn-space'},'+':{url:'/volup',btn:'btn-volinc'},
  '-':{url:'/voldown',btn:'btn-voldec'},'m':{url:'/mute',btn:'btn-mute'},
  'M':{url:'/mute',btn:'btn-mute'},'p':{url:'/playpause',btn:'btn-playpause'},
  'P':{url:'/playpause',btn:'btn-playpause'},'h':{url:'/home'},'H':{url:'/home'},
  '0':{url:'/digit0',btn:'btn-d0'},'1':{url:'/digit1',btn:'btn-d1'},
  '2':{url:'/digit2',btn:'btn-d2'},'3':{url:'/digit3',btn:'btn-d3'},
  '4':{url:'/digit4',btn:'btn-d4'},'5':{url:'/digit5',btn:'btn-d5'},
  '6':{url:'/digit6',btn:'btn-d6'},'7':{url:'/digit7',btn:'btn-d7'},
  '8':{url:'/digit8',btn:'btn-d8'},'9':{url:'/digit9',btn:'btn-d9'}};
function sendCustomKey_del(){fetch('/sendkey',{method:'POST',
  headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'keycode=0xB2'}).then(()=>showToast('⌫ Backspace ✓')).catch(()=>showToast('❌ Erreur'));}
let busy=false,toastTimer;
function showToast(m){const t=document.getElementById('toast');t.textContent=m;t.classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.classList.remove('show'),1400);}
function flash(id){const el=document.getElementById(id);if(!el)return;el.style.transition='none';el.style.filter='brightness(2.5)';el.style.transform='scale(0.91)';setTimeout(()=>{el.style.transition='all .2s ease';el.style.filter='';el.style.transform='';},120);}
function send(url,btnId){if(busy)return;busy=true;if(btnId)flash(btnId);showToast(url.slice(1)+' ✓');fetch(url,{method:'POST'}).catch(()=>showToast('❌ Erreur réseau')).finally(()=>{busy=false;});}
function sendCustomKey(){const v=document.getElementById('sendkey-input').value.trim();if(!v)return;fetch('/sendkey',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'keycode='+encodeURIComponent(v)}).then(()=>showToast('key:'+v+' ✓')).catch(()=>showToast('❌ Erreur'));}
document.addEventListener('keydown',e=>{if(document.activeElement===document.getElementById('sendkey-input'))return;const a=keyMap[e.key];if(!a)return;e.preventDefault();send(a.url,a.btn);});
function checkMqtt(){fetch('/mqttstatus').then(r=>r.text()).then(s=>{const b=document.getElementById('mqtt-badge');if(s==='1'){b.textContent='MQTT ✓';b.classList.remove('offline');}else{b.textContent='MQTT ✗';b.classList.add('offline');}}).catch(()=>{});}
function runHomeMacro(n){fetch('/macro/run',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(n)}).then(r=>r.text()).then(t=>showToast(t==='OK'?'⚡ '+n:'⚠ '+t)).catch(()=>showToast('Erreur'));}
function loadHomeMacros(){fetch('/macro/list').then(r=>r.json()).then(function(data){var el=document.getElementById('home-macro-list');if(!data||!data.length){el.innerHTML='<div class="macro-home-empty">Aucune macro &mdash; <a href="/macros">Cr\u00e9er</a></div>';return;}var grid=document.createElement('div');grid.className='macro-home-grid';data.forEach(function(m){var btn=document.createElement('button');btn.className='btn-macro';btn.textContent='\u26a1 '+m.name;btn.onclick=function(){runHomeMacro(m.name);};grid.appendChild(btn);});el.innerHTML='';el.appendChild(grid);}).catch(function(){});}
loadHomeMacros();
checkMqtt();setInterval(checkMqtt,5000);
</script></body></html>
  )rawliteral");
}



// ── Macros ────────────────────────────────────────────────────────────────────
// Stockage NVS namespace "macros"
// mcr_list : "0,1,2"  (indices des slots utilisés)
// mcr_n_X  : nom de la macro X
// mcr_s_X  : steps "cmd1:delay1,cmd2:delay2,..."
// Max 10 macros, max 24 steps par macro

#define MAX_MACROS 10

// Retourne les indices utilisés (ex: [0,2,5])
std::vector<int> macroGetSlots() {
  std::vector<int> v;
  prefs.begin("macros", true);
  String list = prefs.getString("mcr_list", "");
  prefs.end();
  if (!list.length()) return v;
  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    String tok = (comma<0) ? list.substring(start) : list.substring(start, comma);
    tok.trim();
    if (tok.length()) v.push_back(tok.toInt());
    if (comma < 0) break;
    start = comma + 1;
  }
  return v;
}

void macroSaveSlots(const std::vector<int>& v) {
  String s = "";
  for (int i=0;i<(int)v.size();i++) { if(i) s+=","; s+=String(v[i]); }
  prefs.begin("macros", false);
  prefs.putString("mcr_list", s);
  prefs.end();
}

// Trouver le prochain slot libre (0–9)
int macroNextSlot(const std::vector<int>& used) {
  for (int i=0;i<MAX_MACROS;i++) {
    bool found=false;
    for (int u:used) if(u==i){found=true;break;}
    if (!found) return i;
  }
  return -1;
}

// Sauver une macro (name, steps = "antenne:500,digit1:300,enter:300")
// Retourne false si plus de slots dispo ou nom déjà pris → met à jour l'existant
bool macroSave(const String& name, const String& steps) {
  auto slots = macroGetSlots();
  // Mise à jour si nom existant
  for (int s : slots) {
    prefs.begin("macros", true);
    String n = prefs.getString(("mcr_n_"+String(s)).c_str(), "");
    prefs.end();
    if (n == name) {
      prefs.begin("macros", false);
      prefs.putString(("mcr_s_"+String(s)).c_str(), steps);
      prefs.end();
      return true;
    }
  }
  // Nouveau slot
  if ((int)slots.size() >= MAX_MACROS) return false;
  int slot = macroNextSlot(slots);
  if (slot < 0) return false;
  prefs.begin("macros", false);
  prefs.putString(("mcr_n_"+String(slot)).c_str(), name);
  prefs.putString(("mcr_s_"+String(slot)).c_str(), steps);
  prefs.end();
  slots.push_back(slot);
  macroSaveSlots(slots);
  return true;
}

bool macroDelete(const String& name) {
  auto slots = macroGetSlots();
  for (int i=0;i<(int)slots.size();i++) {
    int s = slots[i];
    prefs.begin("macros", true);
    String n = prefs.getString(("mcr_n_"+String(s)).c_str(), "");
    prefs.end();
    if (n == name) {
      prefs.begin("macros", false);
      prefs.remove(("mcr_n_"+String(s)).c_str());
      prefs.remove(("mcr_s_"+String(s)).c_str());
      prefs.end();
      slots.erase(slots.begin()+i);
      macroSaveSlots(slots);
      return true;
    }
  }
  return false;
}

// Exécuter une macro par nom — retourne false si introuvable
bool macroRun(const String& name) {
  auto slots = macroGetSlots();
  String steps = "";
  for (int s : slots) {
    prefs.begin("macros", true);
    String n = prefs.getString(("mcr_n_"+String(s)).c_str(), "");
    if (n == name) steps = prefs.getString(("mcr_s_"+String(s)).c_str(), "");
    prefs.end();
    if (steps.length()) break;
  }
  if (!steps.length()) return false;
  Serial.printf("[Macro] Run '%s' : %s\n", name.c_str(), steps.c_str());
  // Parser et exécuter les steps
  int pos = 0;
  while (pos < (int)steps.length()) {
    int comma = steps.indexOf(',', pos);
    String step = (comma<0) ? steps.substring(pos) : steps.substring(pos, comma);
    int colon = step.indexOf(':');
    String cmd = (colon<0) ? step : step.substring(0, colon);
    int ms  = (colon<0) ? 300 : step.substring(colon+1).toInt();
    cmd.trim();
    if (cmd == "delay") {
      delay(ms);
    } else {
      dispatchCommand(cmd, false);  // false = pas de redirectHome depuis ici
      delay(ms);
    }
    if (comma < 0) break;
    pos = comma + 1;
  }
  return true;
}

// Récupérer la liste des macros sous forme de JSON
String macroListJSON() {
  auto slots = macroGetSlots();
  String json = "[";
  bool first = true;
  for (int s : slots) {
    prefs.begin("macros", true);
    String n = prefs.getString(("mcr_n_"+String(s)).c_str(), "");
    String st = prefs.getString(("mcr_s_"+String(s)).c_str(), "");
    prefs.end();
    if (!n.length()) continue;
    if (!first) json += ",";
    // Compter les steps
    int cnt = 1;
    for (char c : st) if (c==',') cnt++;
    if (!st.length()) cnt = 0;
    json += "{\"name\":\"" + n + "\",\"steps\":\"" + st + "\",\"count\":" + String(cnt) + "}";
    first = false;
  }
  return json + "]";
}

// ── Page /macros ──────────────────────────────────────────────────────────────

void handleMacrosPage() {
  String html = "<!DOCTYPE html><html lang='fr'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Macros — ESP32 Remote</title><style>";
  html += ":root{--bg:#0f1117;--surface:#1a1d27;--surface2:#22263a;--border:rgba(255,255,255,.07);";
  html += "--text:#e8eaf0;--muted:#6b7080;--accent:#4f8ef7;--green:#3ecf7a;--red:#f05a5a;--radius:14px;--radius-sm:10px;}";
  html += "*{box-sizing:border-box;margin:0;padding:0;}";
  html += "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100dvh;padding:16px 12px 60px;max-width:520px;margin:0 auto;}";
  html += ".header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;}";
  html += ".title{font-size:18px;font-weight:700;}.sub{font-size:12px;color:var(--muted);margin-top:2px;}";
  html += "a.back{color:var(--muted);font-size:13px;text-decoration:none;padding:6px 12px;border:1px solid var(--border);border-radius:99px;}";
  html += "a.back:hover{color:var(--text);}";
  html += ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:18px;margin-bottom:12px;}";
  html += ".card-title{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:14px;}";
  // Macro list
  html += ".macro-item{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid var(--border);}";
  html += ".macro-item:last-child{border:none;}";
  html += ".macro-name{flex:1;font-size:14px;font-weight:600;}";
  html += ".macro-count{font-size:11px;color:var(--muted);margin-top:2px;}";
  html += ".btn-run{background:rgba(79,142,247,.15);color:var(--accent);border:1px solid rgba(79,142,247,.3);padding:6px 12px;border-radius:var(--radius-sm);font-size:12px;font-weight:600;cursor:pointer;transition:all .15s;}";
  html += ".btn-run:hover{background:rgba(79,142,247,.3);}";
  html += ".btn-edit{background:rgba(255,255,255,.05);color:var(--muted);border:1px solid var(--border);padding:6px 12px;border-radius:var(--radius-sm);font-size:12px;cursor:pointer;transition:all .15s;}";
  html += ".btn-edit:hover{color:var(--text);}";
  html += ".btn-del{background:rgba(240,90,90,.1);color:var(--red);border:1px solid rgba(240,90,90,.2);padding:6px 10px;border-radius:var(--radius-sm);font-size:13px;cursor:pointer;transition:all .15s;}";
  html += ".btn-del:hover{background:rgba(240,90,90,.25);}";
  // Editor
  html += ".field{margin-bottom:12px;}";
  html += ".field label{display:block;font-size:11px;color:var(--muted);margin-bottom:5px;font-weight:600;text-transform:uppercase;letter-spacing:.8px;}";
  html += "input.inp{width:100%;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:10px 12px;border-radius:var(--radius-sm);font-size:14px;outline:none;}";
  html += "input.inp:focus{border-color:var(--accent);}";
  html += "select.sel{width:100%;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:10px 12px;border-radius:var(--radius-sm);font-size:14px;outline:none;cursor:pointer;}";
  html += "select.sel:focus{border-color:var(--accent);}";
  // Steps list
  html += ".steps-list{display:flex;flex-direction:column;gap:6px;margin-bottom:12px;min-height:20px;}";
  html += ".step-item{display:flex;align-items:center;gap:8px;background:var(--surface2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 10px;}";
  html += ".step-num{font-size:10px;color:var(--muted);min-width:18px;text-align:center;}";
  html += ".step-cmd{flex:1;font-size:13px;font-family:monospace;font-weight:600;}";
  html += ".step-delay{font-size:11px;color:var(--muted);padding:2px 6px;background:var(--surface);border-radius:99px;}";
  html += ".step-rm{color:var(--red);font-size:16px;cursor:pointer;padding:0 4px;opacity:.6;}";
  html += ".step-rm:hover{opacity:1;}";
  // Add step row
  html += ".add-row{display:flex;gap:8px;align-items:center;margin-bottom:12px;}";
  html += ".add-row select{flex:1;}";
  html += ".delay-inp{width:80px;background:var(--surface2);border:1px solid var(--border);color:var(--text);padding:10px 8px;border-radius:var(--radius-sm);font-size:13px;text-align:center;outline:none;}";
  html += ".delay-inp:focus{border-color:var(--accent);}";
  html += ".btn-add{background:rgba(62,207,122,.12);color:var(--green);border:1px solid rgba(62,207,122,.25);padding:10px 14px;border-radius:var(--radius-sm);font-size:18px;cursor:pointer;}";
  html += ".btn-add:hover{background:rgba(62,207,122,.25);}";
  // Save / new buttons
  html += ".btn-save{width:100%;background:var(--accent);color:#fff;border:none;padding:13px;border-radius:var(--radius-sm);font-size:15px;font-weight:700;cursor:pointer;margin-top:4px;transition:all .15s;}";
  html += ".btn-save:hover{background:#3a7de8;}";
  html += ".btn-new{width:100%;background:rgba(255,255,255,.04);color:var(--muted);border:1px solid var(--border);padding:11px;border-radius:var(--radius-sm);font-size:13px;cursor:pointer;margin-top:8px;}";
  html += ".btn-new:hover{color:var(--text);}";
  html += ".toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%) translateY(20px);background:#1e2130;color:var(--text);padding:10px 18px;border-radius:99px;font-size:13px;opacity:0;transition:all .2s;pointer-events:none;border:1px solid var(--border);}";
  html += ".toast.show{opacity:1;transform:translateX(-50%) translateY(0);}";
  html += ".empty{text-align:center;padding:24px;color:var(--muted);font-size:13px;}";
  html += "</style></head><body>";

  html += "<div class='header'><div><div class='title'>⚡ Macros</div>";
  html += "<div class='sub'>Séquences de commandes automatiques</div></div>";
  html += "<a class='back' href='/'>← Retour</a></div>";

  // Liste des macros existantes
  html += "<div class='card' id='list-card'><div class='card-title'>Macros enregistrées</div>";
  html += "<div id='macro-list'><div class='empty'>Aucune macro — créez-en une ci-dessous</div></div></div>";

  // Éditeur
  html += "<div class='card'><div class='card-title' id='editor-title'>✏️ Nouvelle macro</div>";
  html += "<div class='field'><label>Nom</label><input class='inp' id='macro-name' placeholder='Ex: Antenne 16' maxlength='20'></div>";
  html += "<div class='card-title' style='margin-top:4px'>Étapes</div>";
  html += "<div class='steps-list' id='steps-list'></div>";
  html += "<div class='add-row'>";
  html += "<select class='sel' id='cmd-select'>";

  // Groupes de commandes
  const char* groups[][20] = {
    {"── Navigation ──","",""},
    {"up","Haut ▲",""},{"down","Bas ▼",""},{"left","Gauche ◀",""},{"right","Droite ▶",""},
    {"enter","OK ↩",""},{"esc","Retour ⎋",""},{"space","Espace",""},
    {"── Système ──","",""},
    {"power","Power ⏻",""},{"home","Home 🏠",""},{"wake","Wake 🌅",""},
    {"── Volume ──","",""},
    {"volup","Volume + 🔊",""},{"voldown","Volume - 🔉",""},{"mute","Mute 🔇",""},{"volhuit","Volume 8",""},
    {"── Lecture ──","",""},
    {"playpause","Play/Pause ⏯",""},{"next","Suivant ⏭",""},{"previous","Précédent ⏮",""},
    {"── Sources ──","",""},
    {"antenne","Antenne 📡",""},{"miracast","Miracast 📺",""},
    {"── Pavé ──","",""},
    {"digit0","Chiffre 0",""},{"digit1","Chiffre 1",""},{"digit2","Chiffre 2",""},
    {"digit3","Chiffre 3",""},{"digit4","Chiffre 4",""},{"digit5","Chiffre 5",""},
    {"digit6","Chiffre 6",""},{"digit7","Chiffre 7",""},{"digit8","Chiffre 8",""},{"digit9","Chiffre 9",""},
    {"── Modes son ──","",""},
    {"sonoriginal","Son Original",""},{"sondivertissement","Son Divertissement",""},
    {"sonmusique","Son Musique",""},{"sonmusiquespatiale","Son Spatiale",""},
    {"sondialogue","Son Dialogue",""},{"sonpersonnel","Son Perso",""},
    {"── Pause ──","",""},
    {"delay","⏱ Pause (ms)",""},
    {nullptr,nullptr,nullptr}
  };
  for (int i=0; groups[i][0]!=nullptr; i++) {
    String val = String(groups[i][0]);
    String lbl = String(groups[i][1]);
    if (lbl.length()==0) {
      html += "<option disabled>" + val + "</option>";
    } else {
      html += "<option value='" + val + "'>" + lbl + "</option>";
    }
  }
  html += "</select>";
  html += "<input class='delay-inp' id='step-delay' type='number' value='300' min='50' max='9999' title='Délai après (ms)'>";
  html += "<button class='btn-add' onclick='addStep()' title='Ajouter l\\'étape'>＋</button>";
  html += "</div>"; // add-row
  html += "<button class='btn-save' onclick='saveMacro()'>💾 Enregistrer</button>";
  html += "<button class='btn-new' onclick='newMacro()'>＋ Nouvelle macro</button>";
  html += "</div>"; // card éditeur

  html += "<div class='toast' id='toast'></div>";

  html += R"js(<script>
let steps=[], editingName=null;
const ALL_CMDS={
  up:'▲ Haut',down:'▼ Bas',left:'◀ Gauche',right:'▶ Droite',enter:'↩ OK',esc:'⎋ Retour',space:'⎵ Espace',
  power:'⏻ Power',home:'🏠 Home',wake:'🌅 Wake',
  volup:'🔊 Vol+',voldown:'🔉 Vol−',mute:'🔇 Mute',volhuit:'🔈 Vol8',
  playpause:'⏯ Play',next:'⏭ Next',previous:'⏮ Prev',
  antenne:'📡 Antenne',miracast:'📺 Miracast',
  digit0:'0',digit1:'1',digit2:'2',digit3:'3',digit4:'4',
  digit5:'5',digit6:'6',digit7:'7',digit8:'8',digit9:'9',
  sonoriginal:'🔊 Original',sondivertissement:'🎬 Divert.',sonmusique:'🎵 Musique',
  sonmusiquespatiale:'🌐 Spatiale',sondialogue:'💬 Dialogue',sonpersonnel:'⚙️ Perso',
  delay:'⏱ Pause'
};

let toastT;
function toast(m){const t=document.getElementById('toast');t.textContent=m;t.classList.add('show');clearTimeout(toastT);toastT=setTimeout(()=>t.classList.remove('show'),1800);}

function renderSteps(){
  const el=document.getElementById('steps-list');
  if(!steps.length){el.innerHTML='<div style="color:var(--muted);font-size:12px;padding:8px 0">Aucune étape — ajoutez des commandes ci-dessous</div>';return;}
  el.innerHTML=steps.map((s,i)=>`
    <div class="step-item">
      <span class="step-num">${i+1}</span>
      <span class="step-cmd">${ALL_CMDS[s.cmd]||s.cmd}</span>
      <span class="step-delay">${s.delay}ms</span>
      <span class="step-rm" onclick="removeStep(${i})">✕</span>
    </div>`).join('');
}

function addStep(){
  const cmd=document.getElementById('cmd-select').value;
  const d=parseInt(document.getElementById('step-delay').value)||300;
  if(!cmd||cmd.startsWith('──'))return;
  steps.push({cmd,delay:d});
  renderSteps();
}

function removeStep(i){steps.splice(i,1);renderSteps();}

function stepsToString(){return steps.map(s=>s.cmd+':'+s.delay).join(',');}

function saveMacro(){
  const name=document.getElementById('macro-name').value.trim();
  if(!name){toast('⚠️ Donnez un nom à la macro');return;}
  if(!steps.length){toast('⚠️ Ajoutez au moins une étape');return;}
  const body='name='+encodeURIComponent(name)+'&steps='+encodeURIComponent(stepsToString());
  fetch('/macro/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(r=>r.text()).then(()=>{toast('✅ Macro "'+name+'" enregistrée');loadMacros();newMacro();})
    .catch(()=>toast('❌ Erreur réseau'));
}

function runMacro(name){
  toast('▶ Exécution de "'+name+'"…');
  fetch('/macro/run',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)})
    .then(r=>r.text()).then(r=>toast(r==='OK'?'✅ "'+name+'" terminée':'❌ '+r))
    .catch(()=>toast('❌ Erreur réseau'));
}

function deleteMacro(name){
  if(!confirm('Supprimer "'+name+'" ?'))return;
  fetch('/macro/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)})
    .then(()=>{toast('🗑 "'+name+'" supprimée');loadMacros();})
    .catch(()=>toast('❌ Erreur réseau'));
}

function editMacro(name,stepsStr){
  editingName=name;
  document.getElementById('macro-name').value=name;
  document.getElementById('editor-title').textContent='✏️ Modifier — '+name;
  steps=stepsStr.length?stepsStr.split(',').map(s=>{const c=s.indexOf(':');return{cmd:s.substring(0,c),delay:parseInt(s.substring(c+1))||300};}):[];
  renderSteps();
  document.getElementById('macro-name').scrollIntoView({behavior:'smooth'});
}

function newMacro(){
  editingName=null;
  document.getElementById('macro-name').value='';
  document.getElementById('editor-title').textContent='✏️ Nouvelle macro';
  steps=[];renderSteps();
}

function loadMacros(){
  fetch('/macro/list').then(r=>r.json()).then(data=>{
    const el=document.getElementById('macro-list');
    if(!data.length){el.innerHTML="<div class='empty'>Aucune macro — créez-en une ci-dessous</div>";return;}
    el.innerHTML=data.map(m=>`
      <div class="macro-item">
        <div style="flex:1">
          <div class="macro-name">⚡ ${m.name}</div>
          <div class="macro-count">${m.count} étape${m.count>1?'s':''}</div>
        </div>
        <button class="btn-run" onclick="runMacro('${m.name.replace(/'/g,"\\'")}')">▶ Run</button>
        <button class="btn-edit" onclick="editMacro('${m.name.replace(/'/g,"\\'")}','${m.steps.replace(/'/g,"\\'")}')">✏️</button>
        <button class="btn-del" onclick="deleteMacro('${m.name.replace(/'/g,"\\'")}')">🗑</button>
      </div>`).join('');
  }).catch(()=>{});
}
loadMacros();
renderSteps();
</script>)js";
  html += "</body></html>";
  server.send(200,"text/html",html);
}

void handleMacroSave() {
  if (!server.hasArg("name") || !server.hasArg("steps")) { server.send(400,"text/plain","missing args"); return; }
  String name  = server.arg("name");
  String steps = server.arg("steps");
  name.trim();
  if (!name.length()) { server.send(400,"text/plain","empty name"); return; }
  if (macroSave(name, steps)) {
    // Republier discovery HA pour inclure la nouvelle macro
    publishHADiscovery();
    server.send(200,"text/plain","OK");
  } else {
    server.send(507,"text/plain","Trop de macros (max 10)");
  }
}

void handleMacroRun() {
  if (!server.hasArg("name")) { server.send(400,"text/plain","missing name"); return; }
  String name = server.arg("name");
  if (macroRun(name)) server.send(200,"text/plain","OK");
  else server.send(404,"text/plain","Macro introuvable : "+name);
}

void handleMacroDelete() {
  if (!server.hasArg("name")) { server.send(400,"text/plain","missing name"); return; }
  String name = server.arg("name");
  macroDelete(name);
  publishHADiscovery();
  server.send(200,"text/plain","OK");
}

void handleMacroList() {
  server.send(200,"application/json", macroListJSON());
}

// ── OTA Web ───────────────────────────────────────────────────────────────────

static bool   otaSuccess = false;
static String otaError   = "";
static size_t otaWritten = 0;

void handleUpdateGet() {
  String ip = WiFi.localIP().toString();
  String html = "<!DOCTYPE html><html lang='fr'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>OTA — ESP32 Remote</title><style>";
  html += ":root{--bg:#0f1117;--surface:#1a1d27;--surface2:#22263a;--border:rgba(255,255,255,.07);";
  html += "--text:#e8eaf0;--muted:#6b7080;--accent:#4f8ef7;--green:#3ecf7a;--red:#f05a5a;--radius:14px;--radius-sm:10px;}";
  html += "*{box-sizing:border-box;margin:0;padding:0;}";
  html += "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100dvh;padding:16px 12px 40px;max-width:480px;margin:0 auto;}";
  html += ".header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;}";
  html += ".title{font-size:18px;font-weight:700;}.sub{font-size:12px;color:var(--muted);margin-top:2px;}";
  html += "a.back{display:inline-flex;align-items:center;gap:6px;color:var(--muted);font-size:13px;text-decoration:none;padding:6px 12px;border:1px solid var(--border);border-radius:99px;transition:all .15s;}";
  html += "a.back:hover{color:var(--text);border-color:rgba(255,255,255,.2);}";
  html += ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:20px;margin-bottom:12px;}";
  html += ".card-title{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:16px;}";
  html += ".drop-zone{border:2px dashed var(--border);border-radius:var(--radius-sm);padding:40px 20px;text-align:center;cursor:pointer;transition:all .2s;position:relative;}";
  html += ".drop-zone.dragover{border-color:var(--accent);background:rgba(79,142,247,.06);}";
  html += ".drop-zone.has-file{border-color:rgba(62,207,122,.4);background:rgba(62,207,122,.05);}";
  html += ".drop-icon{font-size:36px;margin-bottom:10px;display:block;}";
  html += ".drop-label{font-size:14px;color:var(--muted);margin-bottom:4px;}";
  html += ".drop-hint{font-size:12px;color:var(--muted);opacity:.6;}";
  html += ".drop-file-name{font-size:14px;font-weight:600;color:var(--green);margin-top:6px;font-family:monospace;}";
  html += ".drop-file-size{font-size:11px;color:var(--muted);margin-top:2px;}";
  html += "#file-input{position:absolute;inset:0;opacity:0;cursor:pointer;width:100%;height:100%;}";
  html += ".btn-upload{width:100%;background:var(--accent);color:#fff;border:none;padding:14px;border-radius:var(--radius-sm);font-size:15px;font-weight:700;cursor:pointer;margin-top:12px;transition:all .15s;display:flex;align-items:center;justify-content:center;gap:8px;}";
  html += ".btn-upload:disabled{opacity:.35;cursor:not-allowed;transform:none;}";
  html += ".btn-upload:hover:not(:disabled){background:#3a7de8;}";
  html += ".progress-wrap{margin-top:16px;display:none;}";
  html += ".progress-track{height:8px;background:var(--surface2);border-radius:99px;overflow:hidden;border:1px solid var(--border);}";
  html += ".progress-bar{height:100%;background:linear-gradient(90deg,var(--accent),#7c5cfc);border-radius:99px;width:0%;transition:width .15s ease;}";
  html += ".progress-label{font-size:12px;color:var(--muted);text-align:right;margin-top:5px;}";
  html += ".result{border-radius:var(--radius-sm);padding:14px 16px;font-size:14px;line-height:1.6;margin-top:12px;display:none;}";
  html += ".result.ok{background:rgba(62,207,122,.1);border:1px solid rgba(62,207,122,.3);color:var(--green);}";
  html += ".result.err{background:rgba(240,90,90,.1);border:1px solid rgba(240,90,90,.3);color:var(--red);}";
  html += ".info{background:rgba(79,142,247,.07);border:1px solid rgba(79,142,247,.15);border-radius:var(--radius-sm);padding:12px 14px;font-size:12px;color:var(--muted);line-height:1.7;}";
  html += ".info strong{color:var(--accent);}";
  html += ".kv{display:flex;justify-content:space-between;font-size:12px;padding:5px 0;border-bottom:1px solid var(--border);}";
  html += ".kv:last-child{border:none;}.kv-k{color:var(--muted);}.kv-v{font-family:monospace;color:var(--text);font-weight:600;}";
  html += "</style></head><body>";
  html += "<div class='header'><div><div class='title'>🔄 Mise à jour OTA</div>";
  html += "<div class='sub'>ESP32 Remote &nbsp;·&nbsp; v" FW_VERSION "</div></div>";
  html += "<a class='back' href='/config'>← Config</a></div>";
  html += "<div class='card'><div class='card-title'>📋 Informations</div>";
  html += "<div class='kv'><span class='kv-k'>Version actuelle</span><span class='kv-v'>v" FW_VERSION "</span></div>";
  html += "<div class='kv'><span class='kv-k'>Hostname</span><span class='kv-v'>" + String(cfg_hostname) + "</span></div>";
  html += "<div class='kv'><span class='kv-k'>IP</span><span class='kv-v'>" + ip + "</span></div>";
  html += "<div class='kv'><span class='kv-k'>Flash libre</span><span class='kv-v'>" + String(ESP.getFreeSketchSpace()/1024) + " Ko</span></div>";
  html += "<div class='kv'><span class='kv-k'>Sketch actuel</span><span class='kv-v'>" + String(ESP.getSketchSize()/1024) + " Ko</span></div>";
  html += "</div>";
  html += "<div class='card'><div class='card-title'>📦 Firmware (.bin)</div>";
  html += "<div class='drop-zone' id='drop' ondragover='onDragOver(event)' ondragleave='onDragLeave()' ondrop='onDrop(event)'>";
  html += "<input type='file' id='file-input' accept='.bin' onchange='onFileChange(this)'>";
  html += "<span class='drop-icon' id='drop-icon'>📂</span>";
  html += "<div class='drop-label' id='drop-label'>Glissez votre .bin ici</div>";
  html += "<div class='drop-hint' id='drop-hint'>ou cliquez pour choisir un fichier</div>";
  html += "<div class='drop-file-name' id='file-name'></div>";
  html += "<div class='drop-file-size' id='file-size'></div></div>";
  html += "<button class='btn-upload' id='btn-upload' disabled onclick='startUpload()'>⬆️ Flasher le firmware</button>";
  html += "<div class='progress-wrap' id='progress-wrap'>";
  html += "<div class='progress-track'><div class='progress-bar' id='progress-bar'></div></div>";
  html += "<div class='progress-label' id='progress-label'>0%</div></div>";
  html += "<div class='result' id='result'></div></div>";
  html += "<div class='info'>⚠️ <strong>Ne pas couper l'alimentation</strong> pendant la mise à jour.<br>";
  html += "L'ESP redémarre automatiquement après un flash réussi.<br>";
  html += "Générez le .bin dans Arduino IDE : <em>Sketch → Export Compiled Binary</em>.</div>";
  html += R"js(<script>
let sel=null;
function fmt(b){return b<1024?b+' o':b<1048576?(b/1024).toFixed(1)+' Ko':(b/1048576).toFixed(2)+' Mo';}
function onFileChange(i){if(i.files[0])setFile(i.files[0]);}
function onDragOver(e){e.preventDefault();document.getElementById('drop').classList.add('dragover');}
function onDragLeave(){document.getElementById('drop').classList.remove('dragover');}
function onDrop(e){e.preventDefault();onDragLeave();const f=e.dataTransfer.files[0];if(f&&f.name.endsWith('.bin'))setFile(f);else alert('Fichier .bin requis');}
function setFile(f){
  sel=f;
  document.getElementById('drop').classList.add('has-file');
  document.getElementById('drop-icon').textContent='✅';
  document.getElementById('drop-label').textContent='Fichier prêt';
  document.getElementById('drop-hint').textContent='';
  document.getElementById('file-name').textContent=f.name;
  document.getElementById('file-size').textContent=fmt(f.size);
  document.getElementById('btn-upload').disabled=false;
}
function startUpload(){
  if(!sel)return;
  document.getElementById('btn-upload').disabled=true;
  document.getElementById('btn-upload').innerHTML='⏳ Flash en cours…';
  document.getElementById('progress-wrap').style.display='block';
  document.getElementById('result').style.display='none';
  const fd=new FormData();fd.append('firmware',sel,sel.name);
  const xhr=new XMLHttpRequest();
  xhr.upload.addEventListener('progress',e=>{
    if(!e.lengthComputable)return;
    const p=Math.round(e.loaded/e.total*100);
    document.getElementById('progress-bar').style.width=p+'%';
    document.getElementById('progress-label').textContent=p+'% — '+fmt(e.loaded)+' / '+fmt(e.total);
  });
  xhr.addEventListener('load',()=>{
    const r=document.getElementById('result');r.style.display='block';
    if(xhr.status===200){
      r.className='result ok';
      r.innerHTML='✅ <strong>Flash réussi !</strong> Redémarrage en cours…<br><small>Reconnectez-vous dans quelques secondes.</small>';
      document.getElementById('progress-bar').style.background='var(--green)';
    } else {
      r.className='result err';
      r.innerHTML='❌ <strong>Erreur :</strong> '+xhr.responseText;
      document.getElementById('btn-upload').disabled=false;
      document.getElementById('btn-upload').innerHTML='⬆️ Réessayer';
    }
  });
  xhr.addEventListener('error',()=>{
    const r=document.getElementById('result');r.style.display='block';
    r.className='result err';r.innerHTML='❌ Erreur réseau.';
    document.getElementById('btn-upload').disabled=false;
    document.getElementById('btn-upload').innerHTML='⬆️ Réessayer';
  });
  xhr.open('POST','/update');xhr.send(fd);
}
</script>)js";
  html += "</body></html>";
  server.send(200,"text/html",html);
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaSuccess = false; otaError = ""; otaWritten = 0;
    Serial.printf("[OTA] Debut : %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      otaError = Update.errorString();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaError.length()) {
      size_t w = Update.write(upload.buf, upload.currentSize);
      otaWritten += w;
      if (w != upload.currentSize) otaError = Update.errorString();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaError.length()) {
      if (Update.end(true)) { otaSuccess = true; Serial.printf("[OTA] OK %u octets\n", otaWritten); }
      else otaError = Update.errorString();
    }
    if (otaError.length()) Serial.printf("[OTA] Erreur : %s\n", otaError.c_str());
  }
}

void handleUpdatePost() {
  if (otaSuccess) {
    server.send(200,"text/plain","OK");
    delay(500); ESP.restart();
  } else {
    server.send(500,"text/plain","Erreur : " + otaError);
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  USB.begin(); ConsumerControl.begin(); Keyboard.begin();
  Serial.begin(115200);
  loadConfig();
  loadBootCount();

  WiFi.mode(WIFI_STA);

  // Appliquer l'IP fixe AVANT WiFi.begin()
  if (cfg_static_ip && isValidIP(cfg_ip) && isValidIP(cfg_gw)) {
    IPAddress ip   = parseIP(cfg_ip);
    IPAddress gw   = parseIP(cfg_gw);
    IPAddress mask = isValidIP(cfg_mask) ? parseIP(cfg_mask) : IPAddress(255,255,255,0);
    IPAddress dns  = isValidIP(cfg_dns)  ? parseIP(cfg_dns)  : IPAddress(8,8,8,8);
    if (WiFi.config(ip, gw, mask, dns)) {
      Serial.printf("[WiFi] IP fixe : %s / GW : %s\n", cfg_ip, cfg_gw);
    } else {
      Serial.println("[WiFi] WiFi.config() échoué → DHCP");
    }
  } else if (cfg_static_ip) {
    Serial.println("[WiFi] IP fixe demandée mais invalide → DHCP");
  }

  WiFi.begin(cfg_ssid, cfg_password);
  Serial.print("[WiFi] Connexion");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) { delay(500); Serial.print("."); }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] OK — %s (%s)\n",
      WiFi.localIP().toString().c_str(),
      cfg_static_ip ? "IP fixe" : "DHCP");
    MDNS.begin(cfg_hostname);
    mqtt.setServer(cfg_mqtt_host, cfg_mqtt_port);
    mqtt.setCallback(mqttCallback);
    mqtt.setKeepAlive(30);
    ntpSync();
    mqttConnect();
    ArduinoOTA.setHostname(cfg_hostname);
    ArduinoOTA.onEnd([]() { Serial.println("OTA OK"); });
    ArduinoOTA.onProgress([](unsigned int p,unsigned int t){ Serial.printf("OTA %u%%\r",p/(t/100)); });
    ArduinoOTA.onError([](ota_error_t e){ Serial.printf("OTA err %d\n",e); });
    ArduinoOTA.begin();
  } else {
    Serial.println("\n[WiFi] Échec → mode AP");
    startAPMode();
  }

  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/config",        HTTP_GET,  handleConfigGet);
  server.on("/config",        HTTP_POST, handleConfigPost);
  server.on("/config/reset",  HTTP_POST, handleConfigReset);
  server.on("/mqttstatus",    HTTP_GET,  []() { server.send(200,"text/plain",mqtt.connected()?"1":"0"); });

  if (apMode) {
    server.on("/generate_204",        HTTP_GET, handleCaptivePortal);
    server.on("/fwlink",              HTTP_GET, handleCaptivePortal);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
    server.on("/connecttest.txt",     HTTP_GET, handleCaptivePortal);
    server.onNotFound(handleCaptivePortal);
  } else {
    server.on("/volup",              HTTP_POST, handle_volup);
    server.on("/voldown",            HTTP_POST, handle_voldown);
    server.on("/mute",               HTTP_POST, handle_mute);
    server.on("/playpause",          HTTP_POST, handle_playpause);
    server.on("/next",               HTTP_POST, handle_next);
    server.on("/previous",           HTTP_POST, handle_previous);
    server.on("/home",               HTTP_POST, handle_home);
    server.on("/power",              HTTP_POST, handle_power);
    server.on("/wake",               HTTP_POST, handle_wake);
    server.on("/up",                 HTTP_POST, handle_up);
    server.on("/down",               HTTP_POST, handle_down);
    server.on("/left",               HTTP_POST, handle_left);
    server.on("/right",              HTTP_POST, handle_right);
    server.on("/enter",              HTTP_POST, handle_enter);
    server.on("/space",              HTTP_POST, handle_space);
    server.on("/esc",                HTTP_POST, handle_esc);
    server.on("/antenne",            HTTP_POST, handle_antenne);
    server.on("/miracast",           HTTP_POST, handle_miracast);
    server.on("/volhuit",            HTTP_POST, handle_volhuit);
    server.on("/son",                HTTP_POST, handle_son);
    server.on("/sonoriginal",        HTTP_POST, handle_sonoriginal);
    server.on("/sondivertissement",  HTTP_POST, handle_sondivertissement);
    server.on("/sonmusique",         HTTP_POST, handle_sonmusique);
    server.on("/sonmusiquespatiale", HTTP_POST, handle_sonmusiquespatiale);
    server.on("/sondialogue",        HTTP_POST, handle_sondialogue);
    server.on("/sonpersonnel",       HTTP_POST, handle_sonpersonnel);
    for(int d=0;d<=9;d++){
      String route="/digit"+String(d);
      switch(d){
        case 0:server.on(route.c_str(),HTTP_POST,handle_digit0);break;
        case 1:server.on(route.c_str(),HTTP_POST,handle_digit1);break;
        case 2:server.on(route.c_str(),HTTP_POST,handle_digit2);break;
        case 3:server.on(route.c_str(),HTTP_POST,handle_digit3);break;
        case 4:server.on(route.c_str(),HTTP_POST,handle_digit4);break;
        case 5:server.on(route.c_str(),HTTP_POST,handle_digit5);break;
        case 6:server.on(route.c_str(),HTTP_POST,handle_digit6);break;
        case 7:server.on(route.c_str(),HTTP_POST,handle_digit7);break;
        case 8:server.on(route.c_str(),HTTP_POST,handle_digit8);break;
        case 9:server.on(route.c_str(),HTTP_POST,handle_digit9);break;
      }
    }
    server.on("/vol", HTTP_GET, []() {
      if (server.hasArg("val")) { setVolume(server.arg("val").toInt()); server.send(200,"text/plain","ok"); }
      else server.send(400,"text/plain","?val= manquant");
    });
    server.on("/macros",       HTTP_GET,  handleMacrosPage);
    server.on("/macro/save",   HTTP_POST, handleMacroSave);
    server.on("/macro/run",    HTTP_POST, handleMacroRun);
    server.on("/macro/delete", HTTP_POST, handleMacroDelete);
    server.on("/macro/list",   HTTP_GET,  handleMacroList);
    server.on("/update", HTTP_GET,  handleUpdateGet);
    server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
    server.on("/sendkey", HTTP_POST, []() {
      if (server.hasArg("keycode")) {
        String ks=server.arg("keycode");
        pressMediaKey((ks.startsWith("0x")||ks.startsWith("0X"))?strtol(ks.c_str(),nullptr,16):ks.toInt());
      }
      redirectHome();
    });
  }

  server.begin();
  Serial.println(apMode
    ? "[HTTP] Portail captif → 192.168.4.1"
    : "[HTTP] Prêt → http://" + String(cfg_hostname) + ".local");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  static unsigned long lastTelemetry = 0;
  if (apMode) {
    dnsServer.processNextRequest();
  } else {
    if (WiFi.status()==WL_CONNECTED && !mqtt.connected()) mqttConnect();
    mqtt.loop();
    ArduinoOTA.handle();
    // Télémétrie toutes les 60s
    if (millis() - lastTelemetry > 60000UL) {
      lastTelemetry = millis();
      publishTelemetry();
    }
  }
  server.handleClient();
}
