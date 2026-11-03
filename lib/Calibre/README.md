# Calibre Wireless Library

Implementation of Calibre's Smart Device App protocol for syncing ebooks with Calibre desktop on ESP32 microcontrollers.

## Overview

This library enables wireless book synchronization between PapyriX e-reader devices and Calibre (https://calibre-ebook.com/). It implements the "Smart Device" protocol that Calibre uses to communicate with compatible devices over WiFi.

**Features:**
- UDP broadcast discovery (device broadcasts "hello" to find Calibre)
- TCP connection with length-prefixed JSON protocol
- Streaming file reception
- Progress callbacks for UI updates
- Password authentication support
- Book deletion support

## Architecture

### Files

- **`calibre_wireless.h`** - Public API - all user-facing types and functions
- **`calibre_wireless.c`** - Core library: config, buffers, JSON parsing
- **`calibre_common.h/c`** - Shared internals: logging, socket utils, validation
- **`calibre_internal.h`** - Private structures and function declarations
- **`calibre_network.c`** - Network layer: UDP discovery, TCP, message send/recv
- **`calibre_protocol.c`** - Protocol handlers for all opcodes
- **`calibre_storage.h`** - Storage abstraction interface
- **`calibre_storage_sdfat.cpp`** - SdFat implementation of storage

## Protocol Specification

### Message Format

All messages use a length-prefixed JSON format:

```
[LENGTH][JSON]
```

- **LENGTH**: ASCII decimal byte count of the serialized UTF-8 JSON, excluding the prefix.
- **JSON**: JSON array `[opcode, payload]` where opcode is an integer. Spaces count toward the byte count.

Example: `22[9, {"canSync": true}]`

### Opcodes

- **0 - OK** (Both) - Success acknowledgment
- **1 - SET_CALIBRE_DEVICE_INFO** (S→D) - Device metadata
- **2 - SET_CALIBRE_DEVICE_NAME** (S→D) - Update device name
- **3 - GET_DEVICE_INFORMATION** (S→D) - Query device info
- **4 - TOTAL_SPACE** (S→D) - Query total storage
- **5 - FREE_SPACE** (S→D) - Query available storage
- **6 - GET_BOOK_COUNT** (S→D) - Query book inventory
- **7 - SEND_BOOKLISTS** (S→D) - Metadata updates (no response!)
- **8 - SEND_BOOK** (S→D) - Initiate book transfer
- **9 - GET_INITIALIZATION_INFO** (S→D) - Initial handshake
- **11 - BOOK_DONE** (D→S) - Transfer complete
- **12 - NOOP** (Both) - Keep-alive ping (see NOOP behavior below)
- **13 - DELETE_BOOK** (S→D) - Remove books
- **14 - GET_BOOK_FILE_SEGMENT** (S→D) - Download from device
- **15 - GET_BOOK_METADATA** (S→D) - Request metadata
- **16 - SEND_BOOK_METADATA** (S→D) - Send metadata
- **17 - DISPLAY_MESSAGE** (S→D) - Show message to user
- **18 - CALIBRE_BUSY** (S→D) - Server busy
- **19 - SET_LIBRARY_INFO** (S→D) - Library metadata
- **20 - ERROR** (Both) - Error response

S→D = Server (Calibre) to Device, D→S = Device to Server

### NOOP Behavior

NOOP messages have special handling based on their payload:

- **Empty payload `{}`**: Respond with OK - this is a keep-alive ping
- **Non-empty payload `{"count": N}`**: Do NOT respond - Calibre sends these with `wait_for_response=False`

Calibre sends NOOP with payload during `books()` processing to update progress. If the device responds to these, the response sits in the TCP buffer and corrupts subsequent request/response pairs (e.g., FREE_SPACE receives the stale NOOP response instead of its own).

### Connection Flow

```
Device                              Calibre Server
  │                                      │
  │── UDP broadcast "hello" ────────────>│  (ports: 54982, 48123, 39001, 44044, 59678)
  │<── "calibre wireless...;0,9090" ─────│
  │                                      │
  │── TCP connect ──────────────────────>│  (port from discovery response)
  │<── GET_INITIALIZATION_INFO ──────────│
  │── OK + capabilities ────────────────>│
  │<── GET_DEVICE_INFORMATION ───────────│
  │── OK + device_info ─────────────────>│
  │<── SET_CALIBRE_DEVICE_INFO ──────────│
  │── OK ───────────────────────────────>│
  │<── FREE_SPACE ───────────────────────│
  │── OK + free_space_on_device ────────>│
  │<── SET_LIBRARY_INFO ─────────────────│  (includes fieldMetadata)
  │── OK ───────────────────────────────>│
  │<── NOOP {count: 0} ──────────────────│  (no response! wait_for_response=False)
  │<── SEND_BOOKLISTS ───────────────────│  (no response expected!)
  │                                      │
  │        [Ready for transfers]         │
  │                                      │
```

**Discovery mechanism:** The device broadcasts "hello" on UDP ports every 500ms (max 20 times). Calibre listens on these ports and responds with its TCP port information. The device then connects to Calibre as a TCP client.

`SET_LIBRARY_INFO` includes `fieldMetadata` with the library field definitions.

### Book Transfer

Two-phase transfer protocol:

```
Server: SEND_BOOK {lpath, length, title, authors, uuid, calibre_id}
Device: OK {willAccept: true}
Server: [raw binary data - <length> bytes]
Device: (accumulates until size reached)
Device: (does not send BOOK_DONE)
```

### Device Capabilities (GET_INITIALIZATION_INFO response)

**Required capabilities**: Calibre requires ALL of these to be `true`, otherwise it rejects with "The app on your device is too old":
- `canStreamBooks`
- `canStreamMetadata`
- `canReceiveBookBinary`
- `canDeleteMultipleBooks`

```json
{
  "appName": "PapyriX Reader",
  "acceptedExtensions": ["epub", "txt", "md", "xtc", "xtch"],
  "cacheUsesLpaths": true,
  "canAcceptLibraryInfo": true,
  "canDeleteMultipleBooks": true,
  "canReceiveBookBinary": true,
  "canSendOkToSendbook": true,
  "canStreamBooks": true,
  "canStreamMetadata": true,
  "canUseCachedMetadata": true,
  "ccVersionNumber": 128,
  "coverHeight": 240,
  "deviceKind": "PapyriX E-Ink Reader",
  "deviceName": "My PapyriX",
  "extensionPathLengths": {},
  "maxBookContentPacketLen": 4096,
  "passwordHash": "",
  "useUuidFileNames": false,
  "versionOK": true,
  "device_store_uuid": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
}
```

### GET_DEVICE_INFORMATION Response

```json
{
  "device_info": {
    "device_store_uuid": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
    "device_name": "PapyriX Reader"
  },
  "device_version": "PapyriX 1.0",
  "version": "1.0"
}
```

## Memory Budget

- **JSON recv buffer** - 2KB, Heap (once) - Protocol messages
- **File chunk buffer** - 4KB, Heap (per book) - Streaming to SD
- **Response buffer** - 256B, Stack - Building JSON responses
- **Path buffer** - 256B, Stack - File path construction

**Total heap per connection**: ~6KB (allocated once, reused)

## Safety & Security

### Path Validation

The library validates incoming paths before use:
- No absolute paths (must not start with `/`)
- No path traversal (must not contain `..`)
- Extension whitelist: Uses extensions from device config (set via `calibre_device_config_add_ext()`)
- Length limit: 256 characters

The same extensions configured for Calibre handshake are used for security validation.

### Size Validation

- Maximum book size: 100MB (configurable via `CALIBRE_MAX_BOOK_SIZE`)
- Size must be > 0

### Resource Management

- Socket operations use timeouts.
- The library attempts to remove partial files after a receive error.
- The library frees address information after DNS resolution.

## API Reference

### Initialization

```c
// Initialize library (call once at startup)
calibre_err_t calibre_init(void);

// Clean up (call at shutdown)
void calibre_deinit(void);
```

### Connection Management

```c
// Create connection context
calibre_conn_t* calibre_conn_create(
    const calibre_device_config_t* config,
    const calibre_callbacks_t* callbacks
);

// Destroy connection
void calibre_conn_destroy(calibre_conn_t* conn);

// Start UDP discovery
calibre_err_t calibre_start_discovery(calibre_conn_t* conn, uint16_t port);

// Stop discovery
void calibre_stop_discovery(calibre_conn_t* conn);

// Connect directly to Calibre
calibre_err_t calibre_connect(calibre_conn_t* conn, const char* host, uint16_t port);

// Disconnect
void calibre_disconnect(calibre_conn_t* conn);

// Check connection state
bool calibre_is_connected(const calibre_conn_t* conn);
```

### Main Loop

```c
// Process incoming messages (call regularly)
calibre_err_t calibre_process(calibre_conn_t* conn, uint32_t timeout_ms);
```

`timeout_ms` controls the initial TCP readiness wait, not the total call duration. Zero polls readiness without waiting, but message reception can then wait for `CALIBRE_RECV_TIMEOUT_MS` (30,000 ms). Discovery, connection, and message handlers can also do work during the call.

### Configuration

```c
// Set books directory
calibre_err_t calibre_set_books_dir(calibre_conn_t* conn, const char* path);

// Set password
calibre_err_t calibre_set_password(calibre_conn_t* conn, const char* password);
```

### Callbacks

```c
typedef struct {
  calibre_progress_cb_t on_progress;   // Transfer progress
  calibre_book_received_cb_t on_book;  // Book received
  calibre_message_cb_t on_message;     // Message from Calibre
  calibre_book_deleted_cb_t on_delete; // Book deletion request
  void* user_ctx;                      // User context
} calibre_callbacks_t;
```

## Integration Example

See `src/states/CalibreSyncState.cpp` for a complete integration example.

Basic usage:

```c
// Initialize
calibre_init();

// Configure device
calibre_device_config_t config;
calibre_device_config_init(&config);
calibre_device_config_add_ext(&config, "epub");
calibre_device_config_add_ext(&config, "pdf");

// Set up callbacks
calibre_callbacks_t callbacks = {
    .on_progress = my_progress_callback,
    .on_book = my_book_callback,
    .on_message = my_message_callback,
    .user_ctx = my_context
};

// Create connection
calibre_conn_t* conn = calibre_conn_create(&config, &callbacks);
calibre_set_books_dir(conn, "/sd/Calibre");

// Start discovery
calibre_start_discovery(conn, 9090);

// Main loop
while (running) {
    calibre_err_t err = calibre_process(conn, 100);
    if (err != CALIBRE_OK && err != CALIBRE_ERR_TIMEOUT) {
        // Handle error
    }
}

// Cleanup
calibre_conn_destroy(conn);
calibre_deinit();
```

## Testing

### Simulators

Use the included simulators to test without hardware. Both simulators implement the real Calibre 8.x protocol flow.

**Device Simulator** - Simulates a PapyriX device connecting to real Calibre:
```bash
# Auto-discover Calibre on local network
node scripts/device-simulator.mjs

# Connect directly to Calibre at specific IP
node scripts/device-simulator.mjs -i 192.168.1.100
```

**Calibre Simulator** - Simulates Calibre desktop for testing device firmware:
```bash
# Start simulator and wait for device connection
node scripts/calibre-simulator.mjs

# Start and send a book after device connects
node scripts/calibre-simulator.mjs -s test.epub
```

Both simulators follow the real protocol flow:
1. GET_INITIALIZATION_INFO → GET_DEVICE_INFORMATION → SET_CALIBRE_DEVICE_INFO
2. FREE_SPACE → SET_LIBRARY_INFO (with fieldMetadata)
3. NOOP (no response) → SEND_BOOKLISTS (no response)
4. Optional: SEND_BOOK for transfers

### Security Tests

The library rejects:

```bash
# Path traversal attack
lpath: "../../../etc/passwd"  → Rejected

# Oversized book
length: 200000000 (200MB)     → Rejected

# Invalid extension
lpath: "malware.exe"          → Rejected
```

## Error Codes

- **0** - `CALIBRE_OK` - Success
- **-1** - `CALIBRE_ERR_NOMEM` - Out of memory
- **-2** - `CALIBRE_ERR_INVALID_ARG` - Invalid argument
- **-3** - `CALIBRE_ERR_SOCKET` - Socket error
- **-4** - `CALIBRE_ERR_CONNECT` - Connection failed
- **-5** - `CALIBRE_ERR_TIMEOUT` - Operation timed out
- **-6** - `CALIBRE_ERR_PROTOCOL` - Protocol error
- **-7** - `CALIBRE_ERR_JSON_PARSE` - JSON parsing error
- **-8** - `CALIBRE_ERR_AUTH` - Authentication failed
- **-9** - `CALIBRE_ERR_WRITE_FILE` - File write error
- **-10** - `CALIBRE_ERR_SD_CARD` - SD card error
- **-11** - `CALIBRE_ERR_DISCONNECTED` - Peer disconnected
- **-12** - `CALIBRE_ERR_CANCELLED` - Operation cancelled
- **-13** - `CALIBRE_ERR_BUSY` - Resource busy

## References

- Calibre source: `https://github.com/kovidgoyal/calibre/blob/master/src/calibre/devices/smart_device_app/driver.py`
- Smart Device protocol documentation: Built-in Calibre help
