
#ifndef _COFFPARSER_H_
#define _COFFPARSER_H_

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdint.h>
#include <mutex>


#pragma pack(push, 1)
// COFF文件头
struct COFF_FILE_HEADER {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

// COFF段头
struct COFF_SECTION_HEADER {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

// 资源目录结构
struct IMAGE_RESOURCE_DIRECTORY {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint16_t NumberOfNamedEntries;
    uint16_t NumberOfIdEntries;
};

// 资源目录条目
struct IMAGE_RESOURCE_DIRECTORY_ENTRY {
    union {
        struct {
            uint32_t NameOffset : 31;
            uint32_t NameIsString : 1;
        };
        uint32_t Name;
        uint16_t Id;
    };

    union {
        uint32_t OffsetToData;
        struct {
            uint32_t OffsetToDirectory : 31;
            uint32_t DataIsDirectory : 1;
        };
    };
};

// 资源数据条目
struct IMAGE_RESOURCE_DATA_ENTRY {
    uint32_t DataRVA;
    uint32_t Size;
    uint32_t CodePage;
    uint32_t Reserved;
};

// 资源名称字符串
struct IMAGE_RESOURCE_DIR_STRING_U {
    uint16_t Length;
    uint16_t NameString[1];
};

// COFF符号表条目
struct COFF_SYMBOL {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    uint16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};
#pragma pack(pop)

// 资源信息结构
struct ResourceInfo {
    std::wstring typeName;
    std::wstring nameName;
    uint16_t languageId;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t codePage;
    const uint8_t* dataPtr;
};

// 主解析器类
class WindResResourceParser {
private:
    const uint8_t* m_baseAddr;
    size_t m_totalSize;
    std::map<std::wstring, std::map<std::wstring, std::map<uint16_t, ResourceInfo>>> m_resourceMap;
    bool m_isValid;

    // 将资源ID字符串转换为可读形式
    std::wstring ResourceIdToString(uintptr_t id) const;

    // 解析Unicode字符串（COFF中的资源名是Unicode）
    std::wstring ParseUnicodeString(const uint8_t* data) const;

    // 查找.data段
    const uint8_t* FindDataSection() const;

    // 递归解析资源目录
    void ParseResourceDirectory(const uint8_t* dirBase, uint32_t dirOffset,
        const std::vector<std::wstring>& typePath,
        std::vector<ResourceInfo>& resources, int depth = 0);

public:
    WindResResourceParser(const void* data, size_t size)
        : m_baseAddr(static_cast<const uint8_t*>(data)),
        m_totalSize(size),
        m_isValid(false) {
    }

    WindResResourceParser(const void* start, const void* end)
        : m_baseAddr(static_cast<const uint8_t*>(start)),
        m_totalSize(static_cast<const uint8_t*>(end) -
            static_cast<const uint8_t*>(start)),
        m_isValid(false) {
    }

    ~WindResResourceParser() {
        m_resourceMap.clear();
    }

    BOOL Parse() ;

    // 兼容Windows API的函数

    // FindResource 的等价实现
    HRSRC FindResourceW(HMODULE hModule, const wchar_t* lpName, const wchar_t* lpType, WORD wLanguage) const;

    // SizeofResource 的等价实现
    uint32_t SizeofResource(HMODULE hModule, HRSRC hResInfo) const;

    // LoadResource 的等价实现
    HGLOBAL LoadResource(HMODULE hModule, HRSRC hResInfo) ;

    // LockResource 的等价实现
    static const void* LockResource(HGLOBAL hResData) ;
    // 枚举资源类型
    BOOL EnumResourceTypesA(HMODULE hModule, 
        ENUMRESTYPEPROCA lpEnumFunc,
        LONG_PTR lParam) ;

    // 枚举资源类型
    BOOL EnumResourceTypesW(HMODULE hModule,
        ENUMRESTYPEPROCW lpEnumFunc,
        LONG_PTR lParam) ;

    // 枚举资源名称
    BOOL EnumResourceNamesA(HMODULE hModule, const char* lpType,
        ENUMRESNAMEPROCA lpEnumFunc,
        LONG_PTR lParam) ;

    // 枚举资源名称
    BOOL EnumResourceNamesW(HMODULE hModule, const wchar_t* lpType,
        ENUMRESNAMEPROCW lpEnumFunc,
        LONG_PTR lParam) ;

    // 枚举资源名称
    BOOL EnumResourceLanguagesA(HMODULE hModule, const char* lpType, const char * lpName,
        ENUMRESLANGPROCA lpEnumFunc,
        LONG_PTR lParam) ;

    // 枚举资源名称
    BOOL EnumResourceLanguagesW(HMODULE hModule, const wchar_t* lpType, const wchar_t * lpName,
        ENUMRESLANGPROCW lpEnumFunc,
        LONG_PTR lParam) ;

    // 加载字符串资源的方法
    int LoadStringW(UINT uID, LPWSTR lpBuffer, int cchBufferMax) const;

    // 打印所有资源信息（调试用）
    void DumpResources() const ;
};


#endif//_COFFPARSER_H_