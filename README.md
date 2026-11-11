# Electronic Voting Machine (EVM) Simulation in C

A console-based Electronic Voting Machine (EVM) simulation written in **C99** using only the C Standard Library. Built as a 2nd-semester C programming project demonstrating structures, arrays, functions, input validation, and binary file handling with persistence.

## Features

- **Admin Panel (Password Protected):**
  - First-run admin password setup & password change
  - Session lockout after 3 consecutive failed login attempts
  - Add, remove (with permanent IDs), and view candidates
  - Register voters individually or by ID range, remove, and view voter status
  - Start, close, and reset elections (with strict `YES` confirmation)
  - Live turnout during voting and full results with tie, zero-vote, and NOTA handling after closing
- **Voter Ballot:**
  - Voter ID verification & one-voter-one-vote enforcement
  - Built-in **NOTA** ("None of the Above") option
  - Vote confirmation prompt and screen clearing after each vote
- **Reliability & Data Integrity:**
  - Strict `fgets` + `strtol` input handling (handles empty lines, long input, invalid numbers, CRLF, and EOF)
  - Field-by-field binary persistence (`admin.dat`, `election.dat`) with 32-bit FNV-1a checksums
  - Atomic `.tmp` / `.bak` saving, startup backup recovery, and `LOCKED` mode on corruption

## Project Files

- [`evm.c`](evm.c) — Complete C99 source code
- [`FINAL EVM DOCUMENTATION.md`](FINAL%20EVM%20DOCUMENTATION.md) — Full project specification, business rules, error catalog, and test cases

## Compile & Run

```bash
gcc -std=c99 -Wall -Wextra -pedantic evm.c -o evm
./evm
```

On Windows (MinGW):

```powershell
gcc -std=c99 -Wall -Wextra -pedantic evm.c -o evm.exe
.\evm.exe
```
