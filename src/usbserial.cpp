#include "usbserial.h"

#include <Arduino.h>
#include <esp_err.h>
#include <stdarg.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>

#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

namespace usbserial {
namespace {

enum Chip : uint8_t { NONE, CDC, CH34X, CP210X, FTDI };
const char *CHIP_NAMES[] = {"", "CDC", "CH34x", "CP210x", "FTDI"};

constexpr size_t IN_SIZE = 512, OUT_SIZE = 256, CTRL_SIZE = 8 + 64;
constexpr TickType_t CTRL_WAIT = pdMS_TO_TICKS(500), OUT_WAIT = pdMS_TO_TICKS(200);

bool installed = false;
usb_host_client_handle_t client = nullptr;
StreamBufferHandle_t rx = nullptr;
SemaphoreHandle_t ctrlDone = nullptr, outDone = nullptr;

// Addresses of newly enumerated devices, queued by the client task and opened by poll().
// Behind a hub several devices show up at once (hub, serial chip, sound chip, ...).
QueueHandle_t newDevs = nullptr;

struct LogMsg { char s[48]; };
QueueHandle_t logQ = nullptr;
esp_err_t installErr = ESP_ERR_INVALID_STATE;  // result of the last start attempt

void logf(const char *fmt, ...) {
  if (!logQ) return;
  LogMsg m;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.s, sizeof m.s, fmt, ap);
  va_end(ap);
  xQueueSend(logQ, &m, 0);
}
volatile bool gone = false;

usb_device_handle_t dev = nullptr;
uint8_t devAddr = 0;  // bus address of dev
Chip chip = NONE;
bool ready = false;
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
    logf("device attached (addr %u)", addr);
  } else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (dev && m->dev_gone.dev_hdl == dev) gone = true;
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

// --- Device lifecycle ---------------------------------------------------------

struct Iface {
  uint8_t num, cls, in, out;
  uint16_t mpsIn;
};

bool findInterface(const usb_config_desc_t *cd, Iface &pick, int &commNum) {
  const uint8_t *p = (const uint8_t *)cd;
  int total = cd->wTotalLength;
  Iface cur = {}, best = {};
  bool have = false, bestValid = false;
  uint8_t alt = 0;
  commNum = -1;
  auto consider = [&]() {
    if (!have || !cur.in || !cur.out) return;
    if (!bestValid || (cur.cls == 0x0A && best.cls != 0x0A)) {
      best = cur;
      bestValid = true;
    }
  };
  for (int off = 0; off + 2 <= total;) {
    uint8_t len = p[off], type = p[off + 1];
    if (len < 2 || off + len > total) break;
    if (type == 0x04 && len >= 9) {  // interface
      consider();
      alt = p[off + 3];
      cur = {p[off + 2], p[off + 5], 0, 0, 0};
      have = alt == 0;
      if (cur.cls == 0x02 && commNum < 0) commNum = cur.num;
    } else if (type == 0x05 && len >= 7 && have) {  // endpoint
      uint8_t ep = p[off + 2];
      if ((p[off + 3] & 3) == 2) {  // bulk
        if (ep & 0x80) {
          if (!cur.in) cur.in = ep, cur.mpsIn = p[off + 4] | p[off + 5] << 8;
        } else if (!cur.out) {
          cur.out = ep;
        }
      }
    }
    off += len;
  }
  consider();
  pick = best;
  return bestValid;
}

bool hasClass(const usb_config_desc_t *cd, uint8_t cls) {
  const uint8_t *p = (const uint8_t *)cd;
  for (int off = 0; off + 9 <= cd->wTotalLength && p[off] >= 2; off += p[off])
    if (p[off + 1] == 0x04 && p[off + 5] == cls) return true;
  return false;
}

void closeDevice() {
  ready = false;
  if (!dev) return;
  if (inBusy) {
    usb_host_endpoint_halt(dev, epIn);
    usb_host_endpoint_flush(dev, epIn);
  }
  if (outBusy) {
    usb_host_endpoint_halt(dev, epOut);
    usb_host_endpoint_flush(dev, epOut);
  }
  for (int i = 0; i < 50 && (inBusy || outBusy || ctrlBusy); i++) vTaskDelay(pdMS_TO_TICKS(2));
  if (chip != NONE) usb_host_interface_release(client, dev, dataIf);
  if (commClaimed) usb_host_interface_release(client, dev, commIf);
  usb_host_device_close(client, dev);
  dev = nullptr;
  chip = NONE;
  commClaimed = false;
  inBusy = false;
  outBusy = false;
  ctrlBusy = false;
  gone = false;
}

// Returns EV_CONNECTED, EV_UNSUPPORTED, or EV_NONE if the device could not be opened.
int openDevice(uint8_t addr) {
  esp_err_t err = usb_host_device_open(client, addr, &dev);
  if (err != ESP_OK) {
    logf("addr %u: open failed: %s", addr, esp_err_to_name(err));
    dev = nullptr;
    return EV_NONE;
  }
  const usb_device_desc_t *dd;
  const usb_config_desc_t *cd;
  usb_host_get_device_descriptor(dev, &dd);
  usb_host_get_active_config_descriptor(dev, &cd);
  mps0 = dd->bMaxPacketSize0;
  devAddr = addr;
  logf("addr %u: %04x:%04x class %02x, %u ifaces", addr, dd->idVendor, dd->idProduct,
       dd->bDeviceClass, cd->bNumInterfaces);

  Iface ifc;
  int comm;
  if (!findInterface(cd, ifc, comm)) {
    // Hubs, sound chips and HID parts of a radio are expected; only report other devices.
    bool expected = dd->bDeviceClass == 0x09 || hasClass(cd, 0x01) || hasClass(cd, 0x03) || hasClass(cd, 0x09);
    logf("addr %u: %s, skipped", addr, dd->bDeviceClass == 0x09 || hasClass(cd, 0x09) ? "hub" : "no serial port");
    usb_host_device_close(client, dev);
    dev = nullptr;
    return expected ? EV_NONE : EV_UNSUPPORTED;
  }
  uint16_t vid = dd->idVendor;
  if (ifc.cls == 0x0A) chip = CDC;
  else if (vid == 0x1A86) chip = CH34X;
  else if (vid == 0x10C4) chip = CP210X;
  else if (vid == 0x0403) chip = FTDI;
  else if (comm >= 0) chip = CDC;
  else {
    logf("addr %u: unknown serial chip, skipped", addr);
    usb_host_device_close(client, dev);
    dev = nullptr;
    return EV_UNSUPPORTED;
  }
  dataIf = ifc.num;
  epIn = ifc.in;
  epOut = ifc.out;
  mpsIn = ifc.mpsIn ? ifc.mpsIn : 64;
  commIf = comm >= 0 ? comm : dataIf;

  err = usb_host_interface_claim(client, dev, dataIf, 0);
  if (err != ESP_OK) {
    logf("claim iface %u failed: %s", dataIf, esp_err_to_name(err));
    chip = NONE;
    closeDevice();
    return EV_UNSUPPORTED;
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
      logf("CH34x chip version 0x%02x", chVersion);
      control(0x40, 0xA1, 0, 0);  // serial init
      break;
    }
    case CP210X:
      control(0x41, 0x00, 1, dataIf);  // IFC_ENABLE
      break;
    case FTDI:
      ftdiChannel = cd->bNumInterfaces > 1 ? dataIf + 1 : 0;
      ftdiH = dd->bcdDevice == 0x0700 || dd->bcdDevice == 0x0800 || dd->bcdDevice == 0x0900;
      control(0x40, 0, 0, ftdiChannel);  // reset
      control(0x40, 2, 0, ftdiChannel);  // no flow control
      control(0x40, 9, 2, ftdiChannel);  // 2 ms latency timer
      break;
    default: break;
  }
  if (!applyLine()) logf("line setup (baud/format) failed");
  if (!applyModem()) logf("DTR/RTS setup failed");
  flushInput();
  ready = true;
  submitIn();
  return EV_CONNECTED;
}

}  // namespace

void begin() {
  if (installed) return;
  if (!logQ) {
    logQ = xQueueCreate(16, sizeof(LogMsg));
    rx = xStreamBufferCreate(8192, 1);
    newDevs = xQueueCreate(8, sizeof(uint8_t));
    ctrlDone = xSemaphoreCreateBinary();
    outDone = xSemaphoreCreateBinary();
  }
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
  cc.max_num_event_msg = 5;
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
  if (gone) {
    closeDevice();
    return EV_DISCONNECTED;
  }
  uint8_t addr;
  if (!dev && xQueueReceive(newDevs, &addr, 0) == pdTRUE) return (Event)openDevice(addr);
  return EV_NONE;
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
  usb_host_lib_info_t li;
  if (usb_host_lib_info(&li) == ESP_OK) logf("host ok: %d device(s) enumerated", li.num_devices);
  uint8_t addrs[8];
  int n = 0;
  usb_host_device_addr_list_fill(sizeof addrs, addrs, &n);
  for (int i = 0; i < n; i++) {
    bool mine = dev && addrs[i] == devAddr;
    usb_device_handle_t h = dev;
    if (!mine && usb_host_device_open(client, addrs[i], &h) != ESP_OK) continue;
    const usb_device_desc_t *dd;
    usb_host_get_device_descriptor(h, &dd);
    logf("addr %u: %04x:%04x class %02x%s", addrs[i], dd->idVendor, dd->idProduct, dd->bDeviceClass,
         mine ? " (in use)" : "");
    if (!mine) usb_host_device_close(client, h);
  }
  if (!n) logf("nothing on the bus: check OTG cable / 5V power");
  if (ready) logf("connected: %s", CHIP_NAMES[chip]);
}

}  // namespace usbserial
