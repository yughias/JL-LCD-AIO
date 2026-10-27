/**
 * @file jl_lcd.h
 * @brief Lightweight C/C++ driver for JL USB LCD panels.
 *
 * Provides synchronous and asynchronous frame submission over a virtual COM
 * port, handling image encoding and USB transfer automatically.
 */

#ifndef __JL_PANEL_H__
#define __JL_PANEL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing an active connection to a JL LCD panel.
 */
typedef struct jl_panel jl_panel_t;

/**
 * @brief Initializes the underlying Windows COM runtime required for device discovery.
 *
 * @note Must be called once on the calling thread before invoking ::jl_open().
 *
 * @return `true` if initialization succeeded, or `false` on failure.
 */
bool jl_init(void);

/**
 * @brief Opens a connection to a JL LCD panel and starts its background worker thread.
 *
 * @param[in] port Serial port path (e.g., `"\\\\.\\COM7"`), or `NULL` to
 *                 automatically scan and connect to the first available panel.
 *
 * @return A pointer to an initialized ::jl_panel_t context on success,
 *         or `NULL` if no device could be found or opened.
 */
jl_panel_t* jl_open(const char* port);

/**
 * @brief Sets the WIC JPEG compression quality used for subsequent frames.
 *
 * @param[in] panel   Valid panel handle returned by ::jl_open().
 * @param[in] quality Compression quality in the range `[0.01f, 1.0f]` (default is `0.75f`).
 */
void jl_set_jpeg_quality(jl_panel_t* panel, float quality);

/**
 * @brief Sets the maximum encoded JPEG payload size allowed per frame transfer.
 *
 * @param[in] panel     Valid panel handle returned by ::jl_open().
 * @param[in] max_bytes Maximum transfer size in bytes (clamped to `JL_MAX_JPEG_BUFFER`, default `80 * 1024`).
 */
void jl_set_max_transfer_size(jl_panel_t* panel, size_t max_bytes);

/**
 * @brief Synchronously encodes and transmits a pixel buffer to the LCD panel.
 *
 * Blocks the calling thread until frame encoding and the underlying USB serial
 * transfer have fully completed.
 *
 * @param[in] panel  Valid panel handle returned by ::jl_open().
 * @param[in] argb   Pointer to the source pixel buffer in 32-bit ARGB format
 * @param[in] width  Width of the source surface in pixels.
 * @param[in] height Height of the source surface in pixels.
 *
 * @return `true` if the frame was encoded and transmitted successfully,
 *         or `false` if an error occurred.
 */
bool jl_push_surface_sync(jl_panel_t* panel, const void* argb, int width, int height);

/**
 * @brief Asynchronously submits a pixel buffer for background encoding and transmission.
 *
 * Copies the provided pixel buffer into an internal queue and returns immediately
 * without waiting for encoding or USB I/O. A dedicated background thread processes
 * and transmits the frame to the panel.
 *
 * @param[in] panel  Valid panel handle returned by ::jl_open().
 * @param[in] argb   Pointer to the source pixel buffer in 32-bit ARGB format
 * @param[in] width  Width of the source surface in pixels.
 * @param[in] height Height of the source surface in pixels.
 *
 * @return `true` if the frame was successfully queued for transmission,
 *         or `false` if the parameters are invalid or the queue could not accept the frame.
 */
bool jl_push_surface_async(jl_panel_t* panel, const void* argb, int width, int height);

/**
 * @brief Synchronously transmits a raw pre-encoded JPEG buffer directly to the LCD panel.
 *
 * Bypasses WIC image encoding entirely, directly transferring the raw JPEG payload
 * over the USB serial interface with the hardware framing and checksum.
 * Saves 100% of CPU encoding overhead when the frame is already in JPEG format.
 *
 * @param[in] panel     Valid panel handle returned by ::jl_open().
 * @param[in] jpeg_data Pointer to the raw JPEG bitstream buffer.
 * @param[in] jpeg_size Size of the JPEG bitstream in bytes (must be <= JL_MAX_JPEG_BUFFER).
 *
 * @return `true` on successful transmission, or `false` on failure.
 */
bool jl_push_jpeg_sync(jl_panel_t* panel, const void* jpeg_data, size_t jpeg_size);

/**
 * @brief Asynchronously queues a raw pre-encoded JPEG buffer for transmission.
 *
 * Copies the raw JPEG buffer into the internal transfer queue and immediately signals
 * the background worker thread. Bypasses WIC encoding in the worker thread.
 *
 * @param[in] panel     Valid panel handle returned by ::jl_open().
 * @param[in] jpeg_data Pointer to the raw JPEG bitstream buffer.
 * @param[in] jpeg_size Size of the JPEG bitstream in bytes (must be <= JL_MAX_JPEG_BUFFER).
 *
 * @return `true` if queued successfully, or `false` on failure.
 */
bool jl_push_jpeg_async(jl_panel_t* panel, const void* jpeg_data, size_t jpeg_size);

/**
 * @brief Sends an arbitrary framed command packet to the LCD panel controller.
 *
 * Framing uses magic bytes 0x55, 0xAA, a 16-bit little-endian total length,
 * the command byte, the payload, and a 16-bit little-endian sum checksum.
 *
 * @param[in] panel       Valid panel handle returned by ::jl_open().
 * @param[in] command     Command opcode (e.g., JL_CMD_SET_LIGHT, JL_CMD_LIVE).
 * @param[in] payload     Optional payload data pointer (can be NULL if payload_len is 0).
 * @param[in] payload_len Size of the payload data in bytes.
 *
 * @return `true` if command was transmitted successfully, or `false` on error.
 */
bool jl_send_command(jl_panel_t* panel, uint8_t command, const uint8_t* payload, size_t payload_len);

/**
 * @brief Sets the backlight brightness level of the LCD panel.
 *
 * @param[in] panel   Valid panel handle returned by ::jl_open().
 * @param[in] percent Brightness level from 0 to 100. If 0, turns off the display.
 * @return `true` if brightness command was sent successfully, or `false` on failure.
 */
bool jl_set_brightness(jl_panel_t* panel, int percent);

/**
 * @brief Controls whether the LCD screen is turned on (showing) or turned off (sleep / standby).
 *
 * When set to `false`, sends the End-Of-Image signal (0xFF 0xD9), stops keepalive and frame
 * transmission, and releases the COM port session, allowing the display MCU to power down the screen.
 * When set to `true`, re-establishes the connection and resumes normal operation.
 *
 * @param[in] panel Valid panel handle returned by ::jl_open().
 * @param[in] show  `true` to turn on/show the display, `false` to turn off/sleep.
 */
void jl_show(jl_panel_t* panel, bool show);

/**
 * @brief Checks if the LCD panel is currently turned on and active.
 *
 * @param[in] panel Valid panel handle returned by ::jl_open().
 * @return `true` if display is active/shown, `false` if turned off/standby.
 */
bool jl_is_shown(jl_panel_t* panel);

/**
 * @brief Closes the panel connection and releases all associated resources.
 *
 * Signals the background worker thread to stop, closes the underlying serial
 * port handle, and frees the ::jl_panel_t instance.
 *
 * @param[in] panel Panel handle to close (safe to pass `NULL` if supported by implementation).
 */
void jl_close(jl_panel_t* panel);

#ifdef __cplusplus
}
#endif

#endif /* __JL_PANEL_H__ */

#ifdef JL_PANEL_IMPLEMENTATION

#define INITGUID
#include <windows.h>
#include <wincodec.h>
#include <stdlib.h>
#include <setupapi.h>
#include <devguid.h>
#include <stdio.h>

#define JL_WIDTH 960
#define JL_HEIGHT 480
#define JL_FRAME_BYTES (JL_WIDTH * JL_HEIGHT * 3)
#define JL_DEFAULT_MAX_JPEG (80 * 1024)
#define JL_MAX_JPEG_BUFFER (256 * 1024)

#ifndef JL_CMD_RESTART
#define JL_CMD_RESTART 0x01
#endif

#ifndef JL_CMD_SET_LIGHT
#define JL_CMD_SET_LIGHT 0x03
#endif

#ifndef JL_CMD_GET_DEVICE_INFO
#define JL_CMD_GET_DEVICE_INFO 0x06
#endif

#ifndef JL_CMD_LIVE
#define JL_CMD_LIVE 0x11
#endif

#ifndef JL_CMD_CLOSE
#define JL_CMD_CLOSE 0x21
#endif

struct jl_panel {
    HANDLE hSerial;
    DWORD last_keepalive;

    float jpeg_quality;
    size_t max_transfer_size;

    // Async threading state
    HANDLE hThread;
    HANDLE hEventQuit;
    HANDLE hEventFrameReady;
    CRITICAL_SECTION cs;
    
    // Shared frame buffer
    uint8_t pending_rgb[JL_FRAME_BYTES];
    uint8_t worker_rgb[JL_FRAME_BYTES];
    uint8_t sync_rgb[JL_FRAME_BYTES];
    uint8_t worker_jpeg[JL_MAX_JPEG_BUFFER];
    uint8_t sync_jpeg[JL_MAX_JPEG_BUFFER];
    bool has_new_frame;
    bool is_raw_jpeg;
    size_t pending_jpeg_size;
    uint8_t pending_raw_jpeg[JL_MAX_JPEG_BUFFER];
    char port_name[64];
    bool is_shown;
    int pending_brightness;
};

#ifdef __cplusplus
#define WIC_CALL(obj, method, ...) (obj)->method(__VA_ARGS__)
#define WIC_CALL0(obj, method) (obj)->method()
#define COM_RELEASE(obj) if(obj) { (obj)->Release(); }
#define COM_REF(guid) (guid)
#else
#define WIC_CALL(obj, method, ...) (obj)->lpVtbl->method(obj, __VA_ARGS__)
#define WIC_CALL0(obj, method) (obj)->lpVtbl->method(obj)
#define COM_RELEASE(obj) if(obj) { (obj)->lpVtbl->Release(obj); }
#define COM_REF(guid) (&(guid))
#endif

static const GUID JL_GUID_DEVCLASS_PORTS = {
    0x4d36e978, 0xe325, 0x11ce,
    { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 }
};

bool jl_init(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

static bool write_all(HANDLE h, const uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        DWORD wrote = 0;
        DWORD chunk = (DWORD)((len - off > 20480) ? 20480 : (len - off));
        if (!WriteFile(h, data + off, chunk, &wrote, NULL) || wrote == 0) {
            return false;
        }
        off += wrote;
    }
    return true;
}

static bool find_panel_port(char* out_port, size_t max_len) {
    HDEVINFO dev_info = SetupDiGetClassDevsA(&JL_GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA dev_data;
    dev_data.cbSize = sizeof(SP_DEVINFO_DATA);
    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); ++i) {
        char hwid[512] = {0};
        if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_data, SPDRP_HARDWAREID,
                                              NULL, (PBYTE)hwid, sizeof(hwid), NULL)) {
            HKEY hkey = SetupDiOpenDevRegKey(dev_info, &dev_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hkey != INVALID_HANDLE_VALUE) {
                char port_name[64] = {0};
                DWORD port_len = sizeof(port_name);
                DWORD type = 0;
                if (RegQueryValueExA(hkey, "PortName", NULL, &type, (LPBYTE)port_name, &port_len) == ERROR_SUCCESS) {
                    snprintf(out_port, max_len, "\\\\.\\%s", port_name);
                    found = true;
                }
                RegCloseKey(hkey);
            }
        }
        if (found) break;
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}

static void scale_argb_to_rgb(const uint8_t* argb, int width, int height, uint8_t* rgb) {
    if (width == JL_WIDTH && height == JL_HEIGHT) {
        // Direct 1:1 LCD canvas optimization: zero float divisions, sequential row pointers
        for (int y = 0; y < JL_HEIGHT; y++) {
            int src_y = JL_HEIGHT - 1 - y;
            const uint8_t* s = argb + (size_t)src_y * (size_t)JL_WIDTH * 4;
            uint8_t* d = rgb + (size_t)y * (size_t)JL_WIDTH * 3;
            for (int x = 0; x < JL_WIDTH; x++) {
                int sx = (JL_WIDTH - 1 - x) * 4;
                d[0] = s[sx + 0];
                d[1] = s[sx + 1];
                d[2] = s[sx + 2];
                d += 3;
            }
        }
        return;
    }

    memset(rgb, 0, JL_FRAME_BYTES);

    float scale_x = (float)JL_WIDTH / (float)width;
    float scale_y = (float)JL_HEIGHT / (float)height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int render_w = (int)(width * scale);
    int render_h = (int)(height * scale);

    int offset_x = (JL_WIDTH - render_w) / 2;
    int offset_y = (JL_HEIGHT - render_h) / 2;

    for (int y = 0; y < render_h; y++) {
        int src_y = height - 1 - (int)(y / scale);
        if (src_y < 0) src_y = 0;
        int dst_y = offset_y + y;

        for (int x = 0; x < render_w; x++) {
            int src_x = width - 1 - (int)(x / scale);
            if (src_x < 0) src_x = 0;
            int dst_x = offset_x + x;

            int src_idx = (src_y * width + src_x) * 4;
            int dst_idx = (dst_y * JL_WIDTH + dst_x) * 3;

            rgb[dst_idx + 0] = argb[src_idx + 0];
            rgb[dst_idx + 1] = argb[src_idx + 1];
            rgb[dst_idx + 2] = argb[src_idx + 2];
        }
    }
}

static bool encode_to_jpeg(IWICImagingFactory* in_factory, uint8_t* rgb, float quality, uint8_t* out_jpeg, size_t max_jpeg_size, size_t* out_size) {
    IWICImagingFactory *factory = in_factory;
    bool created_local_factory = false;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    IStream *memStream = NULL;
    bool success = false;

    if (!factory) {
        if (FAILED(CoCreateInstance(COM_REF(CLSID_WICImagingFactory), NULL, CLSCTX_INPROC_SERVER, COM_REF(IID_IWICImagingFactory), (void**)&factory)))
            return false;
        created_local_factory = true;
    }

    CreateStreamOnHGlobal(NULL, TRUE, &memStream);

    if (SUCCEEDED(WIC_CALL(factory, CreateEncoder, COM_REF(GUID_ContainerFormatJpeg), NULL, &encoder))) {
        if (SUCCEEDED(WIC_CALL(encoder, Initialize, memStream, WICBitmapEncoderNoCache))) {
            if (SUCCEEDED(WIC_CALL(encoder, CreateNewFrame, &frame, &props))) {

                PROPBAG2 opt_q = {0};
                opt_q.pstrName = (LPOLESTR)L"ImageQuality";
                VARIANT val_q;
                VariantInit(&val_q);
                val_q.vt = VT_R4;
                val_q.fltVal = quality;
                WIC_CALL(props, Write, 1, &opt_q, &val_q);

                PROPBAG2 option2 = {0};
                option2.pstrName = (LPOLESTR)L"JpegYCrCbSubsampling";
                VARIANT varValue2;
                VariantInit(&varValue2);
                varValue2.vt = VT_UI1;
                varValue2.bVal = 1;
                WIC_CALL(props, Write, 1, &option2, &varValue2);

                WIC_CALL(frame, Initialize, props);
                WIC_CALL(frame, SetSize, JL_WIDTH, JL_HEIGHT);

                WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
                WIC_CALL(frame, SetPixelFormat, &format);

                const UINT stride = (UINT)JL_WIDTH * 3;
                const UINT total_bytes = stride * (UINT)JL_HEIGHT;

                if (SUCCEEDED(WIC_CALL(frame, WritePixels, JL_HEIGHT, stride, total_bytes, rgb))) {
                    if (SUCCEEDED(WIC_CALL0(frame, Commit)) && SUCCEEDED(WIC_CALL0(encoder, Commit))) {
                        HGLOBAL hGlobal;
                        if (SUCCEEDED(GetHGlobalFromStream(memStream, &hGlobal))) {
                            size_t size = GlobalSize(hGlobal);
                            if (size <= max_jpeg_size) {
                                void* ptr = GlobalLock(hGlobal);
                                if (ptr) {
                                    memcpy(out_jpeg, ptr, size);
                                    *out_size = size;
                                    GlobalUnlock(hGlobal);
                                    success = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    COM_RELEASE(frame);
    COM_RELEASE(props);
    COM_RELEASE(encoder);
    COM_RELEASE(memStream);
    if (created_local_factory) {
        COM_RELEASE(factory);
    }
    return success;
}

DWORD WINAPI jl_worker_thread(LPVOID param) {
    jl_panel_t* p = (jl_panel_t*)param;

    CoInitializeEx(NULL, COINIT_MULTITHREADED); 

    IWICImagingFactory *factory = NULL;
    CoCreateInstance(COM_REF(CLSID_WICImagingFactory), NULL, CLSCTX_INPROC_SERVER, COM_REF(IID_IWICImagingFactory), (void**)&factory);

    HANDLE events[2] = { p->hEventQuit, p->hEventFrameReady };

    while (1) {
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, 500);
        if (wait == WAIT_OBJECT_0) break; // Quit event fired

        bool encode_this_cycle = false;
        bool is_raw = false;
        size_t raw_len = 0;
        float quality = 0.75f;
        size_t max_transfer = JL_DEFAULT_MAX_JPEG;
        bool can_run = false;

        EnterCriticalSection(&p->cs);
        can_run = p->is_shown && (p->hSerial != INVALID_HANDLE_VALUE) && (p->hSerial != NULL);
        quality = p->jpeg_quality;
        max_transfer = p->max_transfer_size;
        if (can_run && p->has_new_frame) {
            is_raw = p->is_raw_jpeg;
            if (is_raw) {
                raw_len = p->pending_jpeg_size;
                memcpy(p->worker_jpeg, p->pending_raw_jpeg, raw_len);
            } else {
                memcpy(p->worker_rgb, p->pending_rgb, JL_FRAME_BYTES);
            }
            p->has_new_frame = false;
            encode_this_cycle = true;
        }
        LeaveCriticalSection(&p->cs);

        if (!can_run) continue;

        DWORD now = GetTickCount();

        EnterCriticalSection(&p->cs);
        if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
            if (now - p->last_keepalive > 1000 || p->last_keepalive == 0) {
                uint8_t live_cmd[] = { 0x55, 0xAA, 0x07, 0x00, 0x11, 0x17, 0x01 };
                write_all(p->hSerial, live_cmd, sizeof(live_cmd));
                p->last_keepalive = now;
            }
        }
        LeaveCriticalSection(&p->cs);

        if (encode_this_cycle) {
            size_t jpeg_size = 0;
            const uint8_t* send_ptr = NULL;

            if (is_raw) {
                send_ptr = p->worker_jpeg;
                jpeg_size = raw_len;
            } else {
                if (encode_to_jpeg(factory, p->worker_rgb, quality, p->worker_jpeg, max_transfer, &jpeg_size)) {
                    send_ptr = p->worker_jpeg;
                }
            }

            if (send_ptr && jpeg_size > 0) {
                uint32_t length = (uint32_t)jpeg_size;
                uint16_t sum = 0;
                uint8_t* len_bytes = (uint8_t*)&length;
                sum += len_bytes[0] + len_bytes[1] + len_bytes[2] + len_bytes[3];
                for(size_t i = 0; i < jpeg_size; i++) sum += send_ptr[i];

                EnterCriticalSection(&p->cs);
                if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
                    write_all(p->hSerial, (const uint8_t*)&length, 4);
                    write_all(p->hSerial, send_ptr, jpeg_size);
                    write_all(p->hSerial, (const uint8_t*)&sum, 2);
                }
                LeaveCriticalSection(&p->cs);
            }
        }
    }

    COM_RELEASE(factory);
    CoUninitialize();
    return 0;
}

jl_panel_t* jl_open(const char* port) {
    char resolved_port[64] = {0};

    if (!port || port[0] == '\0') {
        if (!find_panel_port(resolved_port, sizeof(resolved_port))) {
            return NULL;
        }
    } else {
        strncpy(resolved_port, port, sizeof(resolved_port) - 1);
    }

    HANDLE h = CreateFileA(resolved_port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(h, &dcb);
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    SetCommTimeouts(h, &timeouts);

    jl_panel_t* p = (jl_panel_t*)malloc(sizeof(jl_panel_t));
    memset(p, 0, sizeof(jl_panel_t));
    p->hSerial = h;
    strncpy(p->port_name, resolved_port, sizeof(p->port_name) - 1);
    p->is_shown = true;
    p->jpeg_quality = 0.75f;
    p->max_transfer_size = JL_DEFAULT_MAX_JPEG;
    p->pending_brightness = -1;

    InitializeCriticalSection(&p->cs);
    p->hEventQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
    p->hEventFrameReady = CreateEvent(NULL, FALSE, FALSE, NULL);
    p->hThread = CreateThread(NULL, 0, jl_worker_thread, p, 0, NULL);

    return p;
}

void jl_set_jpeg_quality(jl_panel_t* p, float quality) {
    if (!p) return;
    if (quality < 0.01f) quality = 0.01f;
    if (quality > 1.0f)  quality = 1.0f;

    EnterCriticalSection(&p->cs);
    p->jpeg_quality = quality;
    LeaveCriticalSection(&p->cs);
}

void jl_set_max_transfer_size(jl_panel_t* p, size_t max_bytes) {
    if (!p || max_bytes == 0) return;
    if (max_bytes > JL_MAX_JPEG_BUFFER) max_bytes = JL_MAX_JPEG_BUFFER;

    EnterCriticalSection(&p->cs);
    p->max_transfer_size = max_bytes;
    LeaveCriticalSection(&p->cs);
}

bool jl_push_surface_sync(jl_panel_t* p, const void* rgba, int width, int height) {
    if (!p || !p->is_shown || p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL || !rgba || width <= 0 || height <= 0) return false;

    float quality;
    size_t max_transfer;

    EnterCriticalSection(&p->cs);
    quality = p->jpeg_quality;
    max_transfer = p->max_transfer_size;
    LeaveCriticalSection(&p->cs);

    DWORD now = GetTickCount();
    
    EnterCriticalSection(&p->cs);
    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        if (now - p->last_keepalive > 1000 || p->last_keepalive == 0) {
            uint8_t live_cmd[] = { 0x55, 0xAA, 0x07, 0x00, 0x11, 0x17, 0x01 };
            DWORD w;
            WriteFile(p->hSerial, live_cmd, sizeof(live_cmd), &w, NULL);
            p->last_keepalive = now;
        }
    }
    LeaveCriticalSection(&p->cs);
    
    scale_argb_to_rgb((const uint8_t*)rgba, width, height, p->sync_rgb);

    size_t jpeg_size = 0;
    if (!encode_to_jpeg(NULL, p->sync_rgb, quality, p->sync_jpeg, max_transfer, &jpeg_size)) return false;
    
    uint32_t length = (uint32_t)jpeg_size;
    uint16_t sum = 0;
    
    uint8_t* len_bytes = (uint8_t*)&length;
    sum += len_bytes[0] + len_bytes[1] + len_bytes[2] + len_bytes[3];
    
    for(size_t i = 0; i < jpeg_size; i++) {
        sum += p->sync_jpeg[i];
    }
    
    EnterCriticalSection(&p->cs);
    bool sync_ok = false;
    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        sync_ok = write_all(p->hSerial, (const uint8_t*)&length, 4) &&
                  write_all(p->hSerial, p->sync_jpeg, jpeg_size) &&
                  write_all(p->hSerial, (const uint8_t*)&sum, 2);
    }
    LeaveCriticalSection(&p->cs);
    
    return sync_ok;
}

bool jl_push_surface_async(jl_panel_t* p, const void* rgba, int width, int height) {
    if (!p || !p->is_shown || p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL || !rgba || width <= 0 || height <= 0) return false;

    EnterCriticalSection(&p->cs);
    scale_argb_to_rgb((const uint8_t*)rgba, width, height, p->pending_rgb);
    p->is_raw_jpeg = false;
    p->has_new_frame = true;
    LeaveCriticalSection(&p->cs);

    SetEvent(p->hEventFrameReady); 
    return true;
}

bool jl_push_jpeg_sync(jl_panel_t* p, const void* jpeg_data, size_t jpeg_size) {
    if (!p || !p->is_shown || p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL || !jpeg_data || jpeg_size == 0 || jpeg_size > JL_MAX_JPEG_BUFFER) return false;

    DWORD now = GetTickCount();
    EnterCriticalSection(&p->cs);
    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        if (now - p->last_keepalive > 1000 || p->last_keepalive == 0) {
            uint8_t live_cmd[] = { 0x55, 0xAA, 0x07, 0x00, 0x11, 0x17, 0x01 };
            DWORD w;
            WriteFile(p->hSerial, live_cmd, sizeof(live_cmd), &w, NULL);
            p->last_keepalive = now;
        }
    }
    LeaveCriticalSection(&p->cs);

    uint32_t length = (uint32_t)jpeg_size;
    uint16_t sum = 0;
    const uint8_t* ptr = (const uint8_t*)jpeg_data;
    uint8_t* len_bytes = (uint8_t*)&length;
    sum += len_bytes[0] + len_bytes[1] + len_bytes[2] + len_bytes[3];
    for (size_t i = 0; i < jpeg_size; i++) {
        sum += ptr[i];
    }

    bool jpeg_ok = false;
    EnterCriticalSection(&p->cs);
    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        jpeg_ok = write_all(p->hSerial, (const uint8_t*)&length, 4) &&
                  write_all(p->hSerial, ptr, jpeg_size) &&
                  write_all(p->hSerial, (const uint8_t*)&sum, 2);
    }
    LeaveCriticalSection(&p->cs);

    return jpeg_ok;
}

bool jl_push_jpeg_async(jl_panel_t* p, const void* jpeg_data, size_t jpeg_size) {
    if (!p || !p->is_shown || p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL || !jpeg_data || jpeg_size == 0 || jpeg_size > JL_MAX_JPEG_BUFFER) return false;

    EnterCriticalSection(&p->cs);
    memcpy(p->pending_raw_jpeg, jpeg_data, jpeg_size);
    p->pending_jpeg_size = jpeg_size;
    p->is_raw_jpeg = true;
    p->has_new_frame = true;
    LeaveCriticalSection(&p->cs);

    SetEvent(p->hEventFrameReady);
    return true;
}

void jl_show(jl_panel_t* p, bool show) {
    if (!p) return;
    EnterCriticalSection(&p->cs);
    if (p->is_shown == show) {
        LeaveCriticalSection(&p->cs);
        return;
    }
    p->is_shown = show;

    if (!show) {
        // Turning off display: send EOI to flush decoder and stop transmission
        if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
            uint8_t eoi[2] = {0xFF, 0xD9};
            DWORD w;
            WriteFile(p->hSerial, eoi, 2, &w, NULL);
            FlushFileBuffers(p->hSerial);
        }
        p->has_new_frame = false;
    } else {
        // Turning on display: re-open COM port if it was closed, otherwise trigger keepalive
        if (p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL) {
            HANDLE h = CreateFileA(p->port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                DCB dcb = {0};
                dcb.DCBlength = sizeof(DCB);
                if (GetCommState(h, &dcb)) {
                    dcb.BaudRate = CBR_115200;
                    dcb.ByteSize = 8;
                    dcb.Parity = NOPARITY;
                    dcb.StopBits = ONESTOPBIT;
                    SetCommState(h, &dcb);
                }
                COMMTIMEOUTS timeouts = {0};
                timeouts.ReadIntervalTimeout = 50;
                timeouts.ReadTotalTimeoutConstant = 50;
                SetCommTimeouts(h, &timeouts);
                p->hSerial = h;
            }
        }
        p->last_keepalive = 0; // Trigger keepalive immediately
    }
    LeaveCriticalSection(&p->cs);
}

bool jl_is_shown(jl_panel_t* p) {
    if (!p) return false;
    EnterCriticalSection(&p->cs);
    bool shown = p->is_shown;
    LeaveCriticalSection(&p->cs);
    return shown;
}

bool jl_send_command(jl_panel_t* p, uint8_t command, const uint8_t* payload, size_t payload_len) {
    if (!p) return false;
    EnterCriticalSection(&p->cs);
    if (!p->is_shown || p->hSerial == INVALID_HANDLE_VALUE || p->hSerial == NULL) {
        LeaveCriticalSection(&p->cs);
        return false;
    }

    size_t total_len = 7 + payload_len;
    uint8_t* frame = (uint8_t*)malloc(total_len);
    if (!frame) {
        LeaveCriticalSection(&p->cs);
        return false;
    }

    frame[0] = 0x55;
    frame[1] = 0xAA;
    frame[2] = (uint8_t)(total_len & 0xFF);
    frame[3] = (uint8_t)((total_len >> 8) & 0xFF);
    frame[4] = command;
    if (payload && payload_len > 0) {
        memcpy(&frame[5], payload, payload_len);
    }

    uint16_t sum = 0;
    for (size_t i = 0; i < total_len - 2; i++) {
        sum += frame[i];
    }
    frame[total_len - 2] = (uint8_t)(sum & 0xFF);
    frame[total_len - 1] = (uint8_t)((sum >> 8) & 0xFF);

    bool ok = write_all(p->hSerial, frame, total_len);
    free(frame);
    LeaveCriticalSection(&p->cs);
    return ok;
}

bool jl_set_brightness(jl_panel_t* p, int percent) {
    if (!p) return false;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    EnterCriticalSection(&p->cs);
    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        // 1. Flush active JPEG frame decoder so the microcontroller returns to command mode
        const uint8_t eoi[] = { 0xFF, 0xD9, 0xFF, 0xD9 };
        write_all(p->hSerial, eoi, sizeof(eoi));
        Sleep(30);

        // 2. Send framed SetLight command (0x03)
        uint8_t b_val = (uint8_t)percent;
        size_t total_len = 8;
        uint8_t frame[8];
        frame[0] = 0x55;
        frame[1] = 0xAA;
        frame[2] = 0x08;
        frame[3] = 0x00;
        frame[4] = JL_CMD_SET_LIGHT;
        frame[5] = b_val;
        uint16_t sum = 0;
        for (int i = 0; i < 6; i++) sum += frame[i];
        frame[6] = (uint8_t)(sum & 0xFF);
        frame[7] = (uint8_t)((sum >> 8) & 0xFF);
        write_all(p->hSerial, frame, sizeof(frame));
        Sleep(50);

        if (percent > 0) {
            // 3. Re-arm live streaming mode (0x11)
            uint8_t live_cmd[] = { 0x55, 0xAA, 0x07, 0x00, 0x11, 0x17, 0x01 };
            write_all(p->hSerial, live_cmd, sizeof(live_cmd));
            p->last_keepalive = GetTickCount();
            Sleep(20);
        } else {
            // 3. Send JL_CMD_CLOSE (0x21) for firmware standby
            uint8_t close_cmd[] = { 0x55, 0xAA, 0x07, 0x00, JL_CMD_CLOSE, 0x27, 0x01 };
            write_all(p->hSerial, close_cmd, sizeof(close_cmd));
            Sleep(20);
        }
    }
    LeaveCriticalSection(&p->cs);

    return true;
}

void jl_close(jl_panel_t* p) {
    if (!p) return;
    
    // Shut down thread safely
    SetEvent(p->hEventQuit);
    WaitForSingleObject(p->hThread, INFINITE);
    CloseHandle(p->hThread);
    CloseHandle(p->hEventQuit);
    CloseHandle(p->hEventFrameReady);
    DeleteCriticalSection(&p->cs);

    if (p->hSerial != INVALID_HANDLE_VALUE && p->hSerial != NULL) {
        uint8_t eoi[2] = {0xFF, 0xD9};
        DWORD w; 
        WriteFile(p->hSerial, eoi, 2, &w, NULL);
        CloseHandle(p->hSerial);
        p->hSerial = INVALID_HANDLE_VALUE;
    }
    free(p);
}
#endif // JL_PANEL_IMPLEMENTATION
