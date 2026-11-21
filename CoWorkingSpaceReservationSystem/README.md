# Co-Working Space Desk & Facility Reservation System

A C++17 console application for a co-working operator and its members. It follows the submitted Workshop 1 proposal: three MySQL-backed CRUDS modules, safe booking, automatic charges, and analytical reports.

## What it does

- **Members:** register/login; administrators can create, list, update, delete, and search member profiles. Tiers are Basic, Silver, and Gold.
- **Workspaces:** administrators can create, list, update, delete, and search desks, private offices, and meeting rooms. Members can browse active workspaces or check availability for a time slot.
- **Reservations:** administrators can create, list, reschedule, delete, search, and change status. Members can book, view history, and cancel their own future bookings.
- **Reports:** daily check-ins and occupancy, monthly workspace ranking with a text bar chart, and monthly completed-reservation revenue. Queries use `COUNT`, `SUM`, `AVG`, `GROUP BY`, joins, and a subquery.

Booking hours are whole hours within **08:00-20:00** on one local calendar day. A **20% surcharge** applies to the hours **09:00-12:00** and **17:00-19:00**. Silver receives **5%** and Gold **10%** off the subtotal including surcharge. Charges are stored in integer cents at booking time, so later rate or tier changes do not rewrite old invoices. The system rejects overlapping non-cancelled reservations for the same workspace. A workspace row lock serializes competing bookings.

## Requirements

- MySQL Server 8.0.16 or newer, with InnoDB enabled
- MySQL Connector/C++ 8.3 or newer **with its classic/JDBC component**
- CMake 3.16 or newer and a C++17 compiler (Visual Studio 2019/2022 on Windows)

The current official connector CMake package exposes `mysql::concpp-jdbc`; the application uses `<mysql/jdbc.h>`. On Windows, use the matching Visual Studio build of Connector/C++ and make its runtime DLLs available beside the executable or on `PATH`.

## Set up the database

Run [schema.sql](schema.sql) in MySQL Workbench or the `mysql` client as a user permitted to create a database. It creates the `coworking` schema and its three tables. Re-running it preserves existing records.

For a separate application account, run the following in MySQL after replacing the example password:

```sql
CREATE USER 'coworking_app'@'localhost' IDENTIFIED BY 'replace-with-a-strong-password';
GRANT SELECT, INSERT, UPDATE, DELETE ON coworking.* TO 'coworking_app'@'localhost';
```

## Build

From this folder in PowerShell:

```powershell
cmake -S . -B build -Dmysql-concpp_DIR="C:/path/to/mysql-connector-c++"
cmake --build build --config Release
```

The connector directory is the extracted installation root containing `mysql-concpp-config.cmake`. If installed system-wide, the `-Dmysql-concpp_DIR=...` argument may be omitted.

## Run

In PowerShell, set the connection and administrator password for the current terminal session:

```powershell
$env:COWORK_DB_URL = 'tcp://127.0.0.1:3306'
$env:COWORK_DB_USER = 'coworking_app'
$env:COWORK_DB_PASSWORD = 'your-database-password'
$env:COWORK_ADMIN_PASSWORD = 'your-administrator-password'
./build/Release/coworking.exe
```

For a single-configuration generator, the executable may be `./build/coworking.exe`. The app defaults to `tcp://127.0.0.1:3306`, user `root`, and an empty database password if the database variables are unset. `COWORK_ADMIN_PASSWORD` must be set for administrator login. Passwords are never written to project files.

Use menu **3 Administrator login** to add workspaces and optionally members. A member can self-register from menu **1**. Bookings require a future start time; enter start and end in `YYYY-MM-DD HH:00` format. The price is shown for confirmation before saving.

## Verify

```powershell
./build/Release/coworking.exe --self-test
```

This checks surcharge and discount arithmetic without a database connection. CMake also provides `ctest --test-dir build -C Release --output-on-failure`. For an integration check, create one member and one active workspace; make a booking; try a second overlapping booking and confirm it is rejected; cancel the first and retry. During the reserved time, an administrator can check the member in; after the end time, mark the booking Completed and inspect the revenue report. Deleting a member or workspace with reservations is intentionally blocked by foreign keys; delete the associated reservations first if permanent removal is required.

## Scope notes

- Revenue reports count **Completed** reservations. Occupancy reports count all non-cancelled bookings. Occupancy uses 12 available hours per active workspace per day.
- The project is a local course application. Member passwords use salted SHA-256 in MySQL; a production service should use a dedicated password hashing scheme such as Argon2id and provide rate limiting. Password entry is hidden on Windows.
- Reports display in the console and do not export files, matching the proposal's text-based output scope.
