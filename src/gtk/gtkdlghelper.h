#ifndef GTKFILECHOOSER_H
#define GTKFILECHOOSER_H
#include <stdint.h>
#include <string>
#include <list>
#include <windows.h>
namespace Gtk
{
typedef enum {
    OPEN = 0, SAVE = 1, FOLDER = 2
} Mode;

bool openGtkFileChooser( std::list<std::string> &files,
                               HWND parent,
                               Mode mode,
                               const std::string &title,
                               const std::string &file,
                               const std::string &path,
                               const std::string &filter,
                               std::string *sel_filter,
                               bool sel_multiple=false);


bool gtk_choose_color(HWND parent,COLORREF *ret);

}
#endif // GTKFILECHOOSER_H
