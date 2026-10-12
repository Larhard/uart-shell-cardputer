#include "usbserial.h"

#include <Arduino.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>
#include <stdarg.h>

#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

namespace usbserial {
namespace {

enum Chip : uint8_t { NONE, CDC, CH34X, CP210X, FTDI };
const char *CHIP_NAMES[] = {"", "CDC", "CH34x", "CP210x", "FTDI"};

constexpr size_t IN_SIZE = 512, OUT_SIZE = 256, CTRL_SIZE = 8 + 64;
constexpr TickType_t CTRL_WAIT = pdMS_TO_TICKS(500), OUT_WAIT = pdMS_TO_TICKS(200);
constexpr int MAX_DEVS = 6, MAX_PORTS = 8;

bool installed = false;
esp_err_t installErr = ESP_ERR_INVALID_STATE;  // result of the last start attempt
usb_host_client_handle_t client = nullptr;
StreamBufferHandle_t rx = nullptr;
SemaphoreHandle_t ctrlDone = nullptr, outDone = nullptr;

// Filled by the client task, drained by poll() in the main loop.
QueueHandle_t newDevs = nullptr;  // uint8_t bus addresses of newly enumerated devices
QueueHandle_t goneDevs = nullptr;  // usb_device_handle_t of removed devices

struct LogMsg { char s[80]; };
QueueHandle_t logQ = nullptr;

void logf(const char *fmt, ...) {
  if (!logQ) return;
  LogMsg m;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.s, sizeof m.s, fmt, ap);
  va_end(ap);
  xQueueSend(logQ, &m, 0);
}

// ESP-IDF's own USB host messages (enumeration, hub, controller errors) are copied into
// the terminal log; everything else goes to the previous log output unchanged.
vprintf_like_t prevVprintf = nullptr;

int idfLog(const char *fmt, va_list ap) {
  static const char *TAGS[] = {"ENUM", "USBH", "HUB", "EXT_HUB", "EXT_PORT", "HCD", "USB HOST", "usb_phy"};
  char line[160];
  va_list copy;
  va_copy(copy, ap);
  vsnprintf(line, sizeof line, fmt, copy);
  va_end(copy);
  // Format: "<ESC>[0;31mE (1234) TAG: message<ESC>[0m<LF>" (colors optional)
  const char *msg = strchr(line, ')');
  bool usb = false;
  for (const char *t : TAGS)
    if (msg && strncmp(msg + 2, t, strlen(t)) == 0) usb = true;
  if (usb) {
    char out[72];
    size_t n = 0;
    for (const char *c = msg + 2; *c && n < sizeof out - 1; c++) {
      if (*c == 0x1B) {  // skip color codes
        while (*c && *c != 'm') c++;
        if (!*c) break;
        continue;
      }
      if (*c == '\n' || *c == '\r') continue;
      out[n++] = *c;
    }
    out[n] = 0;
    logf("idf %s", out);
  }
  return prevVprintf ? prevVprintf(fmt, ap) : 0;
}

// Attached devices that have at least one serial port. They stay open so that the
// host reports their removal.
struct Dev {
  usb_device_handle_t h;
  uint8_t addr, hubPort, mps0, numIfaces;
  uint16_t vid, pid, bcd;
};
struct Port {
  uint8_t dev;  // index into devs
  Chip chip;
  uint8_t dataIf, commIf, epIn, epOut;
  uint16_t mpsIn;
};
Dev devs[MAX_DEVS];
Port ports[MAX_PORTS];
int devCount = 0, portCnt = 0, active = -1;
PortId pref = {0, 0, 0, 0};

// The active port, as used by the transfer and chip code below.
usb_device_handle_t dev = nullptr;
volatile bool gone = false;  // the active device was unplugged
bool ready = false;
Chip chip = NONE;
uint8_t dataIf = 0, commIf = 0, epIn = 0, epOut = 0, mps0 = 8;
uint16_t mpsIn = 64;
bool commClaimed = false;
uint16_t ftdiChannel = 0;  // 0 for single-port chips, interface+1 otherwise
bool ftdiH = false;        // FT2232H/FT4232H/FT232H: 120 MHz baud clock
uint16_t chVersion = 0;
usb_transfer_t *xIn = nullptr, *xOut = nullptr, *xCtrl = nullptr;
volatile bool inBusy = false, outBusy = false, ctrlBusy = false;

uint32_t lBaud = 115200;
uint8_t lBits = 8, lParity = 0, lStop = 1;
bool lDtr = false, lRts = false;

// --- USB host plumbing ------------------------------------------------------

void hostTask(void *) {
  for (;;) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}

void clientEvent(const usb_host_client_event_msg_t *m, void *) {
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    uint8_t addr = m->new_dev.address;
    xQueueSend(newDevs, &addr, 0);
  } else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    usb_device_handle_t h = m->dev_gone.dev_hdl;
    if (h == dev) gone = true;
    xQueueSend(goneDevs, &h, 0);
  }
}

void clientTask(void *) {
  for (;;) usb_host_client_handle_events(client, portMAX_DELAY);
}

void ctrlCb(usb_transfer_t *) {
  ctrlBusy = false;
  xSemaphoreGive(ctrlDone);
}

void outCb(usb_transfer_t *) {
  outBusy = false;
  xSemaphoreGive(outDone);
}

void submitIn() {
  if (!ready || gone || inBusy) return;
  xIn->num_bytes = IN_SIZE;
  inBusy = usb_host_transfer_submit(xIn) == ESP_OK;
}

void inCb(usb_transfer_t *t) {
  inBusy = false;
  if (t->status != USB_TRANSFER_STATUS_COMPLETED) return;  // stall, cancel or unplug
  const uint8_t *d = t->data_buffer;
  int n = t->actual_num_bytes;
  if (chip == FTDI) {  // every packet starts with 2 modem status bytes
    for (int off = 0; off < n; off += mpsIn) {
      int len = n - off < mpsIn ? n - off : mpsIn;
      if (len > 2) xStreamBufferSend(rx, d + off + 2, len - 2, 0);
    }
  } else if (n > 0) {
    xStreamBufferSend(rx, d, n, 0);
  }
  submitIn();
}

bool control(uint8_t type, uint8_t req, uint16_t value, uint16_t index, void *data = nullptr,
             uint16_t len = 0) {
  if (!dev || gone || ctrlBusy || len > CTRL_SIZE - 8) return false;
  auto *s = (usb_setup_packet_t *)xCtrl->data_buffer;
  s->bmRequestType = type;
  s->bRequest = req;
  s->wValue = value;
  s->wIndex = index;
  s->wLength = len;
  bool in = type & 0x80;
  if (!in && len) memcpy(xCtrl->data_buffer + 8, data, len);
  xCtrl->num_bytes = 8 + (in ? usb_round_up_to_mps(len, mps0) : len);
  xCtrl->device_handle = dev;
  xCtrl->bEndpointAddress = 0;
  xCtrl->callback = ctrlCb;
  xSemaphoreTake(ctrlDone, 0);
  ctrlBusy = true;
  if (usb_host_transfer_submit_control(client, xCtrl) != ESP_OK) {
    ctrlBusy = false;
    return false;
  }
  if (xSemaphoreTake(ctrlDone, CTRL_WAIT) != pdTRUE) return false;
  if (xCtrl->status != USB_TRANSFER_STATUS_COMPLETED) return false;
  if (in && len) memcpy(data, xCtrl->data_buffer + 8, len);
  return true;
}

// --- Chip specific line setup -------------------------------------------------

// WCH CH340/CH341 divisor, as in the Linux ch341 driver.
uint16_t ch341Divisor(uint32_t speed) {
  constexpr uint32_t CLK = 48000000;
  auto clkDiv = [](int ps, int fact) { return 1u << (12 - 3 * ps - fact); };
  if (speed < 46) speed = 46;
  if (speed > 2000000) speed = 2000000;
  int fact = 1, ps;
  for (ps = 3; ps >= 0; ps--)
    if (speed > CLK / (clkDiv(ps, 1) * 512)) break;
  if (ps < 0) return 0;
  uint32_t cd = clkDiv(ps, fact);
  uint32_t div = CLK / (cd * speed);
  if (div < 9 || div > 255) {
    div /= 2;
    cd *= 2;
    fact = 0;
  }
  if (div < 2) return 0;
  if (16 * CLK / (cd * div) - 16 * speed >= 16 * speed - 16 * CLK / (cd * (div + 1))) div++;
  if (fact == 1 && div % 2 == 0) {
    div /= 2;
    fact = 0;
  }
  return (0x100 - div) << 8 | fact << 2 | ps;
}

// FTDI divisor (BM/R chips: 48 MHz, H chips: 120 MHz), as in the Linux ftdi_sio driver.
uint32_t ftdiDivisor(uint32_t baud) {
  static const uint8_t frac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
  if (ftdiH && baud >= 1200) {
    uint32_t d3 = (8u * 120000000 + 5 * baud) / (10 * baud);
    return (d3 >> 3) | (uint32_t)frac[d3 & 7] << 14 | (1u << 17);
  }
  uint32_t d3 = (48000000 + baud) / (2 * baud);
  uint32_t d = (d3 >> 3) | (uint32_t)frac[d3 & 7] << 14;
  if (d == 1) d = 0;
  else if (d == 0x4001) d = 1;
  return d;
}

bool applyLine() {
  switch (chip) {
    case CDC: {
      uint8_t lc[7] = {(uint8_t)lBaud, (uint8_t)(lBaud >> 8), (uint8_t)(lBaud >> 16),
                       (uint8_t)(lBaud >> 24), (uint8_t)(lStop == 2 ? 2 : 0), lParity, lBits};
      return control(0x21, 0x20, 0, commIf, lc, sizeof lc);  // SET_LINE_CODING
    }
    case CH34X: {
      uint16_t div = ch341Divisor(lBaud);
      if (!div) return false;
      if (chVersion > 0x27) div |= 0x80;
      if (!control(0x40, 0x9A, 0x1312, div)) return false;
      if (chVersion < 0x30) return true;  // old chips are fixed at 8N1
      uint8_t lcr = 0xC0 | (lBits == 7 ? 0x02 : 0x03) | (lParity ? 0x08 : 0) |
                    (lParity == 2 ? 0x10 : 0) | (lStop == 2 ? 0x04 : 0);
      return control(0x40, 0x9A, 0x2518, lcr);
    }
    case CP210X: {
      uint8_t b[4] = {(uint8_t)lBaud, (uint8_t)(lBaud >> 8), (uint8_t)(lBaud >> 16), (uint8_t)(lBaud >> 24)};
      if (!control(0x41, 0x1E, 0, dataIf, b, 4)) return false;  // SET_BAUDRATE
      return control(0x41, 0x03, lBits << 8 | lParity << 4 | (lStop == 2 ? 2 : 0), dataIf);  // SET_LINE_CTL
    }
    case FTDI: {
      uint32_t d = ftdiDivisor(lBaud);
      uint16_t index = d >> 16;
      if (ftdiChannel) index = index << 8 | ftdiChannel;
      if (!control(0x40, 3, d & 0xFFFF, index)) return false;  // SET_BAUDRATE
      return control(0x40, 4, lBits | lParity << 8 | (lStop == 2 ? 2 : 0) << 11, ftdiChannel);  // SET_DATA
    }
    default: return false;
  }
}

bool applyModem() {
  uint16_t dtr = lDtr ? 1 : 0, rts = lRts ? 2 : 0;
  switch (chip) {
    case CDC: return control(0x21, 0x22, dtr | rts, commIf);  // SET_CONTROL_LINE_STATE
    case CH34X: return control(0x40, 0xA4, (uint16_t) ~((lDtr ? 0x20 : 0) | (lRts ? 0x40 : 0)), 0);
    case CP210X: return control(0x41, 0x07, 0x0300 | dtr | rts, dataIf);  // SET_MHS
    case FTDI: return control(0x40, 1, 0x0300 | dtr | rts, ftdiChannel);  // MODEM_CTRL
    default: return false;
  }
}

// --- Ports --------------------------------------------------------------------

PortId idOf(int i) {
  const Dev &d = devs[ports[i].dev];
  return {d.vid, d.pid, ports[i].dataIf, d.hubPort};
}

// How well port i matches the preferred port (higher is better).
int prefScore(int i) {
  PortId id = idOf(i);
  if (!pref.vid || id.vid != pref.vid || id.pid != pref.pid) return 0;
  if (id.iface != pref.iface) return 1;
  return id.hubPort == pref.hubPort ? 3 : 2;
}

void label(int i, char *out, size_t n) {
  const Dev &d = devs[ports[i].dev];
  int len = snprintf(out, n, "%s %04x:%04x", CHIP_NAMES[ports[i].chip], d.vid, d.pid);
  int perDev = 0;
  for (int j = 0; j < portCnt; j++) perDev += ports[j].dev == ports[i].dev;
  if (perDev > 1 && len < (int)n) len += snprintf(out + len, n - len, " if%u", ports[i].dataIf);
  if (d.hubPort && len < (int)n) snprintf(out + len, n - len, " hub%u", d.hubPort);
}

// Stop the active port's transfers and release it; the device itself stays open.
void deactivate() {
  ready = false;
  if (!dev) return;
  bool haltIn = inBusy, haltOut = outBusy;
  if (haltIn) {
    usb_host_endpoint_halt(dev, epIn);
    usb_host_endpoint_flush(dev, epIn);
  }
  if (haltOut) {
    usb_host_endpoint_halt(dev, epOut);
    usb_host_endpoint_flush(dev, epOut);
  }
  for (int i = 0; i < 50 && (inBusy || outBusy || ctrlBusy); i++) vTaskDelay(pdMS_TO_TICKS(2));
  if (!gone) {  // endpoints stay halted after a flush; clear them for the next user
    if (haltIn) usb_host_endpoint_clear(dev, epIn);
    if (haltOut) usb_host_endpoint_clear(dev, epOut);
  }
  // Interfaces must be released even after an unplug, or the device cannot be closed.
  usb_host_interface_release(client, dev, dataIf);
  if (commClaimed) usb_host_interface_release(client, dev, commIf);
  dev = nullptr;
  chip = NONE;
  commClaimed = false;
  inBusy = false;
  outBusy = false;
  ctrlBusy = false;
  gone = false;
  active = -1;
}

bool activate(int i) {
  const Port &p = ports[i];
  const Dev &d = devs[p.dev];
  dev = d.h;
  gone = false;
  chip = p.chip;
  dataIf = p.dataIf;
  commIf = p.commIf;
  epIn = p.epIn;
  epOut = p.epOut;
  mpsIn = p.mpsIn ? p.mpsIn : 64;
  mps0 = d.mps0;
  esp_err_t err = usb_host_interface_claim(client, dev, dataIf, 0);
  if (err != ESP_OK) {
    logf("claim iface %u failed: %s", dataIf, esp_err_to_name(err));
    dev = nullptr;
    chip = NONE;
    return false;
  }
  if (chip == CDC && commIf != dataIf)
    commClaimed = usb_host_interface_claim(client, dev, commIf, 0) == ESP_OK;
  xIn->device_handle = xOut->device_handle = dev;
  xIn->bEndpointAddress = epIn;
  xOut->bEndpointAddress = epOut;

  switch (chip) {
    case CH34X: {
      uint8_t v[2] = {0, 0};
      control(0xC0, 0x5F, 0, 0, v, 2);  // read version
      chVersion = v[0];
      control(0x40, 0xA1, 0, 0);  // serial init
      break;
    }
    case CP210X:
      control(0x41, 0x00, 1, dataIf);  // IFC_ENABLE
      break;
    case FTDI:
      ftdiChannel = d.numIfaces > 1 ? dataIf + 1 : 0;
      ftdiH = d.bcd == 0x0700 || d.bcd == 0x0800 || d.bcd == 0x0900;
      control(0x40, 0, 0, ftdiChannel);  // reset
      control(0x40, 2, 0, ftdiChannel);  // no flow control
      control(0x40, 9, 2, ftdiChannel);  // 2 ms latency timer
      break;
    default: break;
  }
  if (!applyLine()) logf("line setup (baud/format) failed");
  if (!applyModem()) logf("DTR/RTS setup failed");
  flushInput();
  active = i;
  ready = true;
  submitIn();
  return true;
}

// Open a new device and record its serial ports. Devices without any (hubs, sound
// chips, HID) are closed again.
void scanDevice(uint8_t addr) {
  if (devCount >= MAX_DEVS) {
    logf("addr %u: too many devices, ignored", addr);
    return;
  }
  usb_device_handle_t h;
  esp_err_t err = usb_host_device_open(client, addr, &h);
  if (err != ESP_OK) {
    logf("addr %u: open failed: %s", addr, esp_err_to_name(err));
    return;
  }
  const usb_device_desc_t *dd;
  const usb_config_desc_t *cd;
  usb_device_info_t info;
  usb_host_get_device_descriptor(h, &dd);
  usb_host_get_active_config_descriptor(h, &cd);
  uint8_t hubPort = usb_host_device_info(h, &info) == ESP_OK && info.parent.dev_hdl ? info.parent.port_num : 0;

  Dev d = {h, addr, hubPort, dd->bMaxPacketSize0, cd->bNumInterfaces, dd->idVendor, dd->idProduct, dd->bcdDevice};
  int devIdx = devCount, found = 0;
  bool isHub = dd->bDeviceClass == 0x09;

  // Walk the configuration: every alt-0 interface with a bulk IN/OUT pair the driver
  // knows how to talk to becomes a port.
  const uint8_t *p = (const uint8_t *)cd;
  int total = cd->wTotalLength;
  int lastComm = -1;
  Port cur = {};
  bool inIface = false;
  uint8_t cls = 0;
  auto finish = [&]() {
    if (!inIface || !cur.epIn || !cur.epOut || portCnt >= MAX_PORTS) return;
    Chip c = NONE;
    if (cls == 0x0A) c = CDC;
    else if (cls == 0xFF && d.vid == 0x1A86) c = CH34X;
    else if (cls == 0xFF && d.vid == 0x10C4) c = CP210X;
    else if (cls == 0xFF && d.vid == 0x0403) c = FTDI;
    else if (cls == 0xFF && lastComm >= 0 && lastComm == cur.dataIf - 1) c = CDC;
    if (c == NONE) return;
    cur.chip = c;
    cur.dev = devIdx;
    cur.commIf = (c == CDC && lastComm >= 0) ? lastComm : cur.dataIf;
    ports[portCnt++] = cur;
    found++;
  };
  for (int off = 0; off + 2 <= total;) {
    uint8_t len = p[off], type = p[off + 1];
    if (len < 2 || off + len > total) break;
    if (type == 0x04 && len >= 9) {  // interface
      finish();
      inIface = p[off + 3] == 0;  // alternate setting 0 only
      cls = p[off + 5];
      if (cls == 0x09) isHub = true;
      cur = {};
      cur.dataIf = p[off + 2];
      if (inIface && cls == 0x02) lastComm = cur.dataIf;
    } else if (type == 0x05 && len >= 7 && inIface && (p[off + 3] & 3) == 2) {  // bulk endpoint
      uint8_t ep = p[off + 2];
      if (ep & 0x80) {
        if (!cur.epIn) cur.epIn = ep, cur.mpsIn = p[off + 4] | p[off + 5] << 8;
      } else if (!cur.epOut) {
        cur.epOut = ep;
      }
    }
    off += len;
  }
  finish();

  if (!found) {
    logf("%04x:%04x %s, skipped", d.vid, d.pid, isHub ? "hub" : "no serial port");
    usb_host_device_close(client, h);
    return;
  }
  devs[devCount++] = d;
  for (int i = portCnt - found; i < portCnt; i++) {
    char l[40];
    label(i, l, sizeof l);
    logf("port %d: %s", i + 1, l);
  }
}

// A device was unplugged: drop its ports, close it. Returns true if it held the active port.
bool removeDevice(usb_device_handle_t h) {
  int di = -1;
  for (int i = 0; i < devCount; i++)
    if (devs[i].h == h) di = i;
  if (di < 0) return false;
  bool wasActive = active >= 0 && ports[active].dev == di;
  if (wasActive) deactivate();
  int activeDev = active >= 0 ? ports[active].dev : -1;
  uint8_t activeIf = active >= 0 ? ports[active].dataIf : 0;
  // Compact ports and devices, keeping indexes consistent.
  int n = 0;
  for (int i = 0; i < portCnt; i++) {
    if (ports[i].dev == di) continue;
    Port q = ports[i];
    if (q.dev > di) q.dev--;
    ports[n++] = q;
  }
  portCnt = n;
  for (int i = di; i + 1 < devCount; i++) devs[i] = devs[i + 1];
  devCount--;
  if (activeDev >= 0) {
    if (activeDev > di) activeDev--;
    for (int i = 0; i < portCnt; i++)
      if (ports[i].dev == activeDev && ports[i].dataIf == activeIf) active = i;
  }
  usb_host_device_close(client, h);
  logf(wasActive ? "active port unplugged" : "device unplugged");
  return wasActive;
}

int bestPort() {
  int best = 0, score = -1;
  for (int i = 0; i < portCnt; i++) {
    int s = prefScore(i);
    if (s > score) best = i, score = s;
  }
  return portCnt ? best : -1;
}

}  // namespace

void begin() {
  if (installed) return;
  if (!logQ) {
    logQ = xQueueCreate(16, sizeof(LogMsg));
    rx = xStreamBufferCreate(8192, 1);
    newDevs = xQueueCreate(8, sizeof(uint8_t));
    goneDevs = xQueueCreate(8, sizeof(usb_device_handle_t));
    ctrlDone = xSemaphoreCreateBinary();
    outDone = xSemaphoreCreateBinary();
  }
  if (!prevVprintf) prevVprintf = esp_log_set_vprintf(idfLog);
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  installErr = usb_host_install(&hc);
  if (installErr != ESP_OK) {
    logf("host start failed: %s", esp_err_to_name(installErr));
    return;
  }
  xTaskCreatePinnedToCore(hostTask, "usbh", 4096, nullptr, 3, nullptr, 0);
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 8;
  cc.async.client_event_callback = clientEvent;
  cc.async.callback_arg = nullptr;
  installErr = usb_host_client_register(&cc, &client);
  if (installErr != ESP_OK) {
    logf("client register failed: %s", esp_err_to_name(installErr));
    return;
  }
  usb_host_transfer_alloc(IN_SIZE, 0, &xIn);
  usb_host_transfer_alloc(OUT_SIZE, 0, &xOut);
  usb_host_transfer_alloc(CTRL_SIZE, 0, &xCtrl);
  xIn->callback = inCb;
  xOut->callback = outCb;
  xTaskCreatePinnedToCore(clientTask, "usbc", 4096, nullptr, 2, nullptr, 0);
  installed = true;
  logf("host started, waiting for a device");
}

bool connected() { return ready; }
const char *name() { return CHIP_NAMES[ready ? chip : NONE]; }

int portCount() { return portCnt; }
int activePort() { return ready ? active : -1; }
void portLabel(int i, char *out, size_t n) {
  if (i >= 0 && i < portCnt) label(i, out, n);
  else snprintf(out, n, "(none)");
}
PortId portId(int i) { return i >= 0 && i < portCnt ? idOf(i) : PortId{0, 0, 0, 0}; }
void setPreferred(const PortId &id) { pref = id; }

bool selectPort(int i) {
  if (i < 0 || i >= portCnt) return false;
  pref = idOf(i);
  if (i == active && ready) return true;
  deactivate();
  return activate(i);
}

void setLine(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  lBaud = baud;
  lBits = dataBits;
  lParity = parity;
  lStop = stopBits;
  if (ready) applyLine();
}

void setBaud(uint32_t baud) { setLine(baud, lBits, lParity, lStop); }

void setLines(bool dtr, bool rts) {
  lDtr = dtr;
  lRts = rts;
  if (ready) applyModem();
}

size_t read(uint8_t *buf, size_t n) {
  if (!installed) return 0;
  return xStreamBufferReceive(rx, buf, n, 0);
}

size_t write(const uint8_t *buf, size_t n) {
  size_t done = 0;
  while (ready && !gone && done < n && !outBusy) {
    size_t c = n - done < OUT_SIZE ? n - done : OUT_SIZE;
    memcpy(xOut->data_buffer, buf + done, c);
    xOut->num_bytes = c;
    xSemaphoreTake(outDone, 0);
    outBusy = true;
    if (usb_host_transfer_submit(xOut) != ESP_OK) {
      outBusy = false;
      break;
    }
    // If the device does not take the data in time the transfer stays queued; further
    // writes are dropped until it completes (e.g. CDC devices waiting for DTR).
    if (xSemaphoreTake(outDone, OUT_WAIT) != pdTRUE) break;
    if (xOut->status != USB_TRANSFER_STATUS_COMPLETED) break;
    done += c;
  }
  return done;
}

void flushInput() {
  uint8_t tmp[64];
  while (installed && xStreamBufferReceive(rx, tmp, sizeof tmp, 0)) {}
}

Event poll() {
  if (!installed) return EV_NONE;
  Event ev = EV_NONE;
  usb_device_handle_t h;
  while (xQueueReceive(goneDevs, &h, 0) == pdTRUE)
    if (removeDevice(h)) ev = EV_DISCONNECTED;

  uint8_t addr;
  bool added = false;
  while (xQueueReceive(newDevs, &addr, 0) == pdTRUE) {
    scanDevice(addr);
    added = true;
  }

  // Connect when nothing is active, or switch when the preferred port just appeared.
  int want = bestPort();
  bool switchToPref = added && ready && want >= 0 && want != active && prefScore(want) == 3 &&
                      prefScore(active) < 3;
  if (want >= 0 && (!ready || switchToPref)) {
    deactivate();
    if (activate(want)) ev = EV_CONNECTED;
  }
  return ev;
}

bool nextLog(char *out, size_t n) {
  LogMsg m;
  if (!logQ || xQueueReceive(logQ, &m, 0) != pdTRUE) return false;
  snprintf(out, n, "%s", m.s);
  return true;
}

void report() {
  if (!installed) {
    logf("host not running (%s)", esp_err_to_name(installErr));
    return;
  }
  usb_host_lib_info_t li = {};
  usb_host_lib_info(&li);
  logf("host ok, %d device(s) on the bus", li.num_devices);
  uint8_t addrs[12];
  int n = 0;
  usb_host_device_addr_list_fill(sizeof addrs, addrs, &n);
  for (int i = 0; i < n; i++) {
    usb_device_handle_t h = nullptr;
    bool mine = false;
    for (int j = 0; j < devCount; j++)
      if (devs[j].addr == addrs[i]) h = devs[j].h, mine = true;
    if (!mine && usb_host_device_open(client, addrs[i], &h) != ESP_OK) continue;
    const usb_device_desc_t *dd;
    usb_host_get_device_descriptor(h, &dd);
    logf("addr %u: %04x:%04x class %02x", addrs[i], dd->idVendor, dd->idProduct, dd->bDeviceClass);
    if (!mine) usb_host_device_close(client, h);
  }
  if (!li.num_devices) logf("nothing on the bus: check OTG cable / 5V power");
  else if (n < li.num_devices)
    logf("%d device(s) stuck in enumeration (see idf lines)", li.num_devices - n);
  for (int i = 0; i < portCnt; i++) {
    char l[40];
    label(i, l, sizeof l);
    logf("%c port %d: %s", i == active && ready ? '*' : ' ', i + 1, l);
  }
}

}  // namespace usbserial
