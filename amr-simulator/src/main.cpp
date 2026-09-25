#include "application.h"

#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: AMRSimulator.exe <json-path>\n";
        return 1;
    }

    Application application(argv[1]);

    if (!application.init())
    {
        return 1;
    }

    application.run();

    return 0;
}