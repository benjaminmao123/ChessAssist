#include "App.h"

#include <ixwebsocket/IXNetSystem.h>
#include <nfd.h>

int main()
{
    ix::initNetSystem();
    NFD_Init();

    const int exitCode = App().Run();

    NFD_Quit();
    ix::uninitNetSystem();

    return exitCode;
}
