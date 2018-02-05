#include "name_generator.h"

#include <sstream>
#include <array>
#include <random>

std::string NameGenerator::newName () {
    static const std::array<std::string, 23> names
            { "Johnny", "David", "Maria", "Michael", "Hannah", "Jacob", "Alex", "Sarah", "Ashley", "Austin", "Rachel",
              "Tyler", "Taylor", "Andrew", "Jessica", "Daniel", "Katie", "Emma", "Matthew", "Lauren", "Ryan", "Samantha",
              "Bill"
            };

    static const std::array<std::string, 31> lastNames {
        "Smith", "Johnson", "Williams", "Jones", "Brown", "Davis", "Miller", "Wilson", "Moore", "Anderson", "Thomas",
        "Thomas", "Jackson", "White", "Harris", "Martin", "Thompson", "Garcia", "Martinez", "Robinson", "Clark", "Rodriguez",
        "Lewis", "Lee", "Walker", "Hall", "Allen", "Young", "King", "Hernandez", "Wright" };

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(2, 10);
    static int nameIndex {0};
    static int lastNameIndex {0};

    std::ostringstream oss;

    oss << names[nameIndex++] << " " << lastNames[lastNameIndex++] << " " << dis(gen);

    if (nameIndex == 23) nameIndex = 0;
    if (lastNameIndex == 31) lastNameIndex = 0;

    return oss.str();
}
