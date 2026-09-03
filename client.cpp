#include <windows.h>

#include "plugin.h"
#include "plugin_sa.h"

using namespace plugin;

static void TestGameProcess()
{
    // Пока намеренно ничего не делаем.
    // Нам важно только, что Plugin-SDK успешно подключился
    // и событие игрового процесса вызывается.
}

class GTAOnlinePlugin
{
public:
    GTAOnlinePlugin()
    {
        Events::gameProcessEvent += TestGameProcess;
    }
};

static GTAOnlinePlugin gPlugin;

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID reserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(reason);
    UNREFERENCED_PARAMETER(reserved);

    return TRUE;
}
