// diskview-qml.exe: the program of the QML edition of ra9fael.diskview.
//
// WinShun runs the program named in plugin.json and talks JSON-RPC over
// stdin/stdout. This starter has no Qt and links the C runtime in: it adds
// WinShun's own folder (WS_APP_DIR, set by the plugin host) to the folders
// DLLs are searched in and loads DiskViewQmlApp.dll from its own folder. That
// DLL imports Qt, which the loader then finds in WinShun's folder — the host
// is the only place a Qt is guaranteed.
//
// `diskview-qml.exe --preview [--app-dir <folder>]` opens the window with no
// WinShun involved: how the edition is checked on its own.

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using Entry = int(__cdecl*)(const wchar_t* appDir);

int fail(int code, const char* what, DWORD error = 0)
{
    // stderr goes to the host's plugin log; stdout belongs to the protocol.
    if (error)
        std::fprintf(stderr, "diskview-qml: %s (error %lu)\n", what, error);
    else
        std::fprintf(stderr, "diskview-qml: %s\n", what);
    return code;
}

std::wstring folderOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool preview = false;
    std::wstring appDir;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        preview = preview || arg == L"--preview";
        if (arg == L"--app-dir" && i + 1 < argc) appDir = argv[i + 1];
    }
    if (appDir.empty()) {
        appDir.assign(32768, L'\0');
        const DWORD length = ::GetEnvironmentVariableW(L"WS_APP_DIR", appDir.data(),
                                                       static_cast<DWORD>(appDir.size()));
        if (length == 0 || length >= appDir.size()) {
            return fail(2, "WS_APP_DIR is not set: WinShun starts this plugin (--preview takes --app-dir)");
        }
        appDir.resize(length);
    }
    if (::GetFileAttributesW((appDir + L"\\Qt6Core.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail(3, "no Qt6Core.dll in WS_APP_DIR: this WinShun cannot run the plugin");
    }

    std::wstring self(32768, L'\0');
    const DWORD selfLength = ::GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (selfLength == 0 || selfLength >= self.size()) return fail(4, "own path unknown", ::GetLastError());
    self.resize(selfLength);

    if (!::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        || !::AddDllDirectory(appDir.c_str())) {
        return fail(5, "DLL folders not set", ::GetLastError());
    }
    const std::wstring dll = folderOf(self) + L"\\DiskViewQmlApp.dll";
    HMODULE module = ::LoadLibraryExW(dll.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!module) return fail(6, "DiskViewQmlApp.dll not loaded (a WinShun with another Qt?)", ::GetLastError());
    const auto entry = reinterpret_cast<Entry>(::GetProcAddress(module, "winshunDiskViewQmlMain"));
    if (!entry) return fail(7, "DiskViewQmlApp.dll has no winshunDiskViewQmlMain");
    return entry(appDir.c_str());
}
