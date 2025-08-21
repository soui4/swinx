#include <windows.h>
#include <commdlg.h>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory> // Add this include for better memory management
#include <sstream>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#define LOG_DEBUGF(...)
#define LOG_WARNF(...)
#define LOG_ERRORF(...)
#define LOG_FATALF(...)

template <typename T>
using Ref = std::shared_ptr<T>;

template <typename T, typename... Args>
Ref<T> make_ref(Args &&...args)
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



class LinuxDialog {
  public:
    explicit LinuxDialog()
    {
        LOG_DEBUGF("Initializing Linux Dialog");
    }

    virtual ~LinuxDialog() = default;

    void set_parent_window(HWND parent)
    {
        m_parent_window = parent;
    }

    auto open_file_picker(std::vector<FilePickerFilter> const &filters, bool allow_multiple, bool directory = false, bool save_dialog = false) -> std::vector<std::string>
    {
        std::vector<std::string> files{};

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
        if (!m_title.empty()) {
            command += " --title=\"" + m_title + "\"";
        }
        
        // Add initial path if specified
        if (!m_path.empty()) {
            command += " --filename=\"" + m_path + "\"";
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

    std::string choose_color(COLORREF initial_color = 0) {
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

    auto open_directory_picker() -> std::string
    {
        return open_file_picker({}, false, true).at(0);
    }

    void set_path(std::string const &path)
    {
        m_path = path;
    }

    void set_title(std::string const &title)
    {
        m_title = title;
    }

  protected:
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

    std::string m_path;
    std::string m_title;
    HWND m_parent_window = 0; // Add parent window member
};

} // namespace swinx::Dialogs

BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out)
{
    try
    {
        swinx::Dialogs::LinuxDialog dialog;
        
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
            // Parse the HTML color format (#RRGGBB) returned by zenity
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
        swinx::Dialogs::LinuxDialog dialog;

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

void test_pick_file()
{
    TCHAR szBuf[MAX_PATH];

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = 0;
    ofn.lpstrFile = szBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = _T("*.*");
    ofn.lpstrFilter = _T("All Files\0*.*\0");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    SGetOpenFileNameA(&ofn, OPEN);
}

void test_pick_folder()
{
    TCHAR szBuf[MAX_PATH];

    BROWSEINFO info = { 0 };
    info.lpszPath = szBuf;
    info.nMaxPath = MAX_PATH;
    if (PickFolder(&info))
    {
    }
}