#include <windows.h>
#include <commdlg.h>

#include <dbus-cxx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>     // Add this include for better memory management

#define LOG_DEBUGF(...) 
#define LOG_WARNF(...)
#define LOG_ERRORF(...)
#define LOG_FATALF(...)



template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename... Args>
Ref<T> make_ref(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using b32 = uint32_t;
using usz = size_t;

using FilePatternList = std::vector<std::string>;



namespace Fussion::Dialogs {
    
    struct FilePickerFilter {
        std::string name;
        FilePatternList file_patterns;
    };

    // Helper function to detect MIME type using xdg-mime
    auto detect_mime_type(std::string const& file_path) -> std::string
    {
        std::string command = "xdg-mime query filetype \"" + file_path + "\" 2>/dev/null";
        auto file = popen(command.c_str(), "r");
        if (!file) {
            return "";
        }

        char buffer[256] = {};
        fgets(buffer, sizeof(buffer), file);
        pclose(file);

        // Remove newline
        char* end = buffer;
        while (*end && *end != '\n' && *end != '\r') {
            end++;
        }
        *end = 0;

        return std::string(buffer);
    }
    
    // Helper function to convert common file extensions to MIME types
    auto extension_to_mime_type(std::string const& extension) -> std::string
    {
        // Common extension to MIME type mappings
        if (extension == ".txt" || extension == "*.txt") return "text/plain";
        if (extension == ".png" || extension == "*.png") return "image/png";
        if (extension == ".jpg" || extension == "*.jpg" || 
            extension == ".jpeg" || extension == "*.jpeg") return "image/jpeg";
        if (extension == ".gif" || extension == "*.gif") return "image/gif";
        if (extension == ".bmp" || extension == "*.bmp") return "image/bmp";
        if (extension == ".pdf" || extension == "*.pdf") return "application/pdf";
        if (extension == ".html" || extension == "*.html") return "text/html";
        if (extension == ".xml" || extension == "*.xml") return "application/xml";
        if (extension == ".zip" || extension == "*.zip") return "application/zip";
        if (extension == ".tar" || extension == "*.tar") return "application/x-tar";
        return "";
    }
    
    // Enhanced filter processing function
    auto process_filter_patterns(FilePatternList const& patterns) -> FilePatternList
    {
        FilePatternList processed_patterns;
        for (const auto& pattern : patterns) {
            // If it looks like a MIME type, use it as is
            if (pattern.find('/') != std::string::npos && pattern.find('.') == std::string::npos) {
                processed_patterns.push_back(pattern);
                continue;
            }
            
            // If it's a file extension or pattern, try to convert to MIME type
            std::string mime_type = extension_to_mime_type(pattern);
            if (!mime_type.empty()) {
                processed_patterns.push_back(mime_type);
            }
            
            // Always add the original pattern as well
            processed_patterns.push_back(pattern);
        }
        return processed_patterns;
    }

    auto ShellExecute(std::string const& command) -> std::tuple<int, std::vector<std::string>>
    {
        auto file = popen(command.c_str(), "r");

        char buffer[1024] = {};
        fgets(buffer, sizeof(buffer), file);

        auto ret = pclose(file);

        // Remove newline
        char* end = buffer;
        while (*end != '\n') {
            end++;
        }
        *end = 0;

        std::string s { buffer };
        std::vector<std::string> strings;

        size_t pos = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == ' ') {
                strings.push_back(s.substr(pos, i - pos));
                pos = i + 1;
            }
        }

        return { WEXITSTATUS(ret), strings };
    }

    using OpenFileFn = DBus::Path(std::string, std::string, std::map<std::string, DBus::Variant>);
    using SaveFileFn = OpenFileFn;
    using OpenFileResponseFn = void(u32 response, std::map<std::string, DBus::Variant> data);

    class LinuxDialog {
    public:
        explicit LinuxDialog()
        {
            LOG_DEBUGF("Initializing Linux Dialog");
            DBus::set_logging_function([](
                                           char const* logger_name,
                                           SL_LogLocation const* location,
                                           SL_LogLevel const level,
                                           char const* log_string
                                       ) {
                (void)location;
                switch (level) {
                case SL_WARN:
                    LOG_WARNF("DBUS [{}]: {}", logger_name, log_string);
                    break;
                case SL_ERROR:
                    LOG_ERRORF("DBUS [{}]: {}", logger_name, log_string);
                    break;
                case SL_FATAL:
                    LOG_FATALF("DBUS [{}]: {}", logger_name, log_string);
                    break;
                default:
                    break;
                }
            });

            m_dispatcher = DBus::StandaloneDispatcher::create();
            m_connection = m_dispatcher->create_connection(DBus::BusType::SESSION);
            m_desktop_proxy = m_connection->create_object_proxy("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", DBus::ThreadForCalling::CurrentThread);
            m_open_file_fn = m_desktop_proxy->create_method<OpenFileFn>("org.freedesktop.portal.FileChooser", "OpenFile");
            m_save_file_fn = m_desktop_proxy->create_method<SaveFileFn>("org.freedesktop.portal.FileChooser", "SaveFile");
        }

        virtual ~LinuxDialog() = default;
        
        void set_parent_window(HWND parent)
        {
            m_parent_window = parent;
        }

        auto open_file_picker(std::vector<FilePickerFilter> const& filters, bool allow_multiple, bool directory = false, bool save_dialog = false) -> std::vector<std::string>
        {
            std::vector<std::string> files {};

            std::map<std::string, DBus::Variant> options {};
            if (directory) {
                allow_multiple = false;
            }
            options["multiple"] = allow_multiple;
            options["directory"] = directory;

            // Add parent window if specified
            if (m_parent_window) {
                // Convert HWND to string format expected by XDG Desktop Portal
                // Format is "x11:<window_id>" for X11 windows
                char parent_str[32];
                snprintf(parent_str, sizeof(parent_str), "x11:%x", (unsigned int)(uintptr_t)m_parent_window);
                options["parent"] = std::string(parent_str);
            }

            // Process filters for the file dialog
            if (!filters.empty() && !directory) {
                // Format expected by org.freedesktop.portal.FileChooser:
                // a(sa(us))
                // array of tuples containing:
                //   - string: name of the filter
                //   - array of tuples containing:
                //     - uint: type of filter (0 = pattern, 1 = mimetype)
                //     - string: filter string (e.g. "*.png" or "image/png")
                std::vector<std::tuple<std::string, std::vector<std::tuple<u32, std::string>>>> portal_filters {};
                
                for (auto const& filter : filters) {
                    std::vector<std::tuple<u32, std::string>> pattern_list {};
                    
                    // Process each file pattern in the filter
                    auto processed_patterns = process_filter_patterns(filter.file_patterns);
                    for (auto const& pattern : processed_patterns) {
                        // Determine if it's a MIME type or file pattern
                        if (pattern.find('/') != std::string::npos && 
                            pattern.find('.') == std::string::npos) {
                            // Likely a MIME type (contains '/' but no '.')
                            pattern_list.emplace_back(1, pattern); // Type 1 = MIME type
                        } else {
                            // File pattern (e.g., "*.txt", "*.png")
                            pattern_list.emplace_back(0, pattern); // Type 0 = Pattern
                        }
                    }
                    
                    // Only add filter if it has patterns
                    if (!pattern_list.empty()) {
                        portal_filters.emplace_back(filter.name, pattern_list);
                    }
                }
                
                // Add filters to options if we have any
                if (!portal_filters.empty()) {
                    options["filters"] = portal_filters;
                }
            }

            DBus::Path response_path;
            if (save_dialog) {
                if(m_title.empty()) {
                    m_title = "Save File";
                }
                response_path = (*m_save_file_fn)("", m_title, options);
            } else {
                if(m_title.empty()) {
                    m_title = directory?"Select Directory":"Select File";
                }
                response_path = (*m_open_file_fn)("", m_title, options);
            }

            auto request_proxy = m_connection->create_object_proxy("org.freedesktop.portal.Desktop", response_path);
            auto request = request_proxy->create_signal<OpenFileResponseFn>("org.freedesktop.portal.Request", "Response");
            (void)request->connect([this, &files](u32 response, std::map<std::string, DBus::Variant> data) {
                if (response == 0) {
                    auto it = data.find("uris");
                    if (it != data.end()) {
                        for (auto const& file : data["uris"].to_vector<std::string>()) {
                            // We only support localhost for now.
                            if (file.substr(0, 8) == std::string("file:///")) {
                                files.push_back(file.substr(7));
                            } else {
                                LOG_WARNF("Got invalid URI: {}", file);
                            }
                        }
                    }
                }
                m_completed_variable.notify_all();
            });

            std::unique_lock lock(m_mutex);
            m_completed_variable.wait(lock);

            return files;
        }

        auto open_directory_picker() -> std::string
        {
            return open_file_picker({}, false, true).at(0);
        }

        void set_path(std::string const& path)
        {
            m_path = path;
        }

        void set_title(std::string const& title)
        {
            m_title = title;
        }
    protected:
        std::condition_variable m_completed_variable;
        std::mutex m_mutex;
        Ref<DBus::Dispatcher> m_dispatcher {};
        Ref<DBus::Connection> m_connection {};
        Ref<DBus::ObjectProxy> m_desktop_proxy {};
        Ref<DBus::MethodProxy<OpenFileFn>> m_open_file_fn {};
        Ref<DBus::MethodProxy<SaveFileFn>> m_save_file_fn {};
        std::string m_path;
        std::string m_title;
        HWND m_parent_window = 0;  // Add parent window member
    };

}

BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out)
{
    return FALSE;
}

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode)
{
    if (!p || !p->lpstrFile || p->nMaxFile == 0) {
        return FALSE;
    }
    
    // Clear the output buffer
    memset(p->lpstrFile, 0, p->nMaxFile);
    
    try {
        Fussion::Dialogs::LinuxDialog dialog;
        
        // Set the parent window if specified
        if (p->hwndOwner) {
            dialog.set_parent_window(p->hwndOwner);
        }
        if(p->lpstrTitle){
            dialog.set_title(p->lpstrTitle);
        }
        bool is_directory = (mode == FOLDER);
        bool is_save = (mode == SAVE);
        bool allow_multiple = (p->Flags & OFN_ALLOWMULTISELECT) != 0 && !is_save && !is_directory;
        
        // Parse filters from lpstrFilter
        std::vector<Fussion::Dialogs::FilePickerFilter> filters;
        if (p->lpstrFilter && !is_directory) {
            const char* filter_ptr = p->lpstrFilter;
            while (*filter_ptr) {
                Fussion::Dialogs::FilePickerFilter filter;
                filter.name = std::string(filter_ptr);
                filter_ptr += strlen(filter_ptr) + 1;
                
                if (*filter_ptr) {
                    std::string patterns(filter_ptr);
                    // Parse multiple patterns separated by semicolons
                    size_t start = 0;
                    size_t semicolon_pos = patterns.find(';');
                    while (start < patterns.length()) {
                        if (semicolon_pos == std::string::npos) {
                            semicolon_pos = patterns.length();
                        }
                        
                        std::string pattern = patterns.substr(start, semicolon_pos - start);
                        // Remove any whitespace
                        pattern.erase(0, pattern.find_first_not_of(" \t"));
                        pattern.erase(pattern.find_last_not_of(" \t") + 1);
                        
                        if (!pattern.empty()) {
                            filter.file_patterns.push_back(pattern);
                        }
                        
                        start = semicolon_pos + 1;
                        if (start >= patterns.length()) {
                            break;
                        }
                        semicolon_pos = patterns.find(';', start);
                    }
                    
                    filter_ptr += strlen(filter_ptr) + 1;
                }
                
                if (!filter.name.empty() && !filter.file_patterns.empty()) {
                    filters.push_back(filter);
                }
                
                if (!*filter_ptr) break;
            }
        }
        
        // Set initial directory if provided
        if (p->lpstrInitialDir) {
            dialog.set_path(std::string(p->lpstrInitialDir));
        }
        
        // Call the appropriate dialog method
        std::vector<std::string> results;
        if (is_directory) {
            std::string dir = dialog.open_directory_picker();
            if (!dir.empty()) {
                results.push_back(dir);
            }
        } else {
            results = dialog.open_file_picker(filters, allow_multiple, false, is_save);
        }
        
        if (results.empty()) {
            return FALSE; // User cancelled
        }
        
        if (is_directory || is_save || !allow_multiple) {
            // Single file selection or save dialog
            const std::string& file = results[0];
            if (file.length() >= p->nMaxFile) {
                return FALSE; // Buffer too small
            }
            strcpy(p->lpstrFile, file.c_str());
            
            // Set offset and extension info
            p->nFileOffset = 0;
            p->nFileExtension = 0;
            for (size_t i = 0; i < file.length(); i++) {
                if (file[i] == '/') {
                    p->nFileOffset = static_cast<WORD>(i + 1);
                } else if (file[i] == '.' && p->nFileExtension == 0) {
                    p->nFileExtension = static_cast<WORD>(i + 1);
                }
            }
        } else {
            // Multiple file selection
            // For multiple selections, the buffer should contain:
            // 1. Directory path followed by '\0'
            // 2. Each filename followed by '\0'
            // 3. Final '\0' to terminate the list
            
            if (results.size() == 1) {
                // If only one file was selected, treat as single file selection
                const std::string& file = results[0];
                if (file.length() >= p->nMaxFile) {
                    return FALSE; // Buffer too small
                }
                strcpy(p->lpstrFile, file.c_str());
                
                // Set offset and extension info
                p->nFileOffset = 0;
                p->nFileExtension = 0;
                for (size_t i = 0; i < file.length(); i++) {
                    if (file[i] == '/') {
                        p->nFileOffset = static_cast<WORD>(i + 1);
                    } else if (file[i] == '.' && p->nFileExtension == 0) {
                        p->nFileExtension = static_cast<WORD>(i + 1);
                    }
                }
            } else {
                // Multiple files selected
                // Find common directory
                std::string directory = results[0];
                size_t lastSlash = directory.find_last_of('/');
                if (lastSlash != std::string::npos) {
                    directory = directory.substr(0, lastSlash + 1);
                } else {
                    directory = "";
                }
                
                // Check if buffer is large enough
                size_t needed = directory.length() + 1; // +1 for null terminator
                for (const auto& file : results) {
                    size_t fileNameStart = file.find_last_of('/') + 1;
                    if (fileNameStart == std::string::npos) fileNameStart = 0;
                    std::string fileName = file.substr(fileNameStart);
                    needed += fileName.length() + 1; // +1 for null terminator
                }
                needed += 1; // Final null terminator
                
                if (needed > p->nMaxFile) {
                    return FALSE; // Buffer too small
                }
                
                // Fill the buffer
                char* buffer = p->lpstrFile;
                size_t pos = 0;
                
                // Copy directory
                strcpy(buffer + pos, directory.c_str());
                pos += directory.length() + 1;
                
                // Copy filenames
                for (const auto& file : results) {
                    size_t fileNameStart = file.find_last_of('/') + 1;
                    if (fileNameStart == std::string::npos) fileNameStart = 0;
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
    } catch (...) {
        return FALSE; // Error occurred
    }
}