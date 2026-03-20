#include <iostream>
#include <vector>

#include <water/app.h>

int main() {

    App app;
    app.Init();
    app.Run();
    app.Shutdown();

    return 0;
}