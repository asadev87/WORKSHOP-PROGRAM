#include <mysql/jdbc.h>
#include "pricing.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#endif

using Params = std::vector<std::string>;
struct Rows {
    std::unique_ptr<sql::PreparedStatement> statement;
    std::unique_ptr<sql::ResultSet> result;
    sql::ResultSet* operator->() { return result.get(); }
    sql::ResultSet& operator*() { return *result; }
};

std::string env(const char* name, const char* fallback = "") {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

std::string ask(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    if (!std::getline(std::cin, value)) throw std::runtime_error("Input closed.");
    return value;
}

std::string askSecret(const std::string& prompt) {
#ifdef _WIN32
    std::cout << prompt;
    std::string value;
    while (true) {
        int c = _getch();
        if (c == '\r') { std::cout << '\n'; return value; }
        if (c == '\b') { if (!value.empty()) value.pop_back(); continue; }
        if (c >= 32 && c <= 126 && value.size() < 128) value.push_back(static_cast<char>(c));
    }
#else
    return ask(prompt);
#endif
}

std::string requiredSecret(const std::string& prompt) {
    auto value = askSecret(prompt);
    if (value.empty()) throw std::runtime_error("Password is required.");
    return value;
}

std::string required(const std::string& prompt, size_t maximum = 120) {
    auto value = ask(prompt);
    if (value.empty() || value.size() > maximum) throw std::runtime_error("Required value is empty or too long.");
    return value;
}

std::string id(const std::string& prompt) {
    auto value = required(prompt, 20);
    if (!std::regex_match(value, std::regex("[1-9][0-9]*"))) throw std::runtime_error("Enter a positive ID.");
    return value;
}

std::string money(const std::string& prompt) {
    auto value = required(prompt, 12);
    std::smatch match;
    if (!std::regex_match(value, match, std::regex("([0-9]{1,9})(?:\\.([0-9]{1,2}))?")))
        throw std::runtime_error("Enter an amount such as 25.00.");
    long long cents = std::stoll(match[1].str()) * 100;
    if (match[2].matched) cents += std::stoll(match[2].str()) * (match[2].length() == 1 ? 10 : 1);
    if (cents <= 0) throw std::runtime_error("Amount must be greater than zero.");
    return std::to_string(cents);
}

void validEmail(const std::string& email) {
    if (email.size() > 254 || !std::regex_match(email, std::regex(R"([^\s@]+@[^\s@]+\.[^\s@]+)")))
        throw std::runtime_error("Enter a valid email address.");
}

std::tm slot(const std::string& input) {
    if (!std::regex_match(input, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:00)")))
        throw std::runtime_error("Use YYYY-MM-DD HH:00 (whole hours).");
    std::tm tm{};
    std::istringstream in(input);
    in >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if (in.fail()) throw std::runtime_error("Invalid date or time.");
    tm.tm_isdst = -1;
    std::tm original = tm;
    if (std::mktime(&tm) == -1 || tm.tm_year != original.tm_year || tm.tm_mon != original.tm_mon ||
        tm.tm_mday != original.tm_mday || tm.tm_hour != original.tm_hour)
        throw std::runtime_error("Invalid calendar date.");
    return tm;
}

struct Times {
    std::string start, end;
    int hours;
    int startHour;
};

Times readTimes() {
    auto start = required("Start (YYYY-MM-DD HH:00): ", 16);
    auto end = required("End   (YYYY-MM-DD HH:00): ", 16);
    auto a = slot(start), b = slot(end);
    if (start.substr(0, 10) != end.substr(0, 10) || a.tm_hour < 8 || b.tm_hour > 20 ||
        b.tm_hour <= a.tm_hour)
        throw std::runtime_error("Book within one day, 08:00-20:00, for at least one hour.");
    if (std::mktime(&a) < std::time(nullptr)) throw std::runtime_error("Bookings must start in the future.");
    return {start + ":00", end + ":00", b.tm_hour - a.tm_hour, a.tm_hour};
}

std::string randomSalt() {
    std::random_device device;
    std::ostringstream out;
    for (int i = 0; i < 16; ++i) out << std::hex << std::setw(2) << std::setfill('0') << (device() & 255);
    return out.str();
}

class App {
    std::unique_ptr<sql::Connection> db;

    std::unique_ptr<sql::PreparedStatement> prepare(const std::string& sqlText, const Params& params = {}) {
        std::unique_ptr<sql::PreparedStatement> stmt(db->prepareStatement(sqlText));
        for (size_t i = 0; i < params.size(); ++i) stmt->setString(static_cast<unsigned int>(i + 1), params[i]);
        return stmt;
    }

    Rows query(const std::string& sqlText, const Params& params = {}) {
        auto stmt = prepare(sqlText, params);
        std::unique_ptr<sql::ResultSet> result(stmt->executeQuery());
        return {std::move(stmt), std::move(result)};
    }

    int change(const std::string& sqlText, const Params& params = {}) {
        auto stmt = prepare(sqlText, params);
        return stmt->executeUpdate();
    }

    std::string one(const std::string& sqlText, const Params& params = {}) {
        auto rows = query(sqlText, params);
        return rows->next() ? rows->getString(1).asStdString() : "";
    }

    static void printRows(sql::ResultSet& rows, const std::vector<std::string>& columns) {
        for (const auto& column : columns) std::cout << std::left << std::setw(22) << column;
        std::cout << '\n';
        size_t count = 0;
        while (rows.next()) {
            for (size_t i = 0; i < columns.size(); ++i) {
                std::string value = rows.getString(static_cast<unsigned int>(i + 1)).asStdString();
                if (value.size() > 21) value = value.substr(0, 18) + "...";
                std::cout << std::left << std::setw(22) << value;
            }
            std::cout << '\n';
            ++count;
        }
        if (!count) std::cout << "No records.\n";
    }

    static void emailAndPhone(const std::string& email, const std::string& phone) {
        validEmail(email);
        if (phone.empty() || phone.size() > 30 || !std::regex_match(phone, std::regex(R"([+0-9() -]+)")))
            throw std::runtime_error("Invalid phone number.");
    }

    static void tierCheck(const std::string& tier) {
        if (tier != "Basic" && tier != "Silver" && tier != "Gold")
            throw std::runtime_error("Tier must be Basic, Silver or Gold.");
    }

    void listMembers(const std::string& search = "") {
        auto rows = query("SELECT id,name,email,phone,tier FROM members WHERE name LIKE ? OR email LIKE ? ORDER BY id",
                          {"%" + search + "%", "%" + search + "%"});
        printRows(*rows, {"ID","Name","Email","Phone","Tier"});
    }

    void memberModule() {
        while (true) {
            std::cout << "\nMEMBERS: 1 List  2 Add  3 Update  4 Delete  5 Search  0 Back\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") listMembers();
                else if (c == "2") {
                    auto name = required("Name: ");
                    auto email = required("Email: ", 254);
                    auto phone = required("Phone: ", 30);
                    auto tier = required("Tier (Basic/Silver/Gold): ", 6);
                    auto password = requiredSecret("Initial password (8+ characters): ");
                    emailAndPhone(email, phone); tierCheck(tier);
                    if (password.size() < 8) throw std::runtime_error("Password must have at least 8 characters.");
                    auto salt = randomSalt();
                    change("INSERT INTO members(name,email,phone,tier,password_salt,password_hash) VALUES(?,?,?,?,?,SHA2(CONCAT(?,?),256))",
                           {name,email,phone,tier,salt,salt,password});
                    std::cout << "Member created.\n";
                } else if (c == "3") {
                    auto memberId = id("Member ID: ");
                    auto name = required("Name: ");
                    auto email = required("Email: ", 254);
                    auto phone = required("Phone: ", 30);
                    auto tier = required("Tier (Basic/Silver/Gold): ", 6);
                    emailAndPhone(email, phone); tierCheck(tier);
                    std::cout << (change("UPDATE members SET name=?,email=?,phone=?,tier=? WHERE id=?",
                                         {name,email,phone,tier,memberId}) ? "Updated.\n" : "Member not found.\n");
                } else if (c == "4") {
                    auto memberId = id("Member ID: ");
                    std::cout << (change("DELETE FROM members WHERE id=?", {memberId}) ? "Deleted.\n" : "Member not found.\n");
                } else if (c == "5") listMembers(required("Name or email contains: "));
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }

    void listWorkspaces(const std::string& search = "", bool activeOnly = false) {
        std::string sqlText = "SELECT id,name,type,capacity,CONCAT('RM ',FORMAT(hourly_rate_cents/100,2)),status "
                              "FROM workspaces WHERE (name LIKE ? OR type LIKE ?)";
        if (activeOnly) sqlText += " AND status='Active'";
        sqlText += " ORDER BY id";
        auto rows = query(sqlText, {"%" + search + "%", "%" + search + "%"});
        printRows(*rows, {"ID","Name","Type","Capacity","Hourly rate","Status"});
    }

    void listAvailable(const Times& times) {
        auto rows = query(
            "SELECT w.id,w.name,w.type,w.capacity,CONCAT('RM ',FORMAT(w.hourly_rate_cents/100,2)) "
            "FROM workspaces w WHERE w.status='Active' AND NOT EXISTS ("
            "SELECT 1 FROM reservations r WHERE r.workspace_id=w.id AND r.status<>'Cancelled' "
            "AND r.start_at<? AND r.end_at>?) ORDER BY w.id", {times.end,times.start});
        printRows(*rows,{"ID","Name","Type","Capacity","Hourly rate"});
    }

    void workspaceModule() {
        while (true) {
            std::cout << "\nWORKSPACES: 1 List  2 Add  3 Update  4 Delete  5 Search  0 Back\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") listWorkspaces();
                else if (c == "2" || c == "3") {
                    std::string workspaceId;
                    if (c == "3") workspaceId = id("Workspace ID: ");
                    auto name = required("Name: ");
                    auto type = required("Type (Desk/Private Office/Meeting Room): ", 20);
                    auto capacity = id("Capacity: ");
                    auto rate = money("Hourly rate in RM: ");
                    auto status = required("Status (Active/Inactive): ", 8);
                    if (type != "Desk" && type != "Private Office" && type != "Meeting Room")
                        throw std::runtime_error("Invalid workspace type.");
                    if (status != "Active" && status != "Inactive") throw std::runtime_error("Invalid status.");
                    if (c == "2") {
                        change("INSERT INTO workspaces(name,type,capacity,hourly_rate_cents,status) VALUES(?,?,?,?,?)",
                               {name,type,capacity,rate,status});
                        std::cout << "Workspace created.\n";
                    } else {
                        std::cout << (change("UPDATE workspaces SET name=?,type=?,capacity=?,hourly_rate_cents=?,status=? WHERE id=?",
                                             {name,type,capacity,rate,status,workspaceId}) ? "Updated.\n" : "Workspace not found.\n");
                    }
                } else if (c == "4") {
                    auto workspaceId = id("Workspace ID: ");
                    std::cout << (change("DELETE FROM workspaces WHERE id=?", {workspaceId}) ? "Deleted.\n" : "Workspace not found.\n");
                } else if (c == "5") listWorkspaces(required("Name or type contains: "));
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }

    void listReservations(const std::string& search = "", const std::string& ownId = "") {
        auto rows = query(
            "SELECT r.id,m.name,w.name,DATE_FORMAT(r.start_at,'%Y-%m-%d %H:%i'),"
            "DATE_FORMAT(r.end_at,'%Y-%m-%d %H:%i'),r.status,CONCAT('RM ',FORMAT(r.total_cents/100,2)) "
            "FROM reservations r JOIN members m ON m.id=r.member_id JOIN workspaces w ON w.id=r.workspace_id "
            "WHERE (?='' OR r.member_id=?) AND (?='' OR m.name LIKE ? OR w.name LIKE ? OR CAST(r.id AS CHAR)=?) "
            "ORDER BY r.start_at DESC,r.id DESC LIMIT 100",
            {ownId, ownId, search, "%" + search + "%", "%" + search + "%", search});
        printRows(*rows, {"ID","Member","Workspace","Start","End","Status","Total"});
    }

    void reserve(const std::string& memberId, const std::string& workspaceId, const Times& times,
                 const std::string& existingId = "") {
        auto previewWorkspace = query("SELECT hourly_rate_cents,status FROM workspaces WHERE id=?", {workspaceId});
        if (!previewWorkspace->next()) throw std::runtime_error("Workspace not found.");
        if (previewWorkspace->getString(2) != "Active") throw std::runtime_error("Workspace is inactive.");
        auto previewMember = query("SELECT tier FROM members WHERE id=?", {memberId});
        if (!previewMember->next()) throw std::runtime_error("Member not found.");
        auto preview = calculate(std::stoll(previewWorkspace->getString(1).asStdString()),
                                 times.startHour,times.hours,previewMember->getString(1).asStdString());
        std::cout << "Base " << ringgit(preview.base) << ", peak surcharge " << ringgit(preview.surcharge)
                  << ", tier discount " << ringgit(preview.discount) << ", TOTAL " << ringgit(preview.total) << '\n';
        if (ask("Confirm booking (yes): ") != "yes") throw std::runtime_error("Booking cancelled.");
        db->setAutoCommit(false);
        try {
            auto workspace = query("SELECT hourly_rate_cents,status FROM workspaces WHERE id=? FOR UPDATE", {workspaceId});
            if (!workspace->next()) throw std::runtime_error("Workspace not found.");
            if (workspace->getString(2) != "Active") throw std::runtime_error("Workspace is inactive.");
            long long rate = std::stoll(workspace->getString(1).asStdString());
            if (!existingId.empty()) {
                auto current = query("SELECT status FROM reservations WHERE id=? AND workspace_id=? FOR UPDATE",
                                     {existingId,workspaceId});
                if (!current->next() || current->getString(1) != "Booked")
                    throw std::runtime_error("Only a booked reservation can be rescheduled.");
            }
            auto member = query("SELECT tier FROM members WHERE id=?", {memberId});
            if (!member->next()) throw std::runtime_error("Member not found.");
            std::string tier = member->getString(1).asStdString();
            auto conflict = query(
                "SELECT id FROM reservations WHERE workspace_id=? AND status<>'Cancelled' "
                "AND start_at<? AND end_at>? AND (?='' OR id<>?) LIMIT 1",
                {workspaceId,times.end,times.start,existingId,existingId});
            if (conflict->next()) throw std::runtime_error("That workspace is already reserved for this time.");
            auto price = calculate(rate,times.startHour,times.hours,tier);
            if (price.total != preview.total) throw std::runtime_error("Price changed; please book again.");
            Params values{memberId,workspaceId,times.start,times.end,std::to_string(price.base),
                          std::to_string(price.surcharge),std::to_string(price.discount),std::to_string(price.total)};
            if (existingId.empty()) {
                change("INSERT INTO reservations(member_id,workspace_id,start_at,end_at,base_cents,surcharge_cents,discount_cents,total_cents) "
                       "VALUES(?,?,?,?,?,?,?,?)", values);
                std::cout << "Reservation #" << one("SELECT LAST_INSERT_ID()") << " created.\n";
            } else {
                values.push_back(existingId);
                change("UPDATE reservations SET member_id=?,workspace_id=?,start_at=?,end_at=?,base_cents=?,"
                       "surcharge_cents=?,discount_cents=?,total_cents=? WHERE id=?", values);
                std::cout << "Reservation updated.\n";
            }
            db->commit();
        } catch (...) {
            db->rollback();
            db->setAutoCommit(true);
            throw;
        }
        db->setAutoCommit(true);
    }

    void reservationModule() {
        while (true) {
            std::cout << "\nRESERVATIONS: 1 List  2 Add  3 Reschedule  4 Delete  5 Search  6 Status  0 Back\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") listReservations();
                else if (c == "2") {
                    auto memberId = id("Member ID: "); auto workspaceId = id("Workspace ID: ");
                    reserve(memberId,workspaceId,readTimes());
                } else if (c == "3") {
                    auto reservationId = id("Reservation ID: ");
                    auto rows = query("SELECT member_id,workspace_id FROM reservations WHERE id=?", {reservationId});
                    if (!rows->next()) throw std::runtime_error("Reservation not found.");
                    auto memberId = rows->getString(1).asStdString();
                    auto workspaceId = rows->getString(2).asStdString();
                    reserve(memberId,workspaceId,readTimes(),reservationId);
                } else if (c == "4") {
                    auto reservationId = id("Reservation ID: ");
                    std::cout << (change("DELETE FROM reservations WHERE id=?", {reservationId}) ? "Deleted.\n" : "Reservation not found.\n");
                } else if (c == "5") listReservations(required("ID, member or workspace: "));
                else if (c == "6") {
                    auto reservationId = id("Reservation ID: ");
                    auto status = required("New status (Checked In/Completed/Cancelled): ", 10);
                    std::string condition;
                    if (status == "Checked In") condition = "status='Booked' AND NOW()>=start_at AND NOW()<end_at";
                    else if (status == "Completed") condition = "status='Checked In' AND NOW()>=end_at";
                    else if (status == "Cancelled") condition = "status='Booked' AND start_at>NOW()";
                    else throw std::runtime_error("Invalid status.");
                    std::cout << (change("UPDATE reservations SET status=? WHERE id=? AND " + condition,
                                         {status,reservationId}) ? "Status updated.\n" : "Invalid transition or reservation not found.\n");
                }
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }

    void reports() {
        while (true) {
            std::cout << "\nREPORTS: 1 Daily check-ins and occupancy  2 Utilization ranking  3 Monthly revenue  0 Back\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") {
                    auto date = required("Date (YYYY-MM-DD): ", 10);
                    slot(date + " 08:00");
                    std::cout << "\nCheck-ins:\n";
                    auto rows = query("SELECT r.id,m.name,w.name,DATE_FORMAT(r.start_at,'%H:%i'),"
                                      "DATE_FORMAT(r.end_at,'%H:%i'),r.status FROM reservations r "
                                      "JOIN members m ON m.id=r.member_id JOIN workspaces w ON w.id=r.workspace_id "
                                      "WHERE DATE(r.start_at)=? AND r.status IN ('Checked In','Completed') ORDER BY r.start_at", {date});
                    printRows(*rows,{"ID","Member","Workspace","Start","End","Status"});
                    auto totals = query(
                        "SELECT COUNT(*),COALESCE(SUM(TIMESTAMPDIFF(HOUR,start_at,end_at)),0),"
                        "(SELECT COUNT(*) FROM workspaces WHERE status='Active') FROM reservations "
                        "WHERE DATE(start_at)=? AND status<>'Cancelled'", {date});
                    totals->next();
                    long long count = std::stoll(totals->getString(1).asStdString());
                    long long hours = std::stoll(totals->getString(2).asStdString());
                    long long spaces = std::stoll(totals->getString(3).asStdString());
                    double occupancy = spaces ? 100.0 * hours / (spaces * 12) : 0;
                    std::cout << "Bookings: " << count << "  Booked hours: " << hours
                              << "  Occupancy (08:00-20:00): " << std::fixed << std::setprecision(1)
                              << occupancy << "%\n";
                } else if (c == "2") {
                    auto month = required("Month (YYYY-MM): ", 7);
                    if (!std::regex_match(month,std::regex(R"(\d{4}-(0[1-9]|1[0-2]))"))) throw std::runtime_error("Invalid month.");
                    auto rows = query(
                        "SELECT w.name,w.type,COUNT(r.id),COALESCE(SUM(TIMESTAMPDIFF(HOUR,r.start_at,r.end_at)),0),"
                        "COALESCE(ROUND(100*SUM(TIMESTAMPDIFF(HOUR,r.start_at,r.end_at))/(12*DAY(LAST_DAY(CONCAT(?,'-01'))))),0) "
                        "FROM workspaces w LEFT JOIN reservations r ON r.workspace_id=w.id AND r.status<>'Cancelled' "
                        "AND DATE_FORMAT(r.start_at,'%Y-%m')=? GROUP BY w.id,w.name,w.type "
                        "ORDER BY 4 DESC,w.name", {month,month});
                    std::cout << std::left << std::setw(22) << "Workspace" << std::setw(18) << "Type"
                              << std::setw(10) << "Bookings" << std::setw(8) << "Hours" << "Utilization (each # = 5%)\n";
                    while (rows->next()) {
                        int percent = std::stoi(rows->getString(5).asStdString());
                        std::cout << std::left << std::setw(22) << rows->getString(1)
                                  << std::setw(18) << rows->getString(2) << std::setw(10) << rows->getString(3)
                                  << std::setw(8) << rows->getString(4) << std::string(std::min(20,percent/5),'#')
                                  << " " << percent << "%\n";
                    }
                } else if (c == "3") {
                    auto month = required("Month (YYYY-MM): ", 7);
                    if (!std::regex_match(month,std::regex(R"(\d{4}-(0[1-9]|1[0-2]))"))) throw std::runtime_error("Invalid month.");
                    auto rows = query(
                        "SELECT w.type,COUNT(r.id),COALESCE(SUM(r.total_cents),0),"
                        "COALESCE(ROUND(AVG(r.total_cents)),0) FROM workspaces w "
                        "LEFT JOIN reservations r ON r.workspace_id=w.id AND r.status='Completed' "
                        "AND DATE_FORMAT(r.start_at,'%Y-%m')=? GROUP BY w.type ORDER BY 3 DESC", {month});
                    std::cout << "Completed reservations only (revenue recognized on completion).\n";
                    while (rows->next()) {
                        std::string type = rows->getString(1).asStdString();
                        auto count = rows->getString(2).asStdString();
                        long long total = std::stoll(rows->getString(3).asStdString());
                        long long average = std::stoll(rows->getString(4).asStdString());
                        std::cout << std::left << std::setw(18) << type << " " << std::setw(8) << count
                                  << ringgit(total) << "  average " << ringgit(average) << '\n';
                    }
                    auto summary = query(
                        "SELECT COALESCE(SUM(total_cents),0),COUNT(*),"
                        "COALESCE((SELECT AVG(month_total) FROM (SELECT SUM(total_cents) month_total "
                        "FROM reservations WHERE status='Completed' GROUP BY DATE_FORMAT(start_at,'%Y-%m')) x),0) "
                        "FROM reservations WHERE status='Completed' AND DATE_FORMAT(start_at,'%Y-%m')=?", {month});
                    summary->next();
                    std::cout << "Total: " << ringgit(std::stoll(summary->getString(1).asStdString()))
                              << "  Completed: " << summary->getString(2)
                              << "  Average monthly revenue: "
                              << ringgit(std::stoll(summary->getString(3).asStdString())) << '\n';
                }
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }

    void memberMenu(const std::string& memberId) {
        while (true) {
            std::cout << "\nMEMBER: 1 Browse workspaces  2 My reservations  3 Book  4 Cancel  5 Edit profile  0 Log out\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") {
                    if (ask("Search a specific time slot? (yes/no): ") == "yes") listAvailable(readTimes());
                    else listWorkspaces(ask("Search name/type (blank for all): "),true);
                }
                else if (c == "2") listReservations("",memberId);
                else if (c == "3") reserve(memberId,id("Workspace ID: "),readTimes());
                else if (c == "4") {
                    auto reservationId = id("Reservation ID: ");
                    std::cout << (change("UPDATE reservations SET status='Cancelled' WHERE id=? AND member_id=? "
                                         "AND status='Booked' AND start_at>NOW()", {reservationId,memberId})
                                  ? "Cancelled.\n" : "Cannot cancel this reservation.\n");
                } else if (c == "5") {
                    auto name = required("Name: "); auto email = required("Email: ",254);
                    auto phone = required("Phone: ",30); emailAndPhone(email,phone);
                    change("UPDATE members SET name=?,email=?,phone=? WHERE id=?",{name,email,phone,memberId});
                    std::cout << "Profile updated.\n";
                }
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }

    void adminMenu() {
        while (true) {
            std::cout << "\nADMIN: 1 Members  2 Workspaces  3 Reservations  4 Reports  0 Log out\n";
            auto c = ask("> ");
            if (c == "0") return;
            if (c == "1") memberModule();
            else if (c == "2") workspaceModule();
            else if (c == "3") reservationModule();
            else if (c == "4") reports();
        }
    }

public:
    App() {
        auto driver = sql::mysql::get_driver_instance();
        db.reset(driver->connect(env("COWORK_DB_URL","tcp://127.0.0.1:3306"),
                                 env("COWORK_DB_USER","root"),env("COWORK_DB_PASSWORD")));
        db->setSchema("coworking");
    }

    void run() {
        while (true) {
            std::cout << "\nCO-WORKING SPACE RESERVATIONS\n1 Register  2 Member login  3 Administrator login  0 Exit\n";
            auto c = ask("> ");
            if (c == "0") return;
            try {
                if (c == "1") {
                    auto name = required("Name: "); auto email = required("Email: ",254);
                    auto phone = required("Phone: ",30);
                    auto password = requiredSecret("Password (8+ characters): ");
                    emailAndPhone(email,phone);
                    if (password.size() < 8) throw std::runtime_error("Password must have at least 8 characters.");
                    auto salt = randomSalt();
                    change("INSERT INTO members(name,email,phone,tier,password_salt,password_hash) "
                           "VALUES(?,?,?,'Basic',?,SHA2(CONCAT(?,?),256))",{name,email,phone,salt,salt,password});
                    std::cout << "Registration complete.\n";
                } else if (c == "2") {
                    auto email = required("Email: ",254); auto password = requiredSecret("Password: ");
                    auto memberId = one("SELECT id FROM members WHERE email=? AND password_hash=SHA2(CONCAT(password_salt,?),256)",
                                        {email,password});
                    if (memberId.empty()) std::cout << "Invalid credentials.\n";
                    else memberMenu(memberId);
                } else if (c == "3") {
                    auto configured = env("COWORK_ADMIN_PASSWORD");
                    if (configured.empty()) throw std::runtime_error("Set COWORK_ADMIN_PASSWORD before administrator login.");
                    if (askSecret("Administrator password: ") == configured) adminMenu();
                    else std::cout << "Invalid credentials.\n";
                }
            } catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
        }
    }
};

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        auto a = calculate(1000,9,2,"Gold");
        auto b = calculate(1000,12,2,"Basic");
        if (a.base != 2000 || a.surcharge != 400 || a.discount != 240 || a.total != 2160 ||
            b.total != 2000 || ringgit(a.total) != "RM 21.60") return 1;
        std::cout << "Pricing self-test passed.\n";
        return 0;
    }
    try { App app; app.run(); }
    catch (const sql::SQLException& e) {
        std::cerr << "Database error: " << e.what() << " (SQL state " << e.getSQLState() << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
