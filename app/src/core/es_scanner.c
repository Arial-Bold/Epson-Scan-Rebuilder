/*
 * es_scanner.c — Epson Perfection V550 scanner engine for EPSON Scan (rebuild).
 *
 * Drives the scanner through Epson's own 64-bit "Interpreter EB" plugin
 * (installed by the Epson Scanner ICA driver), speaking ESC/I to it, with the
 * workarounds found while bringing the V550 up on Apple Silicon:
 *   - gamma must be a user-defined curve (3/4) and linear curves uploaded
 *   - only certain hardware resolutions are accepted (infrared: 800/1600/3200)
 *   - fast preview mode + film light makes the plugin loop forever
 *   - the parameter block's unused bytes must be cleared
 * Same code as the v550probe tool, packaged as a library.
 */
#include "es_scanner.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Every "error: ..." message the engine prints is also kept for the app to show. */
static char g_err[512];
static int es_fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int es_fprintf(FILE *f, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, f);
    if (f == stderr && !strncmp(buf, "error: ", 7)) {
        snprintf(g_err, sizeof g_err, "%s", buf + 7);
        size_t k = strlen(g_err);
        while (k && (g_err[k - 1] == '\n' || g_err[k - 1] == ' ')) g_err[--k] = 0;
    }
    return n;
}
#define fprintf es_fprintf

#define EPSON_VID 0x04B8
#define V550_PID  0x013B

/* ESC/I control bytes */
#define STX 0x02
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define ESC 0x1B
#define FS  0x1C

static int g_verbose = 0;
void es_set_verbose(int level) { g_verbose = level; }
static bool g_seize = false;
static volatile int g_cancel = 0;
static int g_curW = 0, g_curH = 0;      /* size of the scan in progress */

/* ------------------------------------------------------------------------ */
/* Logging                                                                   */
/* ------------------------------------------------------------------------ */

static struct timespec g_t0;
static double elapsed(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - g_t0.tv_sec) + (t.tv_nsec - g_t0.tv_nsec) / 1e9;
}

static void hexdump(const char *tag, const uint8_t *b, uint32_t n)
{
    if (!g_verbose) return;
    uint32_t show = n > 64 ? 64 : n;
    fprintf(stderr, "%7.2f %s [%u]", elapsed(), tag, n);
    for (uint32_t i = 0; i < show; i++) fprintf(stderr, " %02X", b[i]);
    if (show < n) fprintf(stderr, " ...");
    fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------------ */
/* USB (IOKit)                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    IOUSBDeviceInterface320    **dev;
    IOUSBInterfaceInterface300 **intf;
    UInt8 pipeIn, pipeOut;
} USBConn;

static USBConn g_usb;

static io_service_t find_v550(void)
{
    const char *classes[] = { "IOUSBHostDevice", "IOUSBDevice" };
    for (int i = 0; i < 2; i++) {
        CFMutableDictionaryRef m = IOServiceMatching(classes[i]);
        if (!m) continue;
        SInt32 vid = EPSON_VID, pid = V550_PID;
        CFNumberRef nv = CFNumberCreate(NULL, kCFNumberSInt32Type, &vid);
        CFNumberRef np = CFNumberCreate(NULL, kCFNumberSInt32Type, &pid);
        CFDictionarySetValue(m, CFSTR("idVendor"), nv);
        CFDictionarySetValue(m, CFSTR("idProduct"), np);
        CFRelease(nv);
        CFRelease(np);
        io_service_t s = IOServiceGetMatchingService(MACH_PORT_NULL, m); /* consumes m */
        if (s) return s;
    }
    return 0;
}

static bool usb_open(void)
{
    io_service_t svc = find_v550();
    if (!svc) {
        fprintf(stderr, "error: Perfection V550 (04B8:013B) not found. Is it plugged in and switched on?\n");
        return false;
    }

    IOCFPlugInInterface **plug = NULL;
    SInt32 score = 0;
    kern_return_t kr = IOCreatePlugInInterfaceForService(
        svc, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
    IOObjectRelease(svc);
    if (kr || !plug) {
        fprintf(stderr, "error: can't create USB device plug-in (0x%08x)\n", kr);
        return false;
    }
    HRESULT hr = (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
                                         (LPVOID *)&g_usb.dev);
    IODestroyPlugInInterface(plug);
    if (hr || !g_usb.dev) {
        fprintf(stderr, "error: can't get USB device interface\n");
        return false;
    }

    kr = (*g_usb.dev)->USBDeviceOpen(g_usb.dev);
    if (kr == kIOReturnExclusiveAccess) {
        if (!g_seize) {
            fprintf(stderr, "error: another program is using the scanner. Quit Image Capture, "
                            "Preview, Epson Scan 2, Epson Event Manager and VueScan, then retry.\n"
                            "(Run  ps aux | grep -i -E 'epson|ICDevice|Image Capture'  to see what's running.)\n");
            return false;
        }
        fprintf(stderr, "note: taking the scanner over from another program (--seize)...\n");
        kr = (*g_usb.dev)->USBDeviceOpenSeize(g_usb.dev);
    }
    if (kr) {
        fprintf(stderr, "error: can't open scanner (0x%08x). Quit Image Capture, Preview, "
                        "Epson Scan 2 and VueScan, then try again.\n", kr);
        return false;
    }

    UInt8 cur = 0;
    (*g_usb.dev)->GetConfiguration(g_usb.dev, &cur);
    if (cur == 0) {
        IOUSBConfigurationDescriptorPtr cd = NULL;
        kr = (*g_usb.dev)->GetConfigurationDescriptorPtr(g_usb.dev, 0, &cd);
        if (kr || !cd) {
            fprintf(stderr, "error: can't read USB configuration (0x%08x)\n", kr);
            return false;
        }
        kr = (*g_usb.dev)->SetConfiguration(g_usb.dev, cd->bConfigurationValue);
        if (kr) {
            fprintf(stderr, "error: can't set USB configuration (0x%08x)\n", kr);
            return false;
        }
    }

    IOUSBFindInterfaceRequest req = {
        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare
    };
    io_iterator_t it = 0;
    kr = (*g_usb.dev)->CreateInterfaceIterator(g_usb.dev, &req, &it);
    if (kr) {
        fprintf(stderr, "error: can't list USB interfaces (0x%08x)\n", kr);
        return false;
    }
    io_service_t isvc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!isvc) {
        fprintf(stderr, "error: scanner has no USB interface\n");
        return false;
    }

    plug = NULL;
    kr = IOCreatePlugInInterfaceForService(isvc, kIOUSBInterfaceUserClientTypeID,
                                           kIOCFPlugInInterfaceID, &plug, &score);
    IOObjectRelease(isvc);
    if (kr || !plug) {
        fprintf(stderr, "error: can't create USB interface plug-in (0x%08x)\n", kr);
        return false;
    }
    hr = (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
                                 (LPVOID *)&g_usb.intf);
    IODestroyPlugInInterface(plug);
    if (hr || !g_usb.intf) {
        fprintf(stderr, "error: can't get USB interface\n");
        return false;
    }

    kr = (*g_usb.intf)->USBInterfaceOpen(g_usb.intf);
    if (kr == kIOReturnExclusiveAccess && g_seize)
        kr = (*g_usb.intf)->USBInterfaceOpenSeize(g_usb.intf);
    if (kr == kIOReturnExclusiveAccess) {
        fprintf(stderr, "error: another program is using the scanner's USB interface. Quit Image Capture, "
                        "Preview, Epson Scan 2, Epson Event Manager and VueScan, then retry.\n");
        return false;
    }
    if (kr) {
        fprintf(stderr, "error: can't open USB interface (0x%08x)\n", kr);
        return false;
    }

    UInt8 n = 0;
    (*g_usb.intf)->GetNumEndpoints(g_usb.intf, &n);
    for (UInt8 p = 1; p <= n; p++) {
        UInt8 dir, num, tt, interval;
        UInt16 mps;
        if ((*g_usb.intf)->GetPipeProperties(g_usb.intf, p, &dir, &num, &tt, &mps, &interval))
            continue;
        if (g_verbose)
            fprintf(stderr, "  pipe %u: dir=%u ep=%u type=%u maxpacket=%u\n", p, dir, num, tt, mps);
        if (tt == kUSBBulk) {
            if (dir == kUSBIn && !g_usb.pipeIn) g_usb.pipeIn = p;
            if (dir == kUSBOut && !g_usb.pipeOut) g_usb.pipeOut = p;
        }
    }
    if (!g_usb.pipeIn || !g_usb.pipeOut) {
        fprintf(stderr, "error: scanner bulk pipes not found\n");
        return false;
    }
    return true;
}

static void usb_close(void)
{
    if (g_usb.intf) {
        (*g_usb.intf)->USBInterfaceClose(g_usb.intf);
        (*g_usb.intf)->Release(g_usb.intf);
        g_usb.intf = NULL;
    }
    if (g_usb.dev) {
        (*g_usb.dev)->USBDeviceClose(g_usb.dev);
        (*g_usb.dev)->Release(g_usb.dev);
        g_usb.dev = NULL;
    }
}

/* Timeouts are generous: the lamp warm-up and film calibration can be slow. */
#define USB_NODATA_MS     20000
#define USB_COMPLETION_MS 600000

static IOReturn usb_write(const uint8_t *buf, uint32_t len)
{
    IOReturn kr = (*g_usb.intf)->WritePipeTO(g_usb.intf, g_usb.pipeOut, (void *)buf, len,
                                             USB_NODATA_MS, USB_COMPLETION_MS);
    if (kr == kIOUSBPipeStalled) {
        (*g_usb.intf)->ClearPipeStallBothEnds(g_usb.intf, g_usb.pipeOut);
        kr = (*g_usb.intf)->WritePipeTO(g_usb.intf, g_usb.pipeOut, (void *)buf, len,
                                        USB_NODATA_MS, USB_COMPLETION_MS);
    }
    return kr;
}

/* One bulk read, like Epson's own readPipeToBuffer: it ends at `len` bytes
 * or at a short packet. */
static IOReturn usb_read(uint8_t *buf, uint32_t len)
{
    UInt32 n = len;
    IOReturn kr = (*g_usb.intf)->ReadPipeTO(g_usb.intf, g_usb.pipeIn, buf, &n,
                                            USB_NODATA_MS, USB_COMPLETION_MS);
    if (kr == kIOUSBPipeStalled) {
        (*g_usb.intf)->ClearPipeStallBothEnds(g_usb.intf, g_usb.pipeIn);
        n = len;
        kr = (*g_usb.intf)->ReadPipeTO(g_usb.intf, g_usb.pipeIn, buf, &n,
                                       USB_NODATA_MS, USB_COMPLETION_MS);
    }
    if (!kr && n != len && g_verbose > 1)
        fprintf(stderr, "  usb: short read %u of %u\n", (unsigned)n, len);
    return kr;
}

/* ------------------------------------------------------------------------ */
/* Epson interpreter plugin                                                  */
/*                                                                           */
/* Signatures recovered from the arm64 plugin and Epson's ICA driver:        */
/*   bool INTInit(IOCallback read, IOCallback write, void *ctx)              */
/*   bool INTRead(uint8_t *buf, uint32_t len)                                */
/*   bool INTWrite(uint8_t *buf, uint32_t len)                               */
/*   void INTClose(void)                                                     */
/*   int  INTGetInterpreterError(void)   short INTGetUSBError(void)          */
/*   IOCallback: bool cb(uint8_t *buf, uint32_t len, void *ctx, uint16_t *e) */
/* ------------------------------------------------------------------------ */

typedef unsigned char (*IOCallback)(unsigned char *, unsigned int, void *, unsigned short *);
typedef unsigned char (*INTInit_f)(IOCallback, IOCallback, void *);
typedef unsigned char (*INTRW_f)(unsigned char *, unsigned int);
typedef void (*INTClose_f)(void);
typedef int (*INTErr_f)(void);
typedef short (*INTUSBErr_f)(void);

static struct {
    CFBundleRef bundle;
    INTInit_f   init;
    INTRW_f     read;
    INTRW_f     write;
    INTClose_f  close;
    INTErr_f    error;
    INTUSBErr_f usbError;
    bool        open;
} g_int;

/* Watchdog: Epson's plugin polls the scanner's status register (ESC 03)
 * until the "busy" bits (0x42) clear, with no time limit. If the scanner
 * stays busy, give up after 60 s instead of hanging forever. */
static bool   g_lastWasStatusPoll = false;
static double g_lastTraffic = 0;   /* time of last non-status-poll USB traffic */
static double g_lastNote = 0;
static unsigned g_polls = 0;
static time_t g_busySince = 0;
static uint8_t g_lastStatus[2];

static unsigned char cb_read(unsigned char *buf, unsigned int len, void *ctx, unsigned short *err)
{
    (void)ctx;
    bool quiet = g_lastWasStatusPoll && g_verbose < 2;   /* status polls only at -vv */
    if (g_verbose && !quiet) fprintf(stderr, "  usb read  %u bytes...\n", len);
    IOReturn kr = usb_read(buf, len);
    if (!kr && !quiet) hexdump("usb<", buf, len);
    if (!kr && !g_lastWasStatusPoll) g_lastTraffic = elapsed();
    if (!kr && g_lastWasStatusPoll) {
        g_polls++;
        if (g_verbose && (buf[0] != g_lastStatus[0] || buf[1] != g_lastStatus[1]))
            fprintf(stderr, "%7.2f scanner status now %02X %02X%s\n", elapsed(), buf[0], buf[1],
                    (buf[0] & 0x42) ? " (busy)" : "");
    }
    if (!kr && g_lastWasStatusPoll && len == 2) {
        g_lastStatus[0] = buf[0];
        g_lastStatus[1] = buf[1];
        if (buf[0] & 0x42) {
            if (!g_busySince) g_busySince = time(NULL);
            else if (time(NULL) - g_busySince > 60) {
                fprintf(stderr,
                        "error: the scanner has reported \"busy\" for over a minute (status %02X %02X).\n"
                        "Check the scanner's status light, the transportation lock, and that the\n"
                        "film holder isn't covering the calibration window at the top of the glass.\n",
                        buf[0], buf[1]);
                return 0;
            }
        } else {
            g_busySince = 0;
        }
    }
    if (kr) {
        fprintf(stderr, "usb read failed (0x%08x, %u bytes)\n", kr, len);
        return 0;
    }
    if (err) *err = 0;
    return 1;
}

static unsigned char cb_write(unsigned char *buf, unsigned int len, void *ctx, unsigned short *err)
{
    (void)ctx;
    g_lastWasStatusPoll = (len == 2 && buf[0] == 0x1B && buf[1] == 0x03);
    if (!g_lastWasStatusPoll) { g_busySince = 0; g_lastTraffic = elapsed(); g_polls = 0; }
    if (!g_lastWasStatusPoll || g_verbose > 1) hexdump("usb>", buf, len);
    IOReturn kr = usb_write(buf, len);
    if (kr) {
        fprintf(stderr, "usb write failed (0x%08x, %u bytes)\n", kr, len);
        return 0;
    }
    if (err) *err = 0;
    return 1;
}

static void report_int_error(const char *what)
{
    int ie = g_int.error ? g_int.error() : 0;
    int ue = g_int.usbError ? g_int.usbError() : 0;
    fprintf(stderr, "error: %s (interpreter error %d, usb error %d)\n", what, ie, ue);
}

static bool int_load(const char *path)
{
    if (access(path, R_OK) != 0) {
        fprintf(stderr,
                "error: Epson plugin not found at\n  %s\n"
                "Install the Epson Scanner ICA Driver (ESICA_v5_8_23_41_e.dmg) first.\n", path);
        return false;
    }
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                           (CFIndex)strlen(path), true);
    g_int.bundle = CFBundleCreate(NULL, url);
    CFRelease(url);
    if (!g_int.bundle) {
        fprintf(stderr, "error: can't open plugin bundle\n");
        return false;
    }
    CFErrorRef cferr = NULL;
    if (!CFBundleLoadExecutableAndReturnError(g_int.bundle, &cferr)) {
        fprintf(stderr, "error: can't load Epson plugin");
        if (cferr) {
            CFStringRef d = CFErrorCopyDescription(cferr);
            char msg[512] = "";
            CFStringGetCString(d, msg, sizeof msg, kCFStringEncodingUTF8);
            fprintf(stderr, ": %s", msg);
            CFRelease(d);
            CFRelease(cferr);
        }
        fprintf(stderr, "\n");
        return false;
    }
#define SYM(field, type, name) \
    g_int.field = (type)CFBundleGetFunctionPointerForName(g_int.bundle, CFSTR(name))
    SYM(init, INTInit_f, "INTInit");
    SYM(read, INTRW_f, "INTRead");
    SYM(write, INTRW_f, "INTWrite");
    SYM(close, INTClose_f, "INTClose");
    SYM(error, INTErr_f, "INTGetInterpreterError");
    SYM(usbError, INTUSBErr_f, "INTGetUSBError");
#undef SYM
    if (!g_int.init || !g_int.read || !g_int.write || !g_int.close) {
        fprintf(stderr, "error: plugin is missing INTInit/INTRead/INTWrite/INTClose\n");
        return false;
    }
    return true;
}

static bool int_start(void)
{
    fprintf(stderr, "Initializing scanner (loading firmware)...%s\n",
            g_verbose ? "" : "  (if this stalls, re-run with -v)");
    if (!g_int.init(cb_read, cb_write, &g_usb)) {
        report_int_error("INTInit failed");
        return false;
    }
    g_int.open = true;
    return true;
}

static void int_stop(void)
{
    if (g_int.open) g_int.close();
    g_int.open = false;
}

/* ------------------------------------------------------------------------ */
/* ESC/I                                                                     */
/* ------------------------------------------------------------------------ */

static bool esc_write(const uint8_t *b, uint32_t n)
{
    hexdump("->", b, n);
    if (!g_int.write((unsigned char *)b, n)) {
        report_int_error("INTWrite failed");
        return false;
    }
    return true;
}

static bool esc_read(uint8_t *b, uint32_t n, bool dump)
{
    if (!g_int.read(b, n)) {
        report_int_error("The scanner stopped answering (INTRead failed). If its light is flashing orange, "
                         "or it was only just switched on, wait until the light stays green and try again");
        return false;
    }
    if (dump) hexdump("<-", b, n);
    return true;
}

/* Send bytes that the scanner answers with ACK/NAK. 0 = ACK, 1 = NAK, -1 = I/O error */
static int cmd_simple(const uint8_t *b, uint32_t n)
{
    uint8_t r = 0;
    if (!esc_write(b, n) || !esc_read(&r, 1, true)) return -1;
    if (r == ACK) return 0;
    if (r == NAK) return 1;
    fprintf(stderr, "warning: expected ACK/NAK, got 0x%02X\n", r);
    return 0;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

typedef struct {
    char     level[3];
    uint32_t basic, minRes, maxRes, maxPixels;
    uint32_t fbW, fbH, adfW, adfH, tpuW, tpuH;
    uint8_t  cap1, cap2, inDepth, outDepth;
    char     model[17], rom[5];
} Identity;

static bool get_identity(Identity *id)
{
    const uint8_t c[2] = { FS, 'I' };
    uint8_t b[80];
    if (!esc_write(c, 2) || !esc_read(b, 80, true)) return false;
    memset(id, 0, sizeof *id);
    id->level[0] = b[0];
    id->level[1] = b[1];
    id->basic = le32(b + 4);
    id->minRes = le32(b + 8);
    id->maxRes = le32(b + 12);
    id->maxPixels = le32(b + 16);
    id->fbW = le32(b + 20);
    id->fbH = le32(b + 24);
    id->adfW = le32(b + 28);
    id->adfH = le32(b + 32);
    id->tpuW = le32(b + 36);
    id->tpuH = le32(b + 40);
    id->cap1 = b[44];
    id->cap2 = b[45];
    memcpy(id->model, b + 46, 16);
    for (int i = 15; i >= 0 && (id->model[i] == ' ' || id->model[i] == 0); i--) id->model[i] = 0;
    memcpy(id->rom, b + 62, 4);
    id->inDepth = b[66];
    id->outDepth = b[67];
    return true;
}

static bool get_status(uint8_t s[16])
{
    const uint8_t c[2] = { FS, 'F' };
    return esc_write(c, 2) && esc_read(s, 16, true);
}

static bool get_params(uint8_t p[64])
{
    const uint8_t c[2] = { FS, 'S' };
    return esc_write(c, 2) && esc_read(p, 64, true);
}

static void print_params(const char *title, const uint8_t *p)
{
    printf("%s\n", title);
    printf("  resolution      %u x %u\n", le32(p), le32(p + 4));
    printf("  area            x=%u y=%u w=%u h=%u (pixels)\n",
           le32(p + 8), le32(p + 12), le32(p + 16), le32(p + 20));
    printf("  color 0x%02X  depth %u  option %u  mode %u  block-lines %u\n",
           p[24], p[25], p[26], p[27], p[28]);
    printf("  gamma 0x%02X  brightness %u  color-corr 0x%02X  halftone 0x%02X  threshold %u\n",
           p[29], p[30], p[31], p[32], p[33]);
    printf("  aas %u  sharpness %u  mirror %u  film %u  lamp %u\n",
           p[34], p[35], p[36], p[37], p[38]);
}

static bool wait_warm_up(void)
{
    uint8_t s[16];
    for (int i = 0; i < 90; i++) {
        if (!get_status(s)) return false;
        if (s[0] & 0x80) {
            fprintf(stderr, "error: The scanner has stopped with an error (status 0x%02X) and its light is "
                            "probably flashing. Turn the scanner off, check that the transportation lock "
                            "underneath is unlocked and nothing is blocking the carriage, close the lid, then "
                            "turn it back on and wait until the light stays green before trying again.\n", s[0]);
            return false;
        }
        if (!(s[0] & 0x02)) return true; /* not warming up */
        if (i == 0) fprintf(stderr, "Lamp warming up...\n");
        sleep(2);
    }
    fprintf(stderr, "error: lamp warm-up timed out\n");
    return false;
}

/* Infrared enable: ESC # followed by the current FS S block XOR'd with
 * Epson's 32-byte key (same sequence the SANE epson2 backend uses). */
static bool enable_infrared(void)
{
    static const uint8_t seq[32] = {
        0xCA, 0xFB, 0x77, 0x71, 0x20, 0x16, 0xDA, 0x09,
        0x5F, 0x57, 0x09, 0x12, 0x04, 0x83, 0x76, 0x77,
        0x3C, 0x73, 0x9C, 0xBE, 0x7A, 0xE0, 0x52, 0xE2,
        0x90, 0x0D, 0xFF, 0x9A, 0xEF, 0x4C, 0x2C, 0x81
    };
    uint8_t p[64];
    if (!get_params(p)) return false;
    for (int i = 0; i < 32; i++) p[i] ^= seq[i];
    const uint8_t c[2] = { ESC, '#' };
    int r = cmd_simple(c, 2);
    if (r == 0) r = cmd_simple(p, 32);
    if (r != 0) {
        fprintf(stderr, "error: scanner refused infrared enable (%s)\n", r > 0 ? "NAK" : "I/O error");
        return false;
    }
    return true;
}

/* Gamma 3/4 means "use the curves the host uploaded". Upload straight-line
 * (linear) curves for R, G and B with ESC z, so the data is raw and
 * unaltered — what Digital ICE and Epson's later processing expect. */
static bool set_linear_gamma(void)
{
    static const char chans[3] = { 'R', 'G', 'B' };
    uint8_t table[257];
    for (int n = 0; n < 256; n++) table[n + 1] = (uint8_t)n;
    const uint8_t c[2] = { ESC, 'z' };
    for (int i = 0; i < 3; i++) {
        table[0] = (uint8_t)chans[i];
        int r = cmd_simple(c, 2);
        if (r == 0) r = cmd_simple(table, sizeof table);
        if (r != 0) {
            fprintf(stderr, "error: scanner refused the %c gamma curve (%s)\n", chans[i],
                    r > 0 ? "NAK" : "I/O error");
            return false;
        }
    }
    return true;
}

enum { SRC_FLATBED = 0, SRC_FILM = 1 };
enum { MODE_GRAY = 0, MODE_COLOR = 1, MODE_IR = 2 };

typedef struct {
    int    source;
    int    mode;
    int    depth;     /* 8 or 16 */
    int    res;
    int    res_y;     /* sub-scan resolution when exact (0 = same as res) */
    bool   exact;     /* res / res_y were chosen from Epson's tables: send them as they are */
    int    film;      /* 0 positive, 1 negative */
    bool   preview;
    bool   areaSet;
    double x, y, w, h; /* mm */
    int outW, outH, outRes, outResY;   /* filled in by do_scan */
} ScanOpts;

/* The V550 plugin only accepts certain hardware resolutions. Main-scan must
 * come from a fixed list, and sub-scan must be a multiple of 40 (400 above
 * 1600 dpi). These are the square resolutions that satisfy both. Epson's
 * driver scans at one of these and resizes in software; we'll do the same
 * later with Epson's own resize filter. */
static const int kHardwareRes[] = { 200, 400, 600, 800, 1200, 1600, 3200, 6400 };

/* Infrared (option 3) only accepts these main-scan resolutions. */
static const int kInfraredRes[] = { 800, 1600, 3200 };

static int pick_res(const int *list, size_t n, int wanted)
{
    for (size_t i = 0; i < n; i++)
        if (list[i] >= wanted) return list[i];
    return list[n - 1];
}

static int hardware_res(int wanted)
{
    return pick_res(kHardwareRes, sizeof kHardwareRes / sizeof kHardwareRes[0], wanted);
}

static int infrared_res(int wanted)
{
    return pick_res(kInfraredRes, sizeof kInfraredRes / sizeof kInfraredRes[0], wanted);
}

static bool do_scan(const Identity *id, ScanOpts *in, es_data_fn data, es_progress_fn progress, void *ctx)
{
    ScanOpts opts = *in;
    const ScanOpts *o = &opts;
    if (!in->exact) opts.res = in->mode == MODE_IR ? infrared_res(in->res) : hardware_res(in->res);
    /* Epson's GetResolution can pick a different sub-scan (Y) resolution, e.g. 400 x 320 */
    int resY = in->exact && in->res_y > 0 ? in->res_y : opts.res;
    if (opts.source == SRC_FILM && opts.preview) {
        /* Epson's V550 plugin loops forever in Set_CCD_Mode when the
         * high-speed (preview) mode is combined with the film light. */
        printf("Note: fast preview mode doesn't work with film; using normal speed.\n");
        opts.preview = false;
    }
    if (opts.res != in->res)
        printf("Note: %d dpi isn't a V550 hardware resolution; scanning at %d dpi.\n",
               in->res, opts.res);

    if (o->mode == MODE_IR && o->source != SRC_FILM) {
        fprintf(stderr, "error: infrared scanning only works with the film holder (--source film)\n");
        return false;
    }
    if (o->source == SRC_FILM && id->tpuW == 0) {
        fprintf(stderr, "error: scanner reports no film light (TPU) — is the lid connector plugged in?\n");
        return false;
    }

    uint32_t areaW = o->source == SRC_FILM ? id->tpuW : id->fbW;
    uint32_t areaH = o->source == SRC_FILM ? id->tpuH : id->fbH;
    double scale = (double)o->res / (double)(id->basic ? id->basic : 1);
    double scaleY = (double)resY / (double)(id->basic ? id->basic : 1);

    uint32_t x = 0, y = 0;
    uint32_t w = (uint32_t)(areaW * scale), h = (uint32_t)(areaH * scaleY);
    if (o->areaSet) {
        x = (uint32_t)(o->x / 25.4 * o->res + 0.5);
        y = (uint32_t)(o->y / 25.4 * resY + 0.5);
        w = (uint32_t)(o->w / 25.4 * o->res + 0.5);
        h = (uint32_t)(o->h / 25.4 * resY + 0.5);
    }
    if (!w || !h) {
        fprintf(stderr, "error: empty scan area\n");
        return false;
    }

    uint32_t spp = o->mode == MODE_COLOR ? 3 : 1;
    uint32_t bpl = w * spp * (uint32_t)(o->depth / 8);
    uint32_t lines = (4u << 20) / bpl;
    if (lines < 1) lines = 1;
    if (lines > 255) lines = 255;

    if (!wait_warm_up()) return false;

    if (o->mode == MODE_IR && !enable_infrared()) return false;

    uint8_t p[64];
    if (!get_params(p)) return false;     /* start from the scanner's own defaults */
    /* The scanner only fills 42 of the 64 bytes; the plugin leaves memory
     * garbage in the rest. Clear everything past the defined fields. */
    memset(p + 39, 0, sizeof p - 39);
    put32(p + 0, (uint32_t)o->res);
    put32(p + 4, (uint32_t)resY);
    put32(p + 8, x);
    put32(p + 12, y);
    put32(p + 16, w);
    put32(p + 20, h);
    p[24] = o->mode == MODE_COLOR ? 0x13 : 0x00;           /* RGB pixel order / mono */
    p[25] = (uint8_t)o->depth;
    p[26] = o->source == SRC_FILM ? (o->mode == MODE_IR ? 3 : 1) : 0;
    p[27] = o->preview ? 1 : 0;
    p[28] = (uint8_t)lines;
    p[29] = 0x03;   /* gamma: the plugin only accepts 3 or 4 (user-defined curves) */
    if (o->source == SRC_FILM) p[37] = (uint8_t)o->film;
    if (g_verbose) print_params("Sending scan parameters:", p);

    const uint8_t fsw[2] = { FS, 'W' };
    int r = cmd_simple(fsw, 2);
    if (r == 0) r = cmd_simple(p, 64);
    if (r != 0) {
        fprintf(stderr, "error: scanner rejected the scan parameters (%s). "
                        "Re-run with -v and send me the output.\n", r > 0 ? "NAK" : "I/O error");
        return false;
    }

    if (!set_linear_gamma()) return false;

    /* FS G: start scan. Returns STX, status, block size, block count, last block size. */
    uint8_t g[14];
    const uint8_t fsg[2] = { FS, 'G' };
    for (int tries = 0;; tries++) {
        if (!esc_write(fsg, 2) || !esc_read(g, 14, true)) return false;
        if (g[0] != STX) {
            fprintf(stderr, "error: bad FS G reply (0x%02X)\n", g[0]);
            return false;
        }
        if (g[1] & 0x80) {
            fprintf(stderr, "error: scanner fatal error at start (status 0x%02X)\n", g[1]);
            return false;
        }
        if (!(g[1] & 0x40)) break;
        if (tries >= 30) {
            fprintf(stderr, "error: scanner stayed busy\n");
            return false;
        }
        sleep(2);
    }
    uint32_t blockLen = le32(g + 2), blocks = le32(g + 6), lastLen = le32(g + 10);
    if (lastLen) blocks++;
    if (blockLen == 0 && lastLen) blockLen = lastLen;

    printf("Scanning %s %s, %d dpi, %d-bit, %u x %u px\n",
           o->source == SRC_FILM ? "film" : "flatbed",
           o->mode == MODE_COLOR ? "color" : o->mode == MODE_IR ? "infrared" : "gray",
           o->res, o->depth, w, h);
    if (g_verbose)
        fprintf(stderr, "  %u blocks of %u bytes (last %u)\n", blocks, blockLen, lastLen);

    uint8_t *buf = malloc((size_t)blockLen + 1 > (size_t)lastLen + 1 ? (size_t)blockLen + 1
                                                                     : (size_t)lastLen + 1);
    if (!buf) {
        fprintf(stderr, "error: out of memory\n");
        return false;
    }

    uint64_t total = 0;
    g_curW = (int)w;
    g_curH = (int)h;
    in->outW = (int)w;
    in->outH = (int)h;
    in->outRes = o->res;
    in->outResY = resY;

    bool ok = true;
    for (uint32_t i = 1; i <= blocks; i++) {
        uint32_t len = (i == blocks && lastLen) ? lastLen : blockLen;
        if (!esc_read(buf, len + 1, false)) { ok = false; break; }
        uint8_t st = buf[len];
        if (st & 0x10) {
            fprintf(stderr, "Scan cancelled by the scanner.\n");
            ok = false;
            break;
        }
        if (st & 0xC0) {
            fprintf(stderr, "error: scanner error during scan (status 0x%02X)\n", st);
            ok = false;
            break;
        }
        if (data && data(buf, len, ctx)) { g_cancel = 1; }
        if (g_cancel) {
            fprintf(stderr, "Scan cancelled.\n");
            ok = false;
            break;
        }
        total += len;
        if (i < blocks) {
            const uint8_t a = ACK;
            if (!esc_write(&a, 1)) { ok = false; break; }
        }
        if (progress) progress((double)i / blocks, ctx);
    }
    fprintf(stderr, "\n");

    if (!ok) {
        const uint8_t c = CAN;
        esc_write(&c, 1);
        uint8_t dummy;
        esc_read(&dummy, 1, true);
    }

    free(buf);
    in->outH = (int)(total / bpl);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

static Identity g_id;
static bool g_open = false;

const char *es_last_error(void) { return g_err; }

/* The scanner only reports the film area (TPU) in its identity while it can see
 * the film light. Right after a re-initialization, or while the carriage is
 * still parking, it can briefly report 0 even though the lid is connected. Keep
 * the last good film area and re-ask before giving up. */
static uint32_t g_tpuW, g_tpuH;
static void remember_tpu(Identity *id)
{
    if (id->tpuW && id->tpuH) { g_tpuW = id->tpuW; g_tpuH = id->tpuH; }
    else if (g_tpuW) { id->tpuW = g_tpuW; id->tpuH = g_tpuH; }
}

/* The film area is a fixed property of the scanner, so keep the last one it reported
 * on disk too: after an app restart the scanner can still be warming up its film
 * light and briefly report none. If the lid really isn't connected, the scanner
 * refuses the film scan itself and the user gets that error instead. */
static const char *tpu_cache_path(void)
{
    static char p[1024];
    const char *home = getenv("HOME");
    snprintf(p, sizeof p, "%s/Library/Caches/local.epsonscan.rebuild.filmarea", home ? home : "/tmp");
    return p;
}

static void save_tpu(uint32_t w, uint32_t h)
{
    FILE *f = fopen(tpu_cache_path(), "w");
    if (f) { fprintf(f, "%u %u\n", w, h); fclose(f); }
}

static bool load_tpu(uint32_t *w, uint32_t *h)
{
    FILE *f = fopen(tpu_cache_path(), "r");
    bool ok = f && fscanf(f, "%u %u", w, h) == 2 && *w && *h;
    if (f) fclose(f);
    return ok;
}

static bool ensure_film_area(void)
{
    /* ask again for up to ~10 s while the film light warms up */
    for (int i = 0; i < 7 && g_id.tpuW == 0; i++) {
        if (i) usleep(1500 * 1000);
        Identity id;
        if (get_identity(&id)) { g_id = id; remember_tpu(&g_id); }
    }
    if (g_id.tpuW) { save_tpu(g_id.tpuW, g_id.tpuH); return true; }
    uint32_t w, h;
    if (load_tpu(&w, &h)) {
        fprintf(stderr, "Scanner didn't report its film area; using the one it reported before (%u x %u).\n", w, h);
        g_id.tpuW = w; g_id.tpuH = h; g_tpuW = w; g_tpuH = h;
        return true;
    }
    return false;
}

int es_open(const char *plugin_path)
{
    g_err[0] = 0;
    if (g_open) return 0;
    if (!int_load(plugin_path ? plugin_path : ES_DEFAULT_PLUGIN)) {
        if (!g_err[0]) snprintf(g_err, sizeof g_err, "couldn't load Epson's V550 plugin");
        return -1;
    }
    if (!usb_open()) { usb_close(); return -1; }
    if (!int_start()) { usb_close(); return -1; }
    if (!get_identity(&g_id)) { int_stop(); usb_close(); return -1; }
    remember_tpu(&g_id);
    if (g_id.tpuW) save_tpu(g_id.tpuW, g_id.tpuH);
    g_open = true;
    return 0;
}

void es_close(void)
{
    if (!g_open) return;
    int_stop();
    usb_close();
    g_open = false;
}

int es_info(ESInfo *out)
{
    if (!g_open) return -1;
    memset(out, 0, sizeof *out);
    snprintf(out->model, sizeof out->model, "%s", g_id.model);
    out->basic_res = (int)g_id.basic;
    out->max_res = (int)g_id.maxRes;
    out->flatbed_w_mm = g_id.fbW * 25.4 / g_id.basic;
    out->flatbed_h_mm = g_id.fbH * 25.4 / g_id.basic;
    out->film_w_mm = g_id.tpuW * 25.4 / g_id.basic;
    out->film_h_mm = g_id.tpuH * 25.4 / g_id.basic;
    out->infrared = (g_id.cap1 & 0x02) != 0;
    return 0;
}

void es_cancel(void) { g_cancel = 1; }

void es_current_size(int *w, int *h) { *w = g_curW; *h = g_curH; }

int es_scan(const ESScanParams *p, es_data_fn data, es_progress_fn progress, void *ctx, ESScanResult *res)
{
    g_err[0] = 0;
    g_cancel = 0;
    g_curW = g_curH = 0;
    if (!g_open) { snprintf(g_err, sizeof g_err, "scanner isn't open"); return -1; }
    ScanOpts o;
    memset(&o, 0, sizeof o);
    o.source = p->film ? SRC_FILM : SRC_FLATBED;
    o.mode = p->infrared ? MODE_IR : (p->color ? MODE_COLOR : MODE_GRAY);
    o.depth = p->depth16 ? 16 : 8;
    o.res = p->dpi;
    o.res_y = p->dpi_y;
    o.exact = p->exact != 0;
    o.film = p->negative ? 1 : 0;
    o.preview = p->fast_preview;
    if (p->area_w_mm > 0 && p->area_h_mm > 0) {
        o.areaSet = true;
        o.x = p->area_x_mm; o.y = p->area_y_mm; o.w = p->area_w_mm; o.h = p->area_h_mm;
    }
    if (o.source == SRC_FILM && !ensure_film_area()) {
        snprintf(g_err, sizeof g_err, "The scanner can't see its film light.\n\n"
                 "Check that the cable from the scanner lid is plugged into the back of the scanner, "
                 "then turn the scanner off and on and try again.");
        return -1;
    }
    bool ok = do_scan(&g_id, &o, data, progress, ctx);
    if (!ok && !g_cancel && o.exact && strstr(g_err, "rejected the scan parameters")) {
        /* a resolution from Epson's table the plugin won't take: fall back to the nearest
         * hardware resolution (the caller resizes from whatever was scanned) */
        fprintf(stderr, "Scanner rejected %d x %d dpi; retrying at a hardware resolution.\n", o.res, o.res_y);
        o.exact = false;
        g_err[0] = 0;
        ok = do_scan(&g_id, &o, data, progress, ctx);
    }
    if (!ok && !g_cancel && strstr(g_err, "fatal error at start")) {
        /* The scanner sometimes refuses the first scan after sitting idle or after the
         * lid/holder was moved. Re-initialize it (reloads the firmware, re-homes the
         * carriage) and try once more before giving up. */
        fprintf(stderr, "Scanner refused to start; re-initializing and retrying once.\n");
        es_close();
        if (es_open(NULL) == 0) {
            g_err[0] = 0;
            ok = do_scan(&g_id, &o, data, progress, ctx);
        }
    }
    if (!ok && strstr(g_err, "fatal error at start"))
        snprintf(g_err, sizeof g_err,
                 "The scanner reported an error and couldn't start.\n\n"
                 "%s Then turn the scanner off and on, and try again.",
                 o.source == SRC_FILM
                     ? "Check that the film holder is in its marked position on the glass, film is loaded, "
                       "and the white document mat is removed from the lid."
                     : "Check that the document mat is in the lid and nothing is blocking the carriage.");
    if (res) {
        res->width = o.outW;
        res->height = o.outH;
        res->dpi = o.outRes;
        res->dpi_y = o.outResY;
        res->channels = o.mode == MODE_COLOR ? 3 : 1;
        res->depth = o.depth;
    }
    if (!ok && !g_err[0]) snprintf(g_err, sizeof g_err, "%s", g_cancel ? "Scan cancelled." : "The scan failed.");
    return ok ? 0 : -1;
}
