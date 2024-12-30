#ifndef _CLS_MGR_H_
#define _CLS_MGR_H_
#include <mutex>
#include <map>
#include <list>
#include <atomic>
#include <string>

class ClassMgr {
private:
    ClassMgr() :atom_start(10){
        
    }

    ~ClassMgr() {
        std::unique_lock<std::recursive_mutex> lock(cls_mutex);
        for (auto it : class_list) {
            DeleteObject(it->hbrBackground);
            DestroyIcon(it->hIcon);
            DestroyIcon(it->hIconSm);
            free(it);
        }
        class_list.clear();
    }

public:
    static ClassMgr * instance();
public:
    CLASS* find_class(HINSTANCE module, LPCSTR clsName);
    ATOM get_class_info(HINSTANCE instance, const char* class_name, WNDCLASSEXA* wc);
    UINT get_atom_name(ATOM atomName, LPSTR name, int cchLen);
    ATOM register_class(const WNDCLASSEXA* wc);
    BOOL unregister_class(LPCSTR className, HINSTANCE instance);
    void builtin_register();
public:
    std::recursive_mutex cls_mutex;
    std::list<CLASS*> class_list;
    std::map<std::string, ATOM> atom_map;
    std::atomic_uint32_t atom_start;
    bool     builtin_registed=false;
};


#endif//_CLS_MGR_H_