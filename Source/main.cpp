#include "App.h"

#include <ixwebsocket/IXNetSystem.h>

int main()
{
    ix::initNetSystem();
    const int exitCode = App().Run();
    ix::uninitNetSystem();

    return exitCode;
}
