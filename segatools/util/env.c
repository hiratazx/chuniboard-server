#include "env.h"

const wchar_t* get_config_path() {
    static wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"SEGATOOLS_CONFIG_PATH", path, MAX_PATH)) {
        return L".\\segatools.ini";
    }

    return path;
}
