#pragma once

#include "I18n.h"

namespace i18n {

// clang-format off
static constexpr const char* DEFAULTS[static_cast<int>(StrId::STR__COUNT)] = {
    // Button labels
    "Back",                   // BACK
    "Open",                   // OPEN
    "Select",                 // SELECT
    "Cancel",                 // CANCEL
    "Confirm",                // CONFIRM
    "Done",                   // DONE
    "Retry",                  // RETRY
    "Stop",                   // STOP
    "Scan",                   // SCAN
    "Run",                    // RUN
    "Go",                     // GO
    "Restart",                // RESTART
    "Connect",                // CONNECT
    "Read",                   // READ
    "Delete",                 // DELETE_BTN
    "File",                   // FILE
    "Apps",                   // APPS
    "Home",                   // HOME
    "Yes",                    // YES
    "No",                     // NO

    // Menu items
    "Settings",               // SETTINGS
    "Reader",                 // READER
    "Device",                 // DEVICE
    "Cleanup",                // CLEANUP
    "System Info",            // SYSTEM_INFO
    "Chapters",               // CHAPTERS
    "Bookmarks",              // BOOKMARKS
    "Join Network",           // JOIN_NETWORK
    "Create Hotspot",         // CREATE_HOTSPOT
    "WiFi Transfer",          // WIFI_TRANSFER
    "Calibre Sync",           // CALIBRE_SYNC
    "Language",               // LANGUAGE

    // Screen titles
    "Reader Settings",        // READER_SETTINGS
    "Device Settings",        // DEVICE_SETTINGS
    "Files",                  // FILES
    "Network Mode",           // NETWORK_MODE
    "Select Network",         // SELECT_NETWORK
    "Connecting",             // CONNECTING_TITLE
    "Web Server",             // WEB_SERVER
    "Go to Page",             // GO_TO_PAGE
    "Menu",                   // MENU

    // Settings labels
    "Theme",                  // THEME
    "Font Size",              // FONT_SIZE
    "Text Layout",            // TEXT_LAYOUT
    "Line Spacing",           // LINE_SPACING
    "Text Anti-Aliasing",     // TEXT_ANTI_ALIASING
    "Paragraph Alignment",    // PARAGRAPH_ALIGNMENT
    "Hyphenation",            // HYPHENATION
    "Show Images",            // SHOW_IMAGES
    "Status Bar",             // STATUS_BAR
    "Reading Orientation",    // READING_ORIENTATION
    "Auto Sleep Timeout",     // AUTO_SLEEP_TIMEOUT
    "Sleep Screen",           // SLEEP_SCREEN
    "Startup Behavior",       // STARTUP_BEHAVIOR
    "Short Power Button",     // SHORT_POWER_BUTTON
    "Pages Per Refresh",      // PAGES_PER_REFRESH
    "Sunlight Fading Fix",    // SUNLIGHT_FADING_FIX
    "Front Buttons",          // FRONT_BUTTONS
    "Side Buttons",           // SIDE_BUTTONS
    "Full Book Process",      // FULL_BOOK_PROCESS

    // Settings enum values
    "ON",                     // ON
    "OFF",                    // OFF
    "XSmall",                 // XSMALL
    "Small",                  // SMALL
    "Normal",                 // NORMAL
    "Large",                  // LARGE
    "Compact",                // COMPACT
    "Standard",               // STANDARD
    "Relaxed",                // RELAXED
    "Justified",              // JUSTIFIED
    "Left",                   // LEFT
    "Center",                 // CENTER
    "Right",                  // RIGHT
    "None",                   // NONE_VAL
    "Title",                  // TITLE_VAL
    "Chapter",                // CHAPTER_VAL
    "Portrait",               // PORTRAIT
    "Landscape CW",           // LANDSCAPE_CW
    "Inverted",               // INVERTED
    "Landscape CCW",          // LANDSCAPE_CCW
    "Dark",                   // DARK
    "Light",                  // LIGHT
    "Custom",                 // CUSTOM
    "Cover",                  // COVER
    "Last Document",          // LAST_DOCUMENT
    "Ignore",                 // IGNORE
    "Sleep",                  // SLEEP_VAL
    "Page Turn",              // PAGE_TURN
    "Never",                  // NEVER
    "5 min",                  // MIN_5
    "10 min",                 // MIN_10
    "15 min",                 // MIN_15
    "30 min",                 // MIN_30
    "Prev/Next",              // PREV_NEXT
    "Next/Prev",              // NEXT_PREV
    "B/C/L/R",                // FRONT_BCLR
    "L/R/B/C",                // FRONT_LRBC

    // Cleanup
    "Clear Book Cache",       // CLEAR_BOOK_CACHE
    "Clear Device Storage",   // CLEAR_DEVICE_STORAGE
    "Factory Reset",          // FACTORY_RESET
    "Clear Caches?",          // CLEAR_CACHES_Q
    "This will delete all book caches",    // CLEAR_CACHES_MSG1
    "and reading progress.",               // CLEAR_CACHES_MSG2
    "Clear Device?",          // CLEAR_DEVICE_Q
    "This will erase internal flash",      // CLEAR_DEVICE_MSG1
    "storage. Device will restart.",        // CLEAR_DEVICE_MSG2
    "Factory Reset?",         // FACTORY_RESET_Q
    "This will erase ALL data including",  // FACTORY_RESET_MSG1
    "settings and WiFi credentials!",      // FACTORY_RESET_MSG2
    "Clearing cache...",      // CLEARING_CACHE
    "Cache cleared",          // CACHE_CLEARED
    "No cache to clear",      // NO_CACHE_TO_CLEAR
    "Clearing device storage...",           // CLEARING_STORAGE
    "Resetting device...",    // RESETTING_DEVICE
    "Done. Restarting...",    // DONE_RESTARTING

    // Status messages
    "Scanning...",            // SCANNING
    "Connecting...",          // CONNECTING
    "Getting IP address...",  // GETTING_IP
    "Connected!",             // CONNECTED
    "Starting...",            // STARTING
    "Loading...",             // LOADING
    "Indexing...",            // INDEXING
    "Opening book...",        // OPENING_BOOK
    "Deleting...",            // DELETING
    "No book open",           // NO_BOOK_OPEN
    "Press \"File\" to explore",           // PRESS_FILE_TO_EXPLORE
    "No books found",         // NO_BOOKS_FOUND
    "End of book",            // END_OF_BOOK
    "Failed to load page",    // FAILED_TO_LOAD_PAGE
    "Cannot delete active book",           // CANNOT_DELETE_ACTIVE
    "Deleted",                // DELETED
    "Delete failed",          // DELETE_FAILED
    "Server stopped",         // SERVER_STOPPED
    "Returning to library...",             // RETURNING_TO_LIBRARY
    "SLEEPING",               // SLEEPING

    // Network
    "Connect to existing WiFi",            // CONNECT_WIFI
    "Create WiFi hotspot",    // CREATE_WIFI_HOTSPOT
    "No networks found",      // NO_NETWORKS_FOUND
    "Press Confirm to scan again",         // PRESS_CONFIRM_SCAN
    "Disconnected. Restart?", // DISCONNECTED_RESTART

    // Calibre
    "Waiting for Calibre...", // WAITING_FOR_CALIBRE
    "Connecting to Calibre...",            // CONNECTING_TO_CALIBRE
    "In Calibre: Connect/share > Wireless device",  // CALIBRE_HELP

    // Format strings
    "IP: %s",                 // FMT_IP
    "Received %d book(s)",    // FMT_RECEIVED_BOOKS
    "of %d",                  // FMT_PAGE_OF

    // System info
    "Version",                // VERSION
    "Uptime",                 // UPTIME
    "Battery",                // BATTERY
    "Chip",                   // CHIP
    "CPU",                    // CPU
    "Free Memory",            // FREE_MEMORY
    "Internal Disk",          // INTERNAL_DISK
    "SD Card",                // SD_CARD
    "Ready",                  // READY
    "Not available",          // NOT_AVAILABLE

    // Keyboard
    "Backspace",              // BACKSPACE
    "Space",                  // SPACE

    // Error
    "Error",                  // ERROR
    "Press any button to continue",        // PRESS_ANY_BUTTON

    // Confirm dialog
    "Confirm Delete",         // CONFIRM_DELETE

    // Misc
    "Enter Text",             // ENTER_TEXT
    "No Cover",               // NO_COVER
    "Papyrix",                // PAPYRIX
    "Add",                    // ADD
    "No bookmarks yet",       // NO_BOOKMARKS
    "Books",                  // BOOKS
    "Delete this file?",      // DELETE_FILE_Q
    "Delete this folder?",    // DELETE_FOLDER_Q
    "Enter Password",         // ENTER_PASSWORD
    "Save Password?",         // SAVE_PASSWORD_Q
    "Save password for this network?",  // SAVE_PASSWORD_MSG
    "Connection failed",      // CONNECTION_FAILED
    "Failed to start hotspot",         // HOTSPOT_FAILED
    "Initializing WiFi...",   // INITIALIZING_WIFI
    "Invalid file",           // INVALID_FILE
    "Memory error",           // MEMORY_ERROR
    "Page load error",        // PAGE_LOAD_ERROR
    "Receiving...",           // RECEIVING
    "BOOTING",               // BOOTING

    // Firmware Update (from SD card)
    "Firmware Update",      // FIRMWARE_UPDATE
    "Do not power off or remove SD card during update",  // FIRMWARE_WARNING
    "Found",                // FIRMWARE_FILE_FOUND
    "Flashing...",          // FLASHING_UPDATE
    "Update complete! Restarting...", // UPDATE_COMPLETE
    "Update failed",        // UPDATE_FAILED
    "No firmware file found on SD card",  // NO_FIRMWARE_FILE
    "Validating...",        // VALIDATING_FIRMWARE
};
// clang-format on

static_assert(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]) == static_cast<int>(StrId::STR__COUNT),
              "DEFAULTS array size must match StrId::STR__COUNT");

}  // namespace i18n
