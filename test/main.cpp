#include <iostream>
#include "strategy.h"

int main(int argc, char *argv[]) {

    if (argc != 3) {
        std::cout << "Usage: <program name> <hostname> <port>" << std::endl;

        return 0;
    }

    Strategy strategy {Strategy::StrategyConfig{}};

    strategy.run(argv[1], argv[2]);
}
