#pragma once
#include <iomanip>
#include <sstream>
#include <string>

struct Price {
    long long base, surcharge, discount, total;
};

inline Price calculate(long long hourly, int startHour, int hours, const std::string& tier) {
    long long base = hourly * hours;
    int peakHours = 0;
    for (int hour = startHour; hour < startHour + hours; ++hour)
        if ((hour >= 9 && hour < 12) || (hour >= 17 && hour < 19)) ++peakHours;
    long long surcharge = (hourly * peakHours * 20 + 50) / 100;
    int percent = tier == "Gold" ? 10 : tier == "Silver" ? 5 : 0;
    long long discount = ((base + surcharge) * percent + 50) / 100;
    return {base, surcharge, discount, base + surcharge - discount};
}

inline std::string ringgit(long long cents) {
    std::ostringstream out;
    out << "RM " << cents / 100 << '.' << std::setw(2) << std::setfill('0') << cents % 100;
    return out.str();
}
