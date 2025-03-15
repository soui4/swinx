
#include "gtkdlghelper.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gtk/gtkfilechooser.h>

namespace Gtk{

class GtkInit{
public:
GtkInit(){
    int argc=1;
    char arg0[10]="swinx";
    char** args = new char*[1];
    args[0]=arg0;
    gtk_init(&argc,&args);
    delete []args;
}
};
static GtkInit s_gtkInit;

static char* substr(const char *src, int m, int n)
{
    int len = n - m;
    char *dest = (char*)malloc(sizeof(char) * (len + 1));
    for (int i = m; i < n && (*(src + i) != '\0'); i++) {
        *dest = *(src + i);
        dest++;
    }
    *dest = '\0';
    return dest - len;
}

static void parseString(GSList** list,
                      const char* str,
                      const char* delim)
{
    char *_str = strdup(str);
    char *token = strtok(_str, delim);
    while (token != NULL) {
        *list = g_slist_append(*list, (gpointer)strdup(token));
        token = strtok(NULL, delim);
    }
    free(_str);
}

static GtkWindow * native2GtkWindow(HWND winid){
    GdkWindow *gdk_wnd = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(), winid);
    // 创建一个临时的 GTK 窗口
    if(!gdk_wnd) return nullptr;
    GtkWindow *gtk_wnd = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_widget_realize(GTK_WIDGET(gtk_wnd));  // 确保窗口被实现
    gtk_widget_set_window(GTK_WIDGET(gtk_wnd), gdk_wnd);  // 设置 GDK 窗口
    return gtk_wnd;
}

static bool nativeFileDialog(HWND parent,
                      Gtk::Mode mode,
                      char*** filenames,
                      int* files_count,
                      const char* title,
                      const char* file,
                      const char* path,
                      const char* flt,
                      char** sel_filter,
                      bool sel_multiple)
{
    bool ret = false;
    GtkFileChooserAction actions[] = {
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    };

    GtkWindow *gtk_parent = native2GtkWindow(parent);

    GtkWidget *dialog = NULL;
    dialog = gtk_file_chooser_dialog_new(title,
                                         gtk_parent,
                                         actions[mode],
                                         g_dgettext("gtk30", "_Cancel"),
                                         GTK_RESPONSE_CANCEL,
                                         mode == Gtk::Mode::OPEN || mode == Gtk::Mode::FOLDER  ?
                                             g_dgettext("gtk30", "_Open") : g_dgettext("gtk30", "_Save"),
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);

    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    if (mode == Gtk::Mode::OPEN || mode == Gtk::Mode::FOLDER) {
        gtk_file_chooser_set_current_folder(chooser, path);
        gtk_file_chooser_set_select_multiple (chooser, sel_multiple ? TRUE : FALSE);
    } else {
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, FALSE);
        gtk_file_chooser_set_current_folder(chooser, path);
        gtk_file_chooser_set_current_name(chooser, file);
    }

    // Filters
    GSList *list = NULL;
    if (mode != Gtk::Mode::FOLDER) {
        parseString(&list, flt, ";;");
        for (guint i = 0; i < g_slist_length(list); i++) {
            if (char *flt_name = (char*)g_slist_nth(list, i)->data) {
                GtkFileFilter *filter = gtk_file_filter_new();
                char *start = strchr(flt_name, '(');
                char *end = strchr(flt_name, ')');
                char *short_flt_name = NULL;
                if (mode == Gtk::Mode::OPEN && strlen(flt_name) > 255 && start != NULL) {
                    int end_index = (int)(start - flt_name - 1);
                    if (end_index > 0)
                        short_flt_name = substr(flt_name, 0, end_index);
                }
                gtk_file_filter_set_name(filter, short_flt_name ? short_flt_name : flt_name);
                if (short_flt_name)
                    free(short_flt_name);

                if (start != NULL && end != NULL) {
                    int start_index = (int)(start - flt_name);
                    int end_index = (int)(end - flt_name);
                    if (start_index < end_index) {
                        char *fltrs = substr(flt_name, start_index + 1, end_index);
                        //g_print("%s\n", fltrs);
                        GSList *flt_list = NULL;
                        parseString(&flt_list, fltrs, " ");
                        free(fltrs);
                        for (guint j = 0; j < g_slist_length(flt_list); j++) {
                            char *nm = (char*)g_slist_nth(flt_list, j)->data;
                            if (nm != NULL)
                                gtk_file_filter_add_pattern(filter, nm);
                        }
                        if (flt_list)
                            g_slist_free(flt_list);
                    }
                }
                gtk_file_chooser_add_filter(chooser, filter);
                if (sel_filter && *sel_filter && strcmp(flt_name, *sel_filter) == 0)
                    gtk_file_chooser_set_filter(chooser, filter);
            }
        }
    }

    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        if (sel_multiple) {
            GSList *filenames_list = gtk_file_chooser_get_filenames(chooser);
            *files_count = (int)g_slist_length(filenames_list);
            *filenames = (char**)calloc((size_t)(*files_count), sizeof(char*));
            for (guint i = 0; i < g_slist_length(filenames_list); i++)
                (*filenames)[i] = strdup((char*)g_slist_nth(filenames_list, i)->data);
            g_slist_free(filenames_list);
        } else {
            *files_count = 1;
            *filenames = (char**)calloc((size_t)(*files_count), sizeof(char*));
            **filenames = gtk_file_chooser_get_filename(chooser);
        }
        ret = true;
    }
    if (mode != Gtk::Mode::FOLDER) {
        GtkFileFilter *s_filter = gtk_file_chooser_get_filter(chooser);
        if (sel_filter && *sel_filter)
            free(*sel_filter);
        if (s_filter && sel_filter)
            *sel_filter = strdup(gtk_file_filter_get_name(s_filter));
    }
    gtk_widget_destroy(dialog);
    if (list)
        g_slist_free(list);
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
    return ret;
}


bool openGtkFileChooser( std::list<std::string> &files,
                               HWND parent,
                               Mode mode,
                               const std::string &title,
                               const std::string &file,
                               const std::string &path,
                               const std::string &filter,
                               std::string *sel_filter,
                               bool sel_multiple)
{
    int pos= file.rfind("/");
    const std::string _file = (pos != -1) ?
                file.substr(pos + 1) : file;
    const std::string _path = (path.empty() && pos != -1) ?
                file.substr(0, pos) : path;

    char **filenames = nullptr;
    char *_sel_filter = (sel_filter) ? strdup(sel_filter->c_str()) : nullptr;
    int files_count = 0;
    bool ret = nativeFileDialog(parent,
                     mode,
                     &filenames,
                     &files_count,
                     title.c_str(),
                     _file.c_str(),
                     _path.c_str(),
                     filter.c_str(),
                     &_sel_filter,
                     sel_multiple);

    for (int i = 0; i < files_count; i++) {
        if (filenames[i]) {
            files.push_back(filenames[i]);
            free(filenames[i]);
        }
    }
    if (filenames) {
        free(filenames);
    }
    if (_sel_filter) {
        if (sel_filter)
            *sel_filter = _sel_filter;
        free(_sel_filter);
    }
    return ret;
}

static inline BYTE tobcolor(double v){
    return (BYTE)(v*255);
}
bool gtk_choose_color(HWND parent,COLORREF *out){
    bool ret = false;
    GtkWindow *gtk_parent = native2GtkWindow(parent);
    // 创建颜色选择对话框
    GtkWidget * dialog = gtk_color_chooser_dialog_new("Choose Color", gtk_parent);

    // 运行对话框并获取用户响应
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        // 获取用户选择的颜色
        GdkRGBA color;      // 用于存储选择的颜色
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);
        ret = true;
        *out = RGBA(tobcolor(color.red),tobcolor(color.green),tobcolor(color.blue),tobcolor(color.alpha));
    }
    // 销毁对话框
    gtk_widget_destroy(dialog);
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
    return ret;
}


}//end of ns Gtk