#include "../src/pricing.hpp"
#include <cassert>
#include <iostream>

int main() {
    const auto goldPeak = calculate(1000, 9, 2, "Gold");
    assert(goldPeak.base == 2000 && goldPeak.surcharge == 400);
    assert(goldPeak.discount == 240 && goldPeak.total == 2160);
    assert(ringgit(goldPeak.total) == "RM 21.60");
    const auto basicOffPeak = calculate(1000, 12, 2, "Basic");
    assert(basicOffPeak.total == 2000 && basicOffPeak.surcharge == 0);
    const auto silverMixed = calculate(1000, 11, 2, "Silver");
    assert(silverMixed.surcharge == 200 && silverMixed.discount == 110 && silverMixed.total == 2090);
    std::cout << "Pricing tests passed.\n";
}
