#include "ota_update.h"

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "config.h"
#include "config_eeprom.h"
#include "ota_session.h"
#include "wifi_link.h"
#include "phone_ble.h"

// Distinguish the production board from both other ESP32-S3 products and the
// bench pin map. This descriptor also stays in every future OTA build.
extern const char ffs_ota_device_marker[32]
    __attribute__((section(".rodata_custom_desc"), used)) =
#ifdef BENCH_SDA
    "FIREFLY-SENSE-BENCH-OTA1";
#else
    "FIREFLY-SENSE-N16R8-OTA1";
#endif

namespace {
class FlashWriter : public OtaWriter {
 public:
  bool begin(size_t size) override { return Update.begin(size, U_FLASH); }
  size_t write(uint8_t* data, size_t size) override { return Update.write(data, size); }
  bool end() override { return Update.end(false); }
  void abort() override { Update.abort(); }
} s_writer;
OtaSession s_session(s_writer);
WebServer* s_server = nullptr;
String s_nonce;
bool s_rebootPending = false;
uint32_t s_rebootAt = 0;

const char kUpdateHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FIREFLY-SENSE · 固件升级</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0b1220;color:#e6edf7;font:15px/1.6 system-ui;padding:24px}
main{max-width:620px;margin:6vh auto;background:#111c31;border:1px solid #24344f;border-radius:14px;padding:24px}
h1{font-size:21px;margin:0 0 10px}p{color:#9caec9}a{color:#38bdf8}button,input{font:inherit}
input{width:100%;margin:12px 0}button{padding:10px 18px;border:0;border-radius:8px;background:#38bdf8;color:#0b1220;cursor:pointer}
button:disabled{opacity:.45;cursor:default}progress{width:100%;margin-top:18px}#status{white-space:pre-wrap}
</style><main><a href="/">← 返回感知面板</a><h1>FIREFLY-SENSE · 固件升级</h1>
<p id="device">正在读取设备信息…</p>
<p>选择 FIREFLY-SENSE 整板应用固件（.bin）。升级期间保持供电，完成后设备自动重启。</p>
<input id="file" type="file" accept=".bin"><button id="start" disabled>开始升级</button>
<progress id="progress" max="100" value="0"></progress><p id="status" role="status">等待选择固件</p></main>
<script>
const file=document.querySelector('#file'),button=document.querySelector('#start'),status=document.querySelector('#status');
let info=null,busy=false;
async function getInfo(){const r=await fetch('/device',{cache:'no-store'});if(!r.ok)throw Error('设备信息读取失败');return r.json();}
getInfo().then(x=>{info=x;document.querySelector('#device').textContent='当前版本 '+x.firmware_version+' · '+x.model;
 if(!x.ota_supported)status.textContent='当前设备分区不支持 OTA';button.disabled=!file.files.length||!x.ota_supported;})
 .catch(e=>status.textContent=e.message);
file.onchange=()=>{button.disabled=busy||!info||!info.ota_supported||!file.files.length;status.textContent=file.files.length?file.files[0].name:'等待选择固件';};
const errors={'image-too-small':'文件过小，不是完整应用固件','image-too-large':'固件超出设备可用空间',
 'wrong-device-image':'请选择 FIREFLY-SENSE 整板应用固件，不能使用其他设备或合并固件',
 'length-mismatch':'传输长度不一致','incomplete-upload':'上传未完成，请重新选择固件',
 'flash-begin-failed':'无法准备升级分区','flash-write-failed':'固件写入失败','image-verification-failed':'固件校验失败',
 'request-rejected':'升级请求已失效，请刷新页面','no-ota-partition':'设备缺少 OTA 分区','unsupported-content-type':'上传格式错误'};
button.onclick=async()=>{const f=file.files[0];if(!f||busy)return;
 if(!/\.bin$/i.test(f.name)){status.textContent='请选择 .bin 应用固件';return;}
 if(f.size<320||f.size>info.max_firmware_bytes){status.textContent='固件大小不符合设备要求';return;}
 busy=true;button.disabled=true;file.disabled=true;status.textContent='正在上传固件…';
 try{info=await getInfo();const result=await new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();
 xhr.open('POST','/device/firmware');xhr.timeout=180000;xhr.setRequestHeader('Content-Type','application/octet-stream');
 xhr.setRequestHeader('X-FFS-OTA-Token',info.ota_nonce);
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);document.querySelector('#progress').value=n;
 status.textContent=n===100?'上传完成，正在校验…':'正在上传 '+n+'%';}};
 xhr.onload=()=>{let d;try{d=JSON.parse(xhr.responseText);}catch{reject(Error('设备响应异常'));return;}
 if(xhr.status===200&&d.ok)resolve(d);else reject(Error(errors[d.error]||d.error||'升级失败'));};
 xhr.onerror=()=>reject(Error('连接中断，请等待设备恢复后重试'));xhr.ontimeout=()=>reject(Error('上传超时，请重试'));xhr.send(f);});
 status.textContent='校验通过，设备正在重启…';document.querySelector('#progress').value=100;
 const deadline=Date.now()+90000;await new Promise(r=>setTimeout(r,5000));
 let ready=false;while(Date.now()<deadline){try{const d=await getInfo();if(d.boot_partition!==info.boot_partition){
 document.querySelector('#device').textContent='当前版本 '+d.firmware_version+' · '+d.model;
 status.textContent='升级完成，设备已重新连接';ready=true;info=d;break;}}catch{}await new Promise(r=>setTimeout(r,2000));}
 if(!ready)status.textContent='固件已写入，暂未重新连接；请稍后刷新页面确认';
 }catch(e){status.textContent=e.message;}finally{busy=false;file.disabled=false;button.disabled=!info||!info.ota_supported||!file.files.length;}
};
</script></html>
)HTML";

void jsonReply(int code, const String& body) {
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->sendHeader("X-Content-Type-Options", "nosniff");
  s_server->send(code, "application/json; charset=utf-8", body);
}

void handleDevice() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  String body = "{\"model\":\"FIREFLY-SENSE N16R8\",\"firmware_version\":\"";
  body += kFirmwareVersion;
  body += "\",\"schema_version\":" + String(kSensorSchemaVersion);
  body += ",\"uptime_ms\":" + String(millis());
  body += ",\"reset_reason_code\":" + String(static_cast<int>(esp_reset_reason()));
  body += ",\"ble_protocol\":1,\"phone_ble_ready\":";
  body += phoneBleReady() ? "true" : "false";
  body += ",\"eeprom_valid\":";
  body += g_configValid ? "true" : "false";
  body += ",\"boot_partition\":\"";
  body += running ? running->label : "";
  body += "\",\"ota_supported\":";
  body += next && running && next->address != running->address ? "true" : "false";
  body += ",\"max_firmware_bytes\":" + String(next ? next->size : 0);
  body += ",\"ota_nonce\":\"" + s_nonce + "\"}";
  jsonReply(200, body);
}

bool sameLanRequest() {
  const String ip = WiFi.localIP().toString();
  const String host = s_server->hostHeader();
  if (host != ip && host != ip + ":80") return false;
  const String origin = s_server->header("Origin");
  if (origin.length() && origin != "http://" + host) return false;
  const IPAddress peer = s_server->client().remoteIP();
  const IPAddress mask = WiFi.subnetMask();
  const IPAddress local = WiFi.localIP();
  for (unsigned i = 0; i < 4; ++i)
    if ((peer[i] & mask[i]) != (local[i] & mask[i])) return false;
  return s_server->header("X-FFS-OTA-Token") == s_nonce && !s_rebootPending;
}

void handleRawUpload() {
  HTTPRaw& raw = s_server->raw();
  if (raw.status == RAW_START) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    s_session.start(0, 0);  // reset any previous request before authorizing.
    if (!sameLanRequest()) { s_session.fail("request-rejected"); return; }
    if (s_server->header("Content-Type") != "application/octet-stream") {
      s_session.fail("unsupported-content-type"); return;
    }
    if (!next || !running || next->address == running->address) {
      s_session.fail("no-ota-partition"); return;
    }
    const String length = s_server->header("Content-Length");
    char* end = nullptr;
    const unsigned long size = strtoul(length.c_str(), &end, 10);
    if (!length.length() || !end || *end || size < kOtaPrefixSize || size > next->size) {
      s_session.fail(size > next->size ? "image-too-large" : "image-too-small"); return;
    }
    s_session.start(size, next->size);
  } else if (raw.status == RAW_WRITE) {
    s_session.write(raw.buf, raw.currentSize);
    // Keep the existing PC connection alive while WebServer consumes the body.
    wifiTick();
    delay(1);
  } else if (raw.status == RAW_END) {
    s_session.finish();
  } else if (raw.status == RAW_ABORTED) {
    s_session.fail("incomplete-upload");
  }
}

void handleUploadResult() {
  if (s_server->header("Content-Type") != "application/octet-stream" || s_rebootPending) {
    jsonReply(400, "{\"ok\":false,\"error\":\"unsupported-content-type\"}");
    return;
  }
  if (s_session.complete()) {
    jsonReply(200, "{\"ok\":true,\"rebooting\":true}");
    s_rebootPending = true;
    s_rebootAt = millis() + 1500;
    Serial.println("[ota] verified application; reboot pending.");
  } else {
    const char* error = s_session.error() ? s_session.error() : "incomplete-upload";
    jsonReply(strcmp(error, "request-rejected") == 0 ? 403 : 400,
              String("{\"ok\":false,\"error\":\"") + error + "\"}");
  }
}

// A raw-only handler keeps multipart/form submissions away from raw(), which
// is not initialized by WebServer for multipart requests.
class OtaRequestHandler : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return method == HTTP_POST && uri == "/device/firmware";
  }
  bool canRaw(String uri) override { return uri == "/device/firmware"; }
  bool handle(WebServer&, HTTPMethod, String) override {
    handleUploadResult();
    return true;
  }
  void raw(WebServer&, String, HTTPRaw&) override { handleRawUpload(); }
};
}  // namespace

bool otaAuthorizeControl() {
  return s_server && sameLanRequest();
}

void otaSetup(WebServer& server) {
  s_server = &server;
  char nonce[33];
  snprintf(nonce, sizeof(nonce), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  s_nonce = nonce;
  const char* headers[] = {"Origin", "X-FFS-OTA-Token", "Content-Length", "Content-Type"};
  server.collectHeaders(headers, 4);
  server.on("/device", HTTP_GET, handleDevice);
  server.on("/update", HTTP_GET, []() {
    s_server->sendHeader("Cache-Control", "no-store");
    s_server->sendHeader("Content-Security-Policy", "frame-ancestors 'none'");
    s_server->send_P(200, "text/html; charset=utf-8", kUpdateHtml);
  });
  server.addHandler(new OtaRequestHandler());
}

void otaTick() {
  if (s_rebootPending && static_cast<int32_t>(millis() - s_rebootAt) >= 0) ESP.restart();
}
