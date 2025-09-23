#include <windows.h>
#include <commdlg.h>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory> // Add this include for better memory management
#include <sstream>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <dbus/dbus.h>
#include <unistd.h>
#include <sys/wait.h>
#include <log.h>
#define kLogTag "dlghelper"


using FilePatternList = std::vector<std::string>;

// Dialog result structure to distinguish between failure and user cancellation
struct DialogResult {
    std::vector<std::string> files;
    bool dialog_shown;  // true if dialog was successfully shown (even if user cancelled)

    DialogResult() : dialog_shown(false) {}
    DialogResult(const std::vector<std::string>& f, bool shown) : files(f), dialog_shown(shown) {}

    bool empty() const { return files.empty(); }
    bool failed() const { return !dialog_shown; }
};

namespace swinx::Dialogs
{

struct FilePickerFilter
{
    std::string name;
    FilePatternList file_patterns;
};

// Helper function to detect MIME type using xdg-mime
auto detect_mime_type(std::string const &file_path) -> std::string
{
    std::string command = "xdg-mime query filetype \"" + file_path + "\" 2>/dev/null";
    auto file = popen(command.c_str(), "r");
    if (!file)
    {
        return "";
    }

    char buffer[256] = {};
    fgets(buffer, sizeof(buffer), file);
    pclose(file);

    // Remove newline
    char *end = buffer;
    while (*end && *end != '\n' && *end != '\r')
    {
        end++;
    }
    *end = 0;

    return std::string(buffer);
}

// Helper function to convert common file extensions to MIME types
auto extension_to_mime_type(std::string const &extension) -> std::string
{
    // Common extension to MIME type mappings
    if (extension == ".txt" || extension == "*.txt")
        return "text/plain";
    if (extension == ".png" || extension == "*.png")
        return "image/png";
    if (extension == ".jpg" || extension == "*.jpg" || extension == ".jpeg" || extension == "*.jpeg")
        return "image/jpeg";
    if (extension == ".gif" || extension == "*.gif")
        return "image/gif";
    if (extension == ".bmp" || extension == "*.bmp")
        return "image/bmp";
    if (extension == ".pdf" || extension == "*.pdf")
        return "application/pdf";
    if (extension == ".html" || extension == "*.html")
        return "text/html";
    if (extension == ".xml" || extension == "*.xml")
        return "application/xml";
    if (extension == ".zip" || extension == "*.zip")
        return "application/zip";
    if (extension == ".tar" || extension == "*.tar")
        return "application/x-tar";
    return "";
}

// Enhanced filter processing function
auto process_filter_patterns(FilePatternList const &patterns) -> FilePatternList
{
    FilePatternList processed_patterns;
    for (const auto &pattern : patterns)
    {
        // If it looks like a MIME type, use it as is
        if (pattern.find('/') != std::string::npos && pattern.find('.') == std::string::npos)
        {
            processed_patterns.push_back(pattern);
            continue;
        }

        // If it's a file extension or pattern, try to convert to MIME type
        std::string mime_type = extension_to_mime_type(pattern);
        if (!mime_type.empty())
        {
            processed_patterns.push_back(mime_type);
        }

        // Always add the original pattern as well
        processed_patterns.push_back(pattern);
    }
    return processed_patterns;
}





// Unified D-Bus helper class for all dialogs (file, color, font)
class DBusDialog {
private:
    DBusConnection* connection = nullptr;
    DBusError error;
    std::string m_title;
    std::string m_path;
    HWND m_parent_window = 0;

public:
    DBusDialog() {
        dbus_error_init(&error);
        connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
        if (dbus_error_is_set(&error)) {
            connection = nullptr;
        }
    }

    ~DBusDialog() {
        if (connection) {
            dbus_connection_unref(connection);
        }
        dbus_error_free(&error);
    }

    bool is_available() const {
        return connection != nullptr;
    }

    void set_parent_window(HWND parent) {
        m_parent_window = parent;
    }

    void set_path(const std::string& path) {
        m_path = path;
    }

    void set_title(const std::string& title) {
        m_title = title;
    }

    std::vector<std::string> open_file_dialog(const std::vector<FilePickerFilter>& filters,
                                            bool allow_multiple, bool directory, bool save_dialog,
                                            const std::string& title, const std::string& initial_path) {
        if (!connection) {
            // Fallback to zenity if D-Bus is not available
            return fallback_to_zenity_file(filters, allow_multiple, directory, save_dialog, title, initial_path);
        }

        // Detect desktop environment to avoid opening multiple dialogs
        std::string desktop_env = get_desktop_environment();

        // Try KDE's file dialog first if we're in KDE environment
        if (desktop_env == "KDE" || desktop_env == "UNKNOWN") {
            auto result = try_kde_dialog(filters, allow_multiple, directory, save_dialog, title, initial_path);
            if (result.dialog_shown || desktop_env == "KDE") {
                // If dialog was shown OR we're definitely in KDE, don't try other methods
                // Only fallback to zenity if dialog failed to show (not if user cancelled)
                return result.dialog_shown ? result.files : fallback_to_zenity_file(filters, allow_multiple, directory, save_dialog, title, initial_path);
            }
        }

        // Try GNOME's file dialog if we're in GNOME environment or KDE failed
        if (desktop_env == "GNOME" || desktop_env == "UNKNOWN") {
            auto result = try_gnome_dialog(filters, allow_multiple, directory, save_dialog, title, initial_path);
            if (result.dialog_shown || desktop_env == "GNOME") {
                // If dialog was shown OR we're definitely in GNOME, don't try other methods
                // Only fallback to zenity if dialog failed to show (not if user cancelled)
                return result.dialog_shown ? result.files : fallback_to_zenity_file(filters, allow_multiple, directory, save_dialog, title, initial_path);
            }
        }

        // Final fallback to zenity
        return fallback_to_zenity_file(filters, allow_multiple, directory, save_dialog, title, initial_path);
    }

    std::vector<std::string> open_file_picker(const std::vector<FilePickerFilter>& filters,
                                            bool allow_multiple, bool directory = false, bool save_dialog = false) {
        std::string title = m_title.empty() ? "Select File" : m_title;
        std::string path = m_path;
        return open_file_dialog(filters, allow_multiple, directory, save_dialog, title, path);
    }

    std::string open_directory_picker() {
        auto results = open_file_picker({}, false, true);
        return results.empty() ? "" : results[0];
    }

    std::string choose_color(COLORREF initial_color = 0) {
        if (!connection) {
            // Fallback to zenity if D-Bus is not available
            return fallback_to_zenity_color(initial_color);
        }

        // Detect desktop environment to avoid opening multiple dialogs
        std::string desktop_env = get_desktop_environment();

        // Try KDE's color dialog first if we're in KDE environment
        if (desktop_env == "KDE" || desktop_env == "UNKNOWN") {
            std::string result = try_kde_color_dialog(initial_color);
            if (!result.empty() || desktop_env == "KDE") {
                // If we got a result OR we're definitely in KDE, don't try other methods
                return result.empty() ? fallback_to_zenity_color(initial_color) : result;
            }
        }

        // Fallback to zenity for other environments
        return fallback_to_zenity_color(initial_color);
    }

    // Font dialog methods
    std::string try_kde_font_dialog(const std::string& initial_font) {
        // First check if kdialog service is available
        if (!is_kde_dialog_available()) {
            return "";
        }

        // KDE font dialog via D-Bus
        DBusMessage* msg = dbus_message_new_method_call(
            "org.kde.kdialog", "/kdialog", "org.kde.kdialog", "getFont");

        if (!msg) return "";

        // Pass initial font if provided
        const char* font_cstr = initial_font.empty() ? "" : initial_font.c_str();
        dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &font_cstr,
            DBUS_TYPE_INVALID);

        // Check if connection is still valid before sending
        if (!connection) {
            dbus_message_unref(msg);
            return "";
        }

        // Clear any previous errors
        dbus_error_free(&error);
        dbus_error_init(&error);

        // Use longer timeout for font dialogs
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, 30000, &error);
        dbus_message_unref(msg);

        if (!reply) {
            if (dbus_error_is_set(&error)) {
                dbus_error_free(&error);
            }
            return "";
        }

        if (dbus_error_is_set(&error)) {
            dbus_message_unref(reply);
            dbus_error_free(&error);
            return "";
        }

        char* result_font = nullptr;
        std::string result;

        if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &result_font, DBUS_TYPE_INVALID)) {
            if (result_font && strlen(result_font) > 0) {
                std::string font_result(result_font);
                // Check if user cancelled (kdialog might return empty string on cancel)
                if (!font_result.empty() && font_result != " ") {
                    result = font_result;
                }
            }
        }

        dbus_message_unref(reply);
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
        }

        return result;
    }

private:

    std::string get_desktop_environment() {
        // Check environment variables to determine desktop environment
        const char* desktop = getenv("XDG_CURRENT_DESKTOP");
        if (desktop) {
            std::string desktop_str(desktop);
            if (desktop_str.find("KDE") != std::string::npos) {
                return "KDE";
            }
            if (desktop_str.find("GNOME") != std::string::npos) {
                return "GNOME";
            }
        }

        // Check KDE_SESSION_VERSION
        const char* kde_session = getenv("KDE_SESSION_VERSION");
        if (kde_session) {
            return "KDE";
        }

        // Check GNOME_DESKTOP_SESSION_ID
        const char* gnome_session = getenv("GNOME_DESKTOP_SESSION_ID");
        if (gnome_session) {
            return "GNOME";
        }

        // Check DESKTOP_SESSION
        const char* session = getenv("DESKTOP_SESSION");
        if (session) {
            std::string session_str(session);
            if (session_str.find("kde") != std::string::npos ||
                session_str.find("plasma") != std::string::npos) {
                return "KDE";
            }
            if (session_str.find("gnome") != std::string::npos) {
                return "GNOME";
            }
        }

        return "UNKNOWN";
    }

    std::string get_parent_window_handle() {
        if (!m_parent_window) {
            return "";
        }

        // Convert HWND to X11 window ID string for Portal
        // In Linux, HWND is typically the X11 Window ID
        char window_handle[32];
        snprintf(window_handle, sizeof(window_handle), "x11:%lx", (unsigned long)m_parent_window);

        //SLOG_STMI() << "Parent window handle: " << window_handle;
        return std::string(window_handle);
    }

    DialogResult try_kde_dialog(const std::vector<FilePickerFilter>& filters,
                                bool allow_multiple, bool directory, bool save_dialog,
                                const std::string& title, const std::string& initial_path) {
        // First check if kdialog service is available
        if (!is_kde_dialog_available()) {
            return DialogResult({}, false);  // Dialog failed to show
        }

        // KDE file dialog via D-Bus
        DBusMessage* msg = dbus_message_new_method_call(
            "org.kde.kdialog", "/kdialog", "org.kde.kdialog",
            save_dialog ? "getSaveFileName" : (directory ? "getExistingDirectory" : "getOpenFileName"));

        if (!msg) return DialogResult({}, false);  // Dialog failed to show

        // Add arguments - KDE expects different parameter order
        const char* path_cstr = initial_path.empty() ? "" : initial_path.c_str();

        // Build KDE filter string
        std::string kde_filter = build_kde_filter_string(filters);
        const char* filter_cstr = kde_filter.c_str();

        const char* title_cstr = title.empty() ? "" : title.c_str();

        dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &path_cstr,
            DBUS_TYPE_STRING, &filter_cstr,
            DBUS_TYPE_STRING, &title_cstr,
            DBUS_TYPE_INVALID);

        // Check if connection is still valid before sending
        if (!connection) {
            dbus_message_unref(msg);
            return {};
        }

        // Set parent window for KDE dialog if available
        std::string old_windowid;
        bool windowid_set = false;
        if (m_parent_window) {
            char* existing_windowid = getenv("WINDOWID");
            if (existing_windowid) {
                old_windowid = existing_windowid;
            }

            char window_id_str[32];
            snprintf(window_id_str, sizeof(window_id_str), "%lx", (unsigned long)m_parent_window);
            setenv("WINDOWID", window_id_str, 1);
            windowid_set = true;

            SLOG_STMI() << "Set WINDOWID for KDE dialog: " << window_id_str;
        }

        // Clear any previous errors
        dbus_error_free(&error);
        dbus_error_init(&error);

        // Use longer timeout for KDE dialogs as they can be slow
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, 30000, &error);
        dbus_message_unref(msg);

        if (!reply) {
            if (dbus_error_is_set(&error)) {
                dbus_error_free(&error);
            }
            // Restore original WINDOWID if we changed it
            if (windowid_set) {
                if (!old_windowid.empty()) {
                    setenv("WINDOWID", old_windowid.c_str(), 1);
                } else {
                    unsetenv("WINDOWID");
                }
            }
            return DialogResult({}, false);  // Dialog failed to show
        }

        if (dbus_error_is_set(&error)) {
            dbus_message_unref(reply);
            dbus_error_free(&error);
            // Restore original WINDOWID if we changed it
            if (windowid_set) {
                if (!old_windowid.empty()) {
                    setenv("WINDOWID", old_windowid.c_str(), 1);
                } else {
                    unsetenv("WINDOWID");
                }
            }
            return DialogResult({}, false);  // Dialog failed to show
        }

        char* result_path = nullptr;
        std::vector<std::string> result;

        if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &result_path, DBUS_TYPE_INVALID)) {
            if (result_path && strlen(result_path) > 0) {
                // Check if user cancelled (kdialog returns empty string on cancel)
                std::string path_str(result_path);
                if (!path_str.empty() && path_str != " ") {
                    result.push_back(path_str);
                }
            }
        }

        dbus_message_unref(reply);
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
        }

        // Restore original WINDOWID if we changed it
        if (windowid_set) {
            if (!old_windowid.empty()) {
                setenv("WINDOWID", old_windowid.c_str(), 1);
            } else {
                unsetenv("WINDOWID");
            }
        }

        // Dialog was shown successfully, return result (empty if user cancelled)
        return DialogResult(result, true);
    }

    bool is_kde_dialog_available() {
        if (!connection) return false;

        // Check if kdialog service exists
        DBusMessage* msg = dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner");

        if (!msg) return false;

        const char* service_name = "org.kde.kdialog";
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &service_name, DBUS_TYPE_INVALID);

        dbus_error_free(&error);
        dbus_error_init(&error);

        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, 1000, &error);
        dbus_message_unref(msg);

        if (!reply || dbus_error_is_set(&error)) {
            if (reply) dbus_message_unref(reply);
            dbus_error_free(&error);
            return false;
        }

        dbus_bool_t has_owner = FALSE;
        if (dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &has_owner, DBUS_TYPE_INVALID)) {
            dbus_message_unref(reply);
            return has_owner == TRUE;
        }

        dbus_message_unref(reply);
        return false;
    }

    std::string try_kde_color_dialog(COLORREF initial_color) {
        // First check if kdialog service is available
        if (!is_kde_dialog_available()) {
            return "";
        }

        // KDE color dialog via D-Bus
        DBusMessage* msg = dbus_message_new_method_call(
            "org.kde.kdialog", "/kdialog", "org.kde.kdialog", "getColor");

        if (!msg) return "";

        // Convert COLORREF to HTML color format for KDE
        std::string color_str;
        if (initial_color != 0) {
            char color_buf[8];
            snprintf(color_buf, sizeof(color_buf), "#%02X%02X%02X",
                     GetRValue(initial_color),
                     GetGValue(initial_color),
                     GetBValue(initial_color));
            color_str = color_buf;
        } else {
            color_str = "#FFFFFF"; // Default to white
        }

        const char* color_cstr = color_str.c_str();
        dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &color_cstr,
            DBUS_TYPE_INVALID);

        // Check if connection is still valid before sending
        if (!connection) {
            dbus_message_unref(msg);
            return "";
        }

        // Clear any previous errors
        dbus_error_free(&error);
        dbus_error_init(&error);

        // Use longer timeout for color dialogs
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, 30000, &error);
        dbus_message_unref(msg);

        if (!reply) {
            if (dbus_error_is_set(&error)) {
                dbus_error_free(&error);
            }
            return "";
        }

        if (dbus_error_is_set(&error)) {
            dbus_message_unref(reply);
            dbus_error_free(&error);
            return "";
        }

        char* result_color = nullptr;
        std::string result;

        if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &result_color, DBUS_TYPE_INVALID)) {
            if (result_color && strlen(result_color) > 0) {
                std::string color_result(result_color);
                // Check if user cancelled (kdialog might return empty string on cancel)
                if (!color_result.empty() && color_result != " ") {
                    result = color_result;
                }
            }
        }

        dbus_message_unref(reply);
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
        }

        return result;
    }

    DialogResult try_gnome_dialog(const std::vector<FilePickerFilter>& filters,
                                  bool allow_multiple, bool directory, bool save_dialog,
                                  const std::string& title, const std::string& initial_path) {
        SLOG_STMI() << "try_gnome_dialog: save_dialog=" << save_dialog << ", directory=" << directory
                   << ", allow_multiple=" << allow_multiple << ", title=" << title.c_str();

        // Generate unique request token
        static int request_counter = 0;
        char request_token[64];
        snprintf(request_token, sizeof(request_token), "dlghelper_%d_%ld", ++request_counter, time(nullptr));

        //SLOG_STMI() << "Portal request token: " << request_token;

        // GNOME file dialog via D-Bus (org.freedesktop.portal.FileChooser)
        const char* method_name = save_dialog ? "SaveFile" : "OpenFile";
        DBusMessage* msg = dbus_message_new_method_call(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.FileChooser",
            method_name);

        if (!msg) {
            SLOG_STME() << "Failed to create D-Bus message for Portal";
            return {};
        }

        // Build message arguments
        DBusMessageIter iter, dict_iter;
        dbus_message_iter_init_append(msg, &iter);

        // Parent window handle
        std::string parent_handle_str = get_parent_window_handle();
        const char* parent_handle = parent_handle_str.c_str();
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_handle);

        // Title
        const char *title_cstr = title.c_str();
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title_cstr);

        // Options dictionary
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

        // Add handle_token for request tracking
        const char* request_token_ptr = request_token;
        add_dict_entry(&dict_iter, "handle_token", DBUS_TYPE_STRING, &request_token_ptr);

        if (allow_multiple && !save_dialog) {
            dbus_bool_t multiple_val = allow_multiple ? TRUE : FALSE;
            add_dict_entry(&dict_iter, "multiple", DBUS_TYPE_BOOLEAN, &multiple_val);
        }

        if (directory) {
            dbus_bool_t directory_val = directory ? TRUE : FALSE;
            add_dict_entry(&dict_iter, "directory", DBUS_TYPE_BOOLEAN, &directory_val);
        }

        // Add file filters if provided and not a directory dialog
        if (!filters.empty() && !directory) {
            add_portal_filters(&dict_iter, filters);
        }

        dbus_message_iter_close_container(&iter, &dict_iter);

        // Check if connection is still valid before sending
        if (!connection) {
            dbus_message_unref(msg);
            return {};
        }

        // Clear any previous errors
        dbus_error_free(&error);
        dbus_error_init(&error);

        // Portal dialogs are asynchronous, we need to handle the Request object
        return try_gnome_dialog_async(msg, request_token);
    }

    DialogResult try_gnome_dialog_async(DBusMessage* msg, const char* request_token) {
        // Send the request (this returns immediately with a request path)
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, msg, 5000, &error);
        dbus_message_unref(msg);

        if (!reply || dbus_error_is_set(&error)) {
            if (reply) dbus_message_unref(reply);
            dbus_error_free(&error);
            SLOG_STME() << "Failed to send Portal request";
            return DialogResult({}, false);  // Dialog failed to show
        }

        // Get the request object path from the reply
        char* returned_request_path = nullptr;
        if (!dbus_message_get_args(reply, &error, DBUS_TYPE_OBJECT_PATH, &returned_request_path, DBUS_TYPE_INVALID)) {
            dbus_message_unref(reply);
            dbus_error_free(&error);
            SLOG_STME() << "Failed to get request path from Portal reply";
            return DialogResult({}, false);  // Dialog failed to show
        }

        //SLOG_STMI() << "Got request path: " << returned_request_path;
        dbus_message_unref(reply);

        // Now wait for the Response signal on the request object
        return wait_for_portal_response(returned_request_path);
    }

    DialogResult wait_for_portal_response(const char* request_path) {
        //SLOG_STMI() << "Waiting for Portal response on: " << request_path;

        // Add match rule for the Response signal
        char match_rule[512];
        snprintf(match_rule, sizeof(match_rule),
                "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'",
                request_path);

        dbus_error_free(&error);
        dbus_error_init(&error);

        dbus_bus_add_match(connection, match_rule, &error);
        if (dbus_error_is_set(&error)) {
            SLOG_STME() << "Failed to add match rule: " << error.message;
            dbus_error_free(&error);
            return DialogResult({}, false);  // Dialog failed to show
        }

        //SLOG_STMI() << "Added match rule: " << match_rule;

        // Wait for the Response signal with timeout
        const int timeout_ms = 300000; // 5 minutes
        const int poll_interval_ms = 100; // 100ms
        int elapsed_ms = 0;

        std::vector<std::string> result;
        bool response_received = false;

        while (elapsed_ms < timeout_ms && !response_received) {
            // Check for incoming messages
            dbus_connection_read_write_dispatch(connection, poll_interval_ms);

            // Process any pending messages
            DBusMessage* msg;
            while ((msg = dbus_connection_pop_message(connection)) != nullptr) {
                if (dbus_message_is_signal(msg, "org.freedesktop.portal.Request", "Response")) {
                    const char* msg_path = dbus_message_get_path(msg);
                    if (msg_path && strcmp(msg_path, request_path) == 0) {
                        SLOG_STMI() << "Received Response signal from: " << msg_path;
                        // Parse the response
                        result = parse_portal_response(msg);
                        response_received = true;

                        dbus_message_unref(msg);
                        break;
                    }
                }
                dbus_message_unref(msg);
            }

            if (!response_received) {
                elapsed_ms += poll_interval_ms;
            }
        }

        // Remove the match rule
        dbus_bus_remove_match(connection, match_rule, nullptr);

        if (!response_received) {
            SLOG_STME() << "Timeout waiting for Portal response";
            return DialogResult({}, false);  // Dialog failed (timeout)
        } else {
            SLOG_STMI() << "Portal response received, result.size()=" << result.size();
            return DialogResult(result, true);  // Dialog was shown successfully
        }
    }

    std::vector<std::string> fallback_to_zenity_file(const std::vector<FilePickerFilter>& filters,
                                                   bool allow_multiple, bool directory, bool save_dialog,
                                                   const std::string& title, const std::string& initial_path) {
        std::vector<std::string> files{};
                                                
        SLOG_STMI() << "fallback_to_zenity_file: save_dialog=" << save_dialog << ", directory=" << directory
                   << ", allow_multiple=" << allow_multiple << ", title=" << title.c_str();
        // Build zenity command
        std::string command = "zenity --file-selection";

        // Add dialog type
        if (save_dialog) {
            command += " --save --confirm-overwrite";
        }

        if (directory) {
            command += " --directory";
        }

        if (allow_multiple) {
            command += " --multiple";
        }

        // Add title if specified
        if (!title.empty()) {
            command += " --title=\"" + title + "\"";
        }

        // Add initial path if specified
        if (!initial_path.empty()) {
            command += " --filename=\"" + initial_path + "\"";
        }

        // Add parent window if specified
        if (m_parent_window) {
            command += " --modal";
        }

        // Process filters
        if (!filters.empty() && !directory) {
            for (const auto& filter : filters) {
                if (!filter.file_patterns.empty()) {
                    command += " --file-filter=\"";
                    command += filter.name + "|";

                    for (size_t i = 0; i < filter.file_patterns.size(); ++i) {
                        if (i > 0) command += " ";
                        command += filter.file_patterns[i];
                    }
                    command += "\"";
                }
            }
        }

        // Execute command and get result
        std::string result = execute_zenity_command(command);
        if (!result.empty()) {
            // Parse output - zenity uses | or \n as separators
            // Remove trailing newline
            if (result.back() == '\n') {
                result.pop_back();
            }

            if (allow_multiple) {
                // Multiple files are separated by |
                size_t start = 0;
                size_t pos = 0;
                while (pos <= result.length()) {
                    if (pos == result.length() || result[pos] == '|') {
                        std::string file = result.substr(start, pos - start);
                        if (!file.empty()) {
                            files.push_back(file);
                        }
                        start = pos + 1;
                    }
                    pos++;
                }
            } else {
                // Single file
                files.push_back(result);
            }
        }

        return files;
    }

    std::string fallback_to_zenity_color(COLORREF initial_color) {
        // Build zenity color selection command
        std::string command = "zenity --color-selection";

        // Add title if specified
        if (!m_title.empty()) {
            command += " --title=\"" + m_title + "\"";
        }

        // Add initial color if specified
        if (initial_color != 0) {
            // Convert COLORREF to HTML color format (#RRGGBB)
            char color_str[8];
            snprintf(color_str, sizeof(color_str), "#%02X%02X%02X",
                     GetRValue(initial_color),
                     GetGValue(initial_color),
                     GetBValue(initial_color));
            command += " --color=\"" + std::string(color_str) + "\"";
        }

        // Add parent window if specified
        if (m_parent_window) {
            command += " --modal";
        }

        // Execute command and return result
        return execute_zenity_command(command);
    }

    std::string execute_zenity_command(const std::string& command) {
        std::string result = "";

        // Execute command
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return result;
        }

        char buffer[4096] = {};
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            int status = pclose(pipe);
            if (WEXITSTATUS(status) == 0) {
                result = std::string(buffer);
                // Remove trailing newline
                if (!result.empty() && result.back() == '\n') {
                    result.pop_back();
                }
            } else {
                pclose(pipe);
            }
        } else {
            // fgets returned nullptr - no output from zenity
            int status = pclose(pipe);
            // For zenity, exit status 0 means success (but possibly no output)
            // Exit status 1 means user cancelled the dialog
            // Only return result if the operation was successful
            if (WEXITSTATUS(status) == 0) {
                // Success but no output
                return result;
            }
            // If status is 1, it's user cancellation which we treat as normal
        }

        return result;
    }

    void add_dict_entry(DBusMessageIter* dict_iter, const char* key, int type, const void* value) {
        DBusMessageIter entry_iter, variant_iter;
        dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);

        const char* type_sig;
        switch (type) {
            case DBUS_TYPE_BOOLEAN:
                type_sig = "b";
                break;
            case DBUS_TYPE_STRING:
                type_sig = "s";
                break;
            case DBUS_TYPE_INT32:
                type_sig = "i";
                break;
            default:
                type_sig = "s";
                break;
        }

        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, type_sig, &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, type, value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(dict_iter, &entry_iter);
    }

    void add_portal_filters(DBusMessageIter* dict_iter, const std::vector<FilePickerFilter>& filters) {
        // Portal filters format: "filters" -> array of (string name, array of (uint32 type, string pattern))
        // type: 0 = glob pattern, 1 = mime type

        DBusMessageIter entry_iter, variant_iter, filters_array_iter;

        // Create dictionary entry for "filters"
        dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);

        const char* filters_key = "filters";
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &filters_key);

        // Create variant containing array of filters
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "a(sa(us))", &variant_iter);
        dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY, "(sa(us))", &filters_array_iter);

        // Add each filter
        for (const auto& filter : filters) {
            DBusMessageIter filter_struct_iter, patterns_array_iter;

            // Create filter struct (string name, array of patterns)
            dbus_message_iter_open_container(&filters_array_iter, DBUS_TYPE_STRUCT, nullptr, &filter_struct_iter);

            // Add filter name
            const char* filter_name = filter.name.c_str();
            dbus_message_iter_append_basic(&filter_struct_iter, DBUS_TYPE_STRING, &filter_name);

            // Add patterns array
            dbus_message_iter_open_container(&filter_struct_iter, DBUS_TYPE_ARRAY, "(us)", &patterns_array_iter);

            // Add each pattern
            for (const auto& pattern : filter.file_patterns) {
                DBusMessageIter pattern_struct_iter;
                dbus_message_iter_open_container(&patterns_array_iter, DBUS_TYPE_STRUCT, nullptr, &pattern_struct_iter);

                // Determine pattern type (0 = glob, 1 = mime)
                uint32_t pattern_type = 0; // Default to glob pattern
                if (pattern.find('/') != std::string::npos && pattern.find('.') == std::string::npos) {
                    pattern_type = 1; // MIME type
                }

                dbus_message_iter_append_basic(&pattern_struct_iter, DBUS_TYPE_UINT32, &pattern_type);

                const char* pattern_str = pattern.c_str();
                dbus_message_iter_append_basic(&pattern_struct_iter, DBUS_TYPE_STRING, &pattern_str);

                dbus_message_iter_close_container(&patterns_array_iter, &pattern_struct_iter);
            }

            dbus_message_iter_close_container(&filter_struct_iter, &patterns_array_iter);
            dbus_message_iter_close_container(&filters_array_iter, &filter_struct_iter);
        }

        dbus_message_iter_close_container(&variant_iter, &filters_array_iter);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(dict_iter, &entry_iter);
    }

    std::string build_kde_filter_string(const std::vector<FilePickerFilter>& filters) {
        // KDE filter format: "Description (*.ext1 *.ext2)|*.ext1 *.ext2|Description2 (*.ext3)|*.ext3"
        // Each filter is separated by |, and consists of "Description|patterns"

        if (filters.empty()) {
            return "All Files (*)|*";
        }

        std::string kde_filter;
        bool first = true;

        for (const auto& filter : filters) {
            if (!first) {
                kde_filter += "|";
            }
            first = false;

            // Add description with patterns in parentheses
            kde_filter += filter.name;
            if (!filter.file_patterns.empty()) {
                kde_filter += " (";
                bool first_pattern = true;
                for (const auto& pattern : filter.file_patterns) {
                    if (!first_pattern) {
                        kde_filter += " ";
                    }
                    first_pattern = false;
                    kde_filter += pattern;
                }
                kde_filter += ")";
            }

            // Add separator
            kde_filter += "|";

            // Add patterns
            if (!filter.file_patterns.empty()) {
                bool first_pattern = true;
                for (const auto& pattern : filter.file_patterns) {
                    if (!first_pattern) {
                        kde_filter += " ";
                    }
                    first_pattern = false;
                    kde_filter += pattern;
                }
            } else {
                kde_filter += "*";
            }
        }

        // Add "All Files" option at the end
        if (!kde_filter.empty()) {
            kde_filter += "|All Files (*)|*";
        }

        return kde_filter;
    }

    std::vector<std::string> parse_portal_response(DBusMessage* reply) {
        std::vector<std::string> result;

        if (!reply) return result;

        // Portal response format: (uint32 response, dict results)
        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply, &iter)) {
            return result;
        }

        // First argument: response code (uint32)
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
            return result;
        }

        uint32_t response_code;
        dbus_message_iter_get_basic(&iter, &response_code);

        // 0 = success, 1 = user cancelled, 2 = other error
        if (response_code != 0) {
            return result; // User cancelled or error occurred
        }

        // Move to next argument: results dictionary
        if (!dbus_message_iter_next(&iter)) {
            return result;
        }

        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            return result;
        }

        // Parse the results dictionary
        DBusMessageIter dict_iter;
        dbus_message_iter_recurse(&iter, &dict_iter);

        // Look for "uris" key in the dictionary
        while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry_iter;
            dbus_message_iter_recurse(&dict_iter, &entry_iter);

            // Get the key
            if (dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_STRING) {
                dbus_message_iter_next(&dict_iter);
                continue;
            }

            char* key;
            dbus_message_iter_get_basic(&entry_iter, &key);

            if (strcmp(key, "uris") == 0) {
                // Move to the value (variant)
                if (!dbus_message_iter_next(&entry_iter)) {
                    break;
                }

                if (dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_VARIANT) {
                    break;
                }

                DBusMessageIter variant_iter;
                dbus_message_iter_recurse(&entry_iter, &variant_iter);

                // The variant should contain an array of strings
                if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY) {
                    break;
                }

                DBusMessageIter array_iter;
                dbus_message_iter_recurse(&variant_iter, &array_iter);

                // Parse each URI in the array
                while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
                    char* uri;
                    dbus_message_iter_get_basic(&array_iter, &uri);

                    if (uri && strlen(uri) > 0) {
                        // Convert file:// URI to local path
                        std::string file_path = uri_to_local_path(uri);
                        if (!file_path.empty()) {
                            result.push_back(file_path);
                        }
                    }

                    if (!dbus_message_iter_next(&array_iter)) {
                        break;
                    }
                }

                break; // Found "uris", no need to continue
            }

            dbus_message_iter_next(&dict_iter);
        }

        return result;
    }

    std::string uri_to_local_path(const std::string& uri) {
        // Convert file:// URI to local file path
        if (uri.substr(0, 7) == "file://") {
            std::string path = uri.substr(7);

            // URL decode the path
            std::string decoded_path;
            for (size_t i = 0; i < path.length(); ++i) {
                if (path[i] == '%' && i + 2 < path.length()) {
                    // Decode %XX sequences
                    char hex_str[3] = {path[i+1], path[i+2], '\0'};
                    char* end_ptr;
                    long hex_val = strtol(hex_str, &end_ptr, 16);
                    if (end_ptr == hex_str + 2) {
                        decoded_path += static_cast<char>(hex_val);
                        i += 2;
                    } else {
                        decoded_path += path[i];
                    }
                } else {
                    decoded_path += path[i];
                }
            }

            return decoded_path;
        }

        // If not a file:// URI, return as-is (might be a local path already)
        return uri;
    }


};

} // namespace swinx::Dialogs

BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out)
{
    try
    {
        swinx::Dialogs::DBusDialog dialog;

        // Set the parent window if specified
        if (parent) {
            dialog.set_parent_window(parent);
        }

        // Use the first color in the array as the initial color
        COLORREF initial_color = 0;
        if (initClr) {
            initial_color = initClr[0];
        }

        // Set a default title
        dialog.set_title("Choose Color");

        // Call the color selection dialog
        std::string color_result = dialog.choose_color(initial_color);

        // If user didn't cancel, convert the result
        if (!color_result.empty()) {
            // Parse the HTML color format (#RRGGBB) returned by zenity or KDE
            unsigned int r, g, b;
            if(color_result.substr(0,4)=="rgb("){
                if (sscanf(color_result.c_str(), "rgb(%d, %d, %d)", &r, &g, &b) == 3) {
                    *out = RGB(r, g, b);
                    return TRUE;
                }
            }else if(color_result.substr(0,5)=="rgba("){
                float a=1.0f;
                if (sscanf(color_result.c_str(), "rgba(%d, %d, %d, %f)", &r, &g, &b, &a) == 4) {
                    *out = RGBA(r, g, b, (int)floor(a*255+0.5f));
                    return TRUE;
                }
            }else if(color_result.substr(0,1)=="#"){
                // Parse HTML hex color format (#RRGGBB)
                if (sscanf(color_result.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
                    *out = RGB(r, g, b);
                    return TRUE;
                }
            }
        }

        // User cancelled or error occurred
        return FALSE;
    }
    catch (...)
    {
        return FALSE; // Error occurred
    }
}

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode)
{
    if (!p || !p->lpstrFile || p->nMaxFile == 0)
    {
        return FALSE;
    }

    // Clear the output buffer
    memset(p->lpstrFile, 0, p->nMaxFile);

    try
    {
        swinx::Dialogs::DBusDialog dialog;

        // Set the parent window if specified
        if (p->hwndOwner)
        {
            dialog.set_parent_window(p->hwndOwner);
        }
        if (p->lpstrTitle)
        {
            dialog.set_title(p->lpstrTitle);
        }
        bool is_directory = (mode == FOLDER);
        bool is_save = (mode == SAVE);
        bool allow_multiple = (p->Flags & OFN_ALLOWMULTISELECT) != 0 && !is_save && !is_directory;

        // Parse filters from lpstrFilter
        std::vector<swinx::Dialogs::FilePickerFilter> filters;
        if (p->lpstrFilter && !is_directory)
        {
            const char *filter_ptr = p->lpstrFilter;
            while (*filter_ptr)
            {
                swinx::Dialogs::FilePickerFilter filter;
                filter.name = std::string(filter_ptr);
                filter_ptr += strlen(filter_ptr) + 1;

                if (*filter_ptr)
                {
                    std::string patterns(filter_ptr);
                    // Parse multiple patterns separated by semicolons
                    size_t start = 0;
                    size_t semicolon_pos = patterns.find(';');
                    while (start < patterns.length())
                    {
                        if (semicolon_pos == std::string::npos)
                        {
                            semicolon_pos = patterns.length();
                        }

                        std::string pattern = patterns.substr(start, semicolon_pos - start);
                        // Remove any whitespace
                        pattern.erase(0, pattern.find_first_not_of(" \t"));
                        pattern.erase(pattern.find_last_not_of(" \t") + 1);

                        if (!pattern.empty())
                        {
                            filter.file_patterns.push_back(pattern);
                        }

                        start = semicolon_pos + 1;
                        if (start >= patterns.length())
                        {
                            break;
                        }
                        semicolon_pos = patterns.find(';', start);
                    }

                    filter_ptr += strlen(filter_ptr) + 1;
                }

                if (!filter.name.empty() && !filter.file_patterns.empty())
                {
                    filters.push_back(filter);
                }

                if (!*filter_ptr)
                    break;
            }
        }

        // Set initial directory if provided
        if (p->lpstrInitialDir)
        {
            dialog.set_path(std::string(p->lpstrInitialDir));
        }

        // Call the appropriate dialog method
        std::vector<std::string> results;
        if (is_directory)
        {
            std::string dir = dialog.open_directory_picker();
            if (!dir.empty())
            {
                results.push_back(dir);
            }
        }
        else
        {
            results = dialog.open_file_picker(filters, allow_multiple, false, is_save);
        }

        if (results.empty())
        {
            return FALSE; // User cancelled
        }

        if (is_directory || is_save || !allow_multiple)
        {
            // Single file selection or save dialog
            const std::string &file = results[0];
            if (file.length() >= p->nMaxFile)
            {
                return FALSE; // Buffer too small
            }
            strcpy(p->lpstrFile, file.c_str());

            // Set offset and extension info
            p->nFileOffset = 0;
            p->nFileExtension = 0;
            for (size_t i = 0; i < file.length(); i++)
            {
                if (file[i] == '/')
                {
                    p->nFileOffset = static_cast<WORD>(i + 1);
                }
                else if (file[i] == '.' && p->nFileExtension == 0)
                {
                    p->nFileExtension = static_cast<WORD>(i + 1);
                }
            }
        }
        else
        {
            // Multiple file selection
            // For multiple selections, the buffer should contain:
            // 1. Directory path followed by '\0'
            // 2. Each filename followed by '\0'
            // 3. Final '\0' to terminate the list

            if (results.size() == 1)
            {
                // If only one file was selected, treat as single file selection
                const std::string &file = results[0];
                if (file.length() >= p->nMaxFile)
                {
                    return FALSE; // Buffer too small
                }
                strcpy(p->lpstrFile, file.c_str());

                // Set offset and extension info
                p->nFileOffset = 0;
                p->nFileExtension = 0;
                for (size_t i = 0; i < file.length(); i++)
                {
                    if (file[i] == '/')
                    {
                        p->nFileOffset = static_cast<WORD>(i + 1);
                    }
                    else if (file[i] == '.' && p->nFileExtension == 0)
                    {
                        p->nFileExtension = static_cast<WORD>(i + 1);
                    }
                }
            }
            else
            {
                // Multiple files selected
                // Find common directory
                std::string directory = results[0];
                size_t lastSlash = directory.find_last_of('/');
                if (lastSlash != std::string::npos)
                {
                    directory = directory.substr(0, lastSlash + 1);
                }
                else
                {
                    directory = "";
                }

                // Check if buffer is large enough
                size_t needed = directory.length() + 1; // +1 for null terminator
                for (const auto &file : results)
                {
                    size_t fileNameStart = file.find_last_of('/') + 1;
                    if (fileNameStart == std::string::npos)
                        fileNameStart = 0;
                    std::string fileName = file.substr(fileNameStart);
                    needed += fileName.length() + 1; // +1 for null terminator
                }
                needed += 1; // Final null terminator

                if (needed > p->nMaxFile)
                {
                    return FALSE; // Buffer too small
                }

                // Fill the buffer
                char *buffer = p->lpstrFile;
                size_t pos = 0;

                // Copy directory
                strcpy(buffer + pos, directory.c_str());
                pos += directory.length() + 1;

                // Copy filenames
                for (const auto &file : results)
                {
                    size_t fileNameStart = file.find_last_of('/') + 1;
                    if (fileNameStart == std::string::npos)
                        fileNameStart = 0;
                    std::string fileName = file.substr(fileNameStart);
                    strcpy(buffer + pos, fileName.c_str());
                    pos += fileName.length() + 1;
                }

                // Final null terminator
                buffer[pos] = '\0';

                // Set offset (points to first filename)
                p->nFileOffset = static_cast<WORD>(directory.length() + 1);
                p->nFileExtension = 0; // Not applicable for multiple selection
            }
        }

        return TRUE;
    }
    catch (...)
    {
        return FALSE; // Error occurred
    }
}





// Helper function to convert font string to LOGFONT
bool parse_font_string_to_logfont(const std::string& font_str, LOGFONTA* logfont) {
    if (font_str.empty() || !logfont) return false;

    // Initialize LOGFONT with defaults
    memset(logfont, 0, sizeof(LOGFONTA));
    logfont->lfHeight = -12; // Default size
    logfont->lfWeight = FW_NORMAL;
    logfont->lfCharSet = DEFAULT_CHARSET;
    logfont->lfOutPrecision = OUT_DEFAULT_PRECIS;
    logfont->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    logfont->lfQuality = DEFAULT_QUALITY;
    logfont->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    // Parse font string format: "Family Name, Size, Style"
    // Example: "Arial, 12, Bold Italic" or "Arial 12 Bold Italic"

    std::string work_str = font_str;
    std::vector<std::string> parts;

    // Split by comma first, then by space if no comma
    size_t pos = 0;
    std::string delimiter = ",";
    bool has_comma = work_str.find(',') != std::string::npos;

    if (!has_comma) {
        delimiter = " ";
    }

    while ((pos = work_str.find(delimiter)) != std::string::npos) {
        std::string part = work_str.substr(0, pos);
        // Trim whitespace
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) {
            parts.push_back(part);
        }
        work_str.erase(0, pos + delimiter.length());
    }

    // Add the last part
    work_str.erase(0, work_str.find_first_not_of(" \t"));
    work_str.erase(work_str.find_last_not_of(" \t") + 1);
    if (!work_str.empty()) {
        parts.push_back(work_str);
    }

    if (parts.empty()) return false;

    // First part is always the font family name
    strncpy(logfont->lfFaceName, parts[0].c_str(), LF_FACESIZE - 1);
    logfont->lfFaceName[LF_FACESIZE - 1] = '\0';

    // Parse remaining parts for size and style
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string& part = parts[i];

        // Check if it's a number (font size)
        if (std::isdigit(part[0])) {
            int size = std::atoi(part.c_str());
            if (size > 0) {
                // Convert point size to logical units (negative for character height)
                logfont->lfHeight = -size;
            }
        }
        // Check for style keywords
        else if (part == "Bold" || part == "bold") {
            logfont->lfWeight = FW_BOLD;
        }
        else if (part == "Italic" || part == "italic") {
            logfont->lfItalic = TRUE;
        }
        else if (part == "Underline" || part == "underline") {
            logfont->lfUnderline = TRUE;
        }
        else if (part == "Strikeout" || part == "strikeout") {
            logfont->lfStrikeOut = TRUE;
        }
    }

    return true;
}

// Helper function to convert LOGFONT to font string
std::string logfont_to_font_string(const LOGFONTA* logfont) {
    if (!logfont) return "";

    std::string result = logfont->lfFaceName;

    // Add size (convert from logical units to points)
    int size = abs(logfont->lfHeight);
    if (size > 0) {
        result += ", " + std::to_string(size);
    }

    // Add style attributes
    std::vector<std::string> styles;
    if (logfont->lfWeight >= FW_BOLD) {
        styles.push_back("Bold");
    }
    if (logfont->lfItalic) {
        styles.push_back("Italic");
    }
    if (logfont->lfUnderline) {
        styles.push_back("Underline");
    }
    if (logfont->lfStrikeOut) {
        styles.push_back("Strikeout");
    }

    for (const auto& style : styles) {
        result += ", " + style;
    }

    return result;
}

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA p) {
    if (!p || !p->lpLogFont) {
        return FALSE;
    }

    try {
        swinx::Dialogs::DBusDialog font_dialog;
        std::string result_font;

        // Convert current LOGFONT to font string for initial value
        std::string initial_font = logfont_to_font_string(p->lpLogFont);

        // Try KDE dialog first
        if (font_dialog.is_available()) {
            result_font = font_dialog.try_kde_font_dialog(initial_font);
        }

        // If we got a result, parse it back to LOGFONT
        if (!result_font.empty()) {
            LOGFONTA new_logfont;
            if (parse_font_string_to_logfont(result_font, &new_logfont)) {
                *p->lpLogFont = new_logfont;

                // Set point size if requested
                if (p->Flags & CF_INITTOLOGFONTSTRUCT) {
                    // Convert logical units to points (approximate)
                    p->iPointSize = abs(new_logfont.lfHeight) * 10;
                }

                return TRUE;
            }
        }

        return FALSE; // User cancelled or error occurred
    }
    catch (...) {
        return FALSE;
    }
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW p) {
    if (!p || !p->lpLogFont) {
        return FALSE;
    }

    // Convert CHOOSEFONTW to CHOOSEFONTA
    CHOOSEFONTA chooseA;
    LOGFONTA logfontA;

    // Copy structure fields
    chooseA.lStructSize = sizeof(CHOOSEFONTA);
    chooseA.hwndOwner = p->hwndOwner;
    chooseA.hDC = p->hDC;
    chooseA.lpLogFont = &logfontA;
    chooseA.iPointSize = p->iPointSize;
    chooseA.Flags = p->Flags;
    chooseA.rgbColors = p->rgbColors;
    chooseA.lCustData = p->lCustData;
    chooseA.lpfnHook = p->lpfnHook;
    chooseA.lpTemplateName = nullptr; // Convert if needed
    chooseA.hInstance = p->hInstance;
    chooseA.lpszStyle = nullptr; // Convert if needed
    chooseA.nFontType = p->nFontType;
    chooseA.nSizeMin = p->nSizeMin;
    chooseA.nSizeMax = p->nSizeMax;

    // Convert LOGFONTW to LOGFONTA
    logfontA.lfHeight = p->lpLogFont->lfHeight;
    logfontA.lfWidth = p->lpLogFont->lfWidth;
    logfontA.lfEscapement = p->lpLogFont->lfEscapement;
    logfontA.lfOrientation = p->lpLogFont->lfOrientation;
    logfontA.lfWeight = p->lpLogFont->lfWeight;
    logfontA.lfItalic = p->lpLogFont->lfItalic;
    logfontA.lfUnderline = p->lpLogFont->lfUnderline;
    logfontA.lfStrikeOut = p->lpLogFont->lfStrikeOut;
    logfontA.lfCharSet = p->lpLogFont->lfCharSet;
    logfontA.lfOutPrecision = p->lpLogFont->lfOutPrecision;
    logfontA.lfClipPrecision = p->lpLogFont->lfClipPrecision;
    logfontA.lfQuality = p->lpLogFont->lfQuality;
    logfontA.lfPitchAndFamily = p->lpLogFont->lfPitchAndFamily;

    // Convert wide char face name to multibyte
    WideCharToMultiByte(CP_UTF8, 0, p->lpLogFont->lfFaceName, -1,
                       logfontA.lfFaceName, LF_FACESIZE, nullptr, nullptr);

    // Call the ANSI version
    BOOL result = ChooseFontA(&chooseA);

    if (result) {
        // Convert results back to wide char
        p->lpLogFont->lfHeight = logfontA.lfHeight;
        p->lpLogFont->lfWidth = logfontA.lfWidth;
        p->lpLogFont->lfEscapement = logfontA.lfEscapement;
        p->lpLogFont->lfOrientation = logfontA.lfOrientation;
        p->lpLogFont->lfWeight = logfontA.lfWeight;
        p->lpLogFont->lfItalic = logfontA.lfItalic;
        p->lpLogFont->lfUnderline = logfontA.lfUnderline;
        p->lpLogFont->lfStrikeOut = logfontA.lfStrikeOut;
        p->lpLogFont->lfCharSet = logfontA.lfCharSet;
        p->lpLogFont->lfOutPrecision = logfontA.lfOutPrecision;
        p->lpLogFont->lfClipPrecision = logfontA.lfClipPrecision;
        p->lpLogFont->lfQuality = logfontA.lfQuality;
        p->lpLogFont->lfPitchAndFamily = logfontA.lfPitchAndFamily;

        // Convert multibyte face name back to wide char
        MultiByteToWideChar(CP_UTF8, 0, logfontA.lfFaceName, -1,
                           p->lpLogFont->lfFaceName, LF_FACESIZE);

        p->iPointSize = chooseA.iPointSize;
        p->rgbColors = chooseA.rgbColors;
    }

    return result;
}