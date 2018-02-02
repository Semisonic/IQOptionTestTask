#include <iostream>
#include <sstream>

#include "overseer.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "Usage: <program name> <port number to listen>" << std::endl;

        return 0;
    }

    int portNumber = 0;
    std::istringstream iss {argv[1]};

    if (!(iss >> portNumber)) {
        std::cout << "Usage: <program name> <port number to listen>" << std::endl << "port must be numeric" << std::endl;

        return 0;
    }

    if (portNumber < 0 || portNumber > USHRT_MAX) {
        std::cout << "Usage: <program name> <port number to listen>" << std::endl
                  << "port must be between 0 and " << USHRT_MAX << std::endl;

        return 0;
    }

    Overseer os;

    os.run(static_cast<unsigned short>(portNumber));

    return 0;
}