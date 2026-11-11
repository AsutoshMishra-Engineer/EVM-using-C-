# EVM in C: Final Specification and AI Code-Generation Prompt (v3.0, FINAL)

**How to use this file:** Paste the **entire file** into your AI tool. Part A is the instruction to the AI. Parts B to N are the specification it must follow. Every design choice is already decided, so nothing is left to guess. This version supersedes v2.0 and v2.1.

---

# PART A: INSTRUCTION TO THE AI

You are an expert C programmer. Write a **complete, compilable, console-based Electronic Voting Machine (EVM) simulation in C** that follows the specification below **exactly**.

**Hard requirements**

1. Language: C99, standard library only: `stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `errno.h`, `limits.h`, `stdint.h`. No third-party libraries and **no POSIX-only functions** (no `strcasecmp`, `strdup`, `getline`, `fileno`, `fsync`, `strtok_r`), because they are not available under `-std=c99`.
2. Must compile with **no warnings** using: `gcc -std=c99 -Wall -Wextra -pedantic evm.c -o evm`
3. Follow every rule ID (`R-xxx`) in this document. Do not add, remove, or change rules. If something seems ambiguous, follow the literal text of this specification and do not ask me questions.
4. Never use `gets()`, never use raw `scanf` for input, never use `strcpy`, `strcat`, or `sprintf`. Prefer `snprintf` or `memcpy` with explicit null-termination (avoid `strncpy` patterns that trigger `-Wstringop-truncation`). Cast to `unsigned char` before calling any `ctype.h` function (`isalpha`, `tolower`, and so on). Implement case-insensitive comparison manually with `tolower`.
5. **Zero global mutable state:** declare the `AppState` (which contains the `Election`, `admin_password`, `failed_attempts`, `admin_locked`, `locked_mode`) inside `main()` and pass pointers everywhere. Constants and read-only tables are allowed at file scope.
6. The program must **never crash, hang, or loop forever** on any input, including empty input, very long input, letters, symbols, negative numbers, overflow numbers, CRLF line endings, and EOF (`Ctrl+D` / `Ctrl+Z`).
7. Every function must have a one-line comment saying what it does and which rule IDs it enforces.
8. Organise the code into clearly separated functions (see Part L). A single `evm.c` file is acceptable, but keep the sections in the order given.
9. Print every error through one function, `print_error(code)`, using the exact messages in Part K.
10. If your output is cut off by a length limit, stop at a function boundary, write `CONTINUE`, and resume from the next function when asked, without repeating earlier code.

**Output format**

1. The complete source code in one code block.
2. A **compile and run** instruction line.
3. A **rule-to-function traceability table** (every `R-` ID mapped to the function that enforces it).
4. A list of any rule you could not implement and why (this should be empty).

---

# PART B: OVERVIEW AND KNOWN LIMITATIONS

A menu-driven EVM simulation. An **Admin** sets up the election and controls it. **Voters** cast exactly one vote each. Results are shown after the election closes. All data is saved to disk, so it survives a restart. This is a learning project, not a secure voting system.

**Known limitations (to be stated in the project report, not fixed in code):**

- The admin password is stored without encryption, and the terminal shows it while it is typed (standard C cannot hide input).
- The admin lockout (R-AUTH-03) resets when the program is restarted.
- Files are checksummed to detect accidental damage, not to stop deliberate tampering.
- Only one running copy of the program is supported (no file locking).
- If `admin.dat` is damaged or lost while election data exists, the data files must be deleted manually (R-INT-04, R-INT-06).

# PART C: ROLES

| Role | Access | Authentication |
|---|---|---|
| Admin | Setup, election control, results, reset | Password |
| Voter | Cast one vote | Registered Voter ID |

---

# PART D: DATA MODEL

## D.1 Candidate

| Field | Type | Rule |
|---|---|---|
| `id` | int | Permanent unique ID, assigned from `next_candidate_id`, starts at 1, **never reused** (R-CAN-06) |
| `name` | char[50] | 1 to 49 chars after normalisation |
| `party` | char[30] | 1 to 29 chars after normalisation |
| `votes` | int | Starts at 0, never negative |

Candidates are stored in an array in the order added. The **serial number** shown to voters is the array position + 1 (1 to N). The permanent `id` is shown only in admin screens, and the admin selects a candidate to remove by its permanent `id`.

## D.2 Voter

| Field | Type | Rule |
|---|---|---|
| `id` | int | 1 to 999999, unique |
| `has_voted` | int (0/1) | Starts 0, set to 1 once, only cleared by full reset (which deletes the voter registry) |

## D.3 NOTA

A built-in counter `nota_votes` (starts 0). It is always the **last ballot option (serial N+1)**. It cannot be edited or deleted, is not counted toward the candidate limit, and can never be declared the winner.

## D.4 Election state

`NOT_STARTED`, `IN_PROGRESS`, `CLOSED`. Allowed transitions only: `NOT_STARTED → IN_PROGRESS → CLOSED`. Return to `NOT_STARTED` only through Reset (R-RST-02).

## D.5 Structures

- **`Election`** (stored in `election.dat`): `state`, `next_candidate_id`, `candidate_count`, `candidates[10]`, `nota_votes`, `voter_count`, `voters[1000]`.
- **`AppState`** (in memory, passed by pointer): `Election election`, `char admin_password[MAX_PASS_LEN + 1]`, `int failed_attempts`, `int admin_locked`, `int locked_mode`.

**Total votes** is always defined as: sum of all candidate votes + `nota_votes`.

---

# PART E: CONSTANTS (use `#define`)

| Constant | Value |
|---|---|
| `MAX_CANDIDATES` | 10 (NOTA excluded) |
| `MIN_CANDIDATES_TO_START` | 2 |
| `MAX_NAME_LEN` | 49 (array size 50) |
| `MAX_PARTY_LEN` | 29 (array size 30) |
| `MAX_VOTERS` | 1000 |
| `MIN_VOTER_ID` / `MAX_VOTER_ID` | 1 / 999999 |
| `MIN_PASS_LEN` / `MAX_PASS_LEN` | 6 / 20 |
| `MAX_LOGIN_ATTEMPTS` | 3 |
| `INPUT_BUF_SIZE` | 128 |
| `DATA_FILE` | `"election.dat"` |
| `ADMIN_FILE` | `"admin.dat"` |
| `ELECTION_MAGIC` / `ADMIN_MAGIC` | `"EVM1"` / `"EVMA"` (4 bytes each, no terminator stored) |
| `FILE_VERSION` | 1 |

---

# PART F: STARTUP AND MENUS

## F.1 Startup (exact order)

1. **Recover backups** (R-INT-07).
2. **Admin file:**
   - If `admin.dat` exists and is not empty: load it. If invalid, print `E22` and exit (R-INT-04).
   - If `admin.dat` is missing or 0 bytes: if `election.dat` exists and is not 0 bytes, print `E22` and exit (R-INT-06). Otherwise run **first-run password setup** (R-AUTH-07).
3. **Election file:**
   - If `election.dat` is missing or 0 bytes: initialise a clean election and save it (R-INT-05). If saving fails, print `E23` and exit.
   - Otherwise load and validate it. If any check fails, set `locked_mode = 1` (R-INT-02).
4. If `locked_mode` is set, run `locked_mode_menu`. Otherwise run `main_menu`.

`admin.dat` is therefore always created **before** `election.dat` ever exists.

## F.2 Main Menu

| Option | Action |
|---|---|
| 1 | Admin Login |
| 2 | Voter Login (only when state is `IN_PROGRESS`, otherwise E07a / E07b) |
| 3 | Exit (save, then quit) |

## F.3 Admin Menu

| Option | Action | Allowed state |
|---|---|---|
| 1 | Add candidate | `NOT_STARTED` |
| 2 | Remove candidate (by permanent ID) | `NOT_STARTED` |
| 3 | View candidates (permanent ID, name, party; no vote counts) | Any |
| 4 | Register one voter | `NOT_STARTED` |
| 5 | Register voter range (start ID to end ID) | `NOT_STARTED` |
| 6 | View voters (ID and `Voted: Yes/No` only, never the choice) | Any |
| 7 | Remove voter | `NOT_STARTED` |
| 8 | Start election | `NOT_STARTED` |
| 9 | Close election | `IN_PROGRESS` |
| 10 | View results / turnout | See R-RES-01 |
| 11 | Reset election | Any |
| 12 | Change admin password | Any |
| 0 | Logout (back to Main Menu) | Any |

Any option used in the wrong state shows **E13** and changes nothing.

## F.4 Locked Mode Menu (only when `locked_mode = 1`)

| Option | Action |
|---|---|
| 1 | Admin Login. On success the **only** action offered is **Recovery Reset** (R-INT-02). No other admin function is available. |
| 2 | Voter Login: always shows E21 |
| 3 | Exit: quits **without saving** `election.dat` |

---

# PART G: RULES

## G.1 Authentication

| ID | Rule |
|---|---|
| R-AUTH-01 | Admin functions require the correct password. |
| R-AUTH-02 | Password comparison is case-sensitive. |
| R-AUTH-03 | After 3 consecutive wrong attempts, admin login is **locked for the rest of the program session**. Voters can still vote. The failed-attempt counter persists across returns to the Main Menu and resets to 0 **only** after a successful admin login. When the lock is active, choosing Admin Login shows E04 immediately and does not ask for a password. |
| R-AUTH-04 | The program never prints the password back and never writes it to any log. (Terminal echo while typing is an accepted limitation.) |
| R-AUTH-05 | A new password must be 6 to 20 characters, different from the current password (only when changing an existing password), and entered twice (the confirmation must match, otherwise E26). |
| R-AUTH-06 | Voters cannot reach any admin function. |
| R-AUTH-07 | **First run:** the password is created at startup per F.1. It is validated by R-AUTH-05 (length and confirmation, without the "differs from current" check) and R-AUTH-08, and saved to `admin.dat`. Invalid input or a mismatch shows E18 or E26 and **asks again**. EOF exits cleanly and creates no files. If saving fails, print E23 and exit. |
| R-AUTH-08 | Password characters: printable ASCII 33 to 126 only. **Spaces are not allowed.** Passwords are never trimmed. A password containing any other character is rejected with E18. |
| R-AUTH-09 | Each selection of Admin Login gives exactly **one** password attempt. A wrong password shows E03 (with attempts left). The third consecutive wrong attempt sets `admin_locked = 1` and shows E04. |
| R-AUTH-10 | **Change password** (admin option 12) asks for the new password and confirmation **once**. If invalid (E18) or mismatched (E26), it shows the error and returns to the Admin Menu with the old password unchanged. If saving fails, restore the old password in memory and show E23. |

## G.2 Candidates

| ID | Rule |
|---|---|
| R-CAN-01 | Add or remove candidates only in `NOT_STARTED`. |
| R-CAN-02 | Maximum 10 candidates (NOTA excluded). The 11th is rejected with E08. |
| R-CAN-03 | Name and party must not be empty or whitespace-only. **A blank party is rejected** (the admin must type `Independent`). Rejection uses E17. |
| R-CAN-04 | Duplicate names are rejected (E09). Comparison is done **after normalisation**, is case-insensitive, and compares the name only (not the party). |
| R-CAN-05 | Over-long names or parties are rejected, never truncated (E17). Lines exceeding `INPUT_BUF_SIZE` are rejected by `read_line`; otherwise `normalise_text` runs first and the normalised length must be 1 to 49 for name and 1 to 29 for party. |
| R-CAN-06 | Candidate IDs are never reused. After removal, IDs are not renumbered. `next_candidate_id` is saved to the file. |
| R-CAN-07 | Removing a candidate asks for the permanent ID (not found: E24), then a yes/no confirmation (R-INP-06). Removal keeps the array compact (shift remaining entries) but does **not** change anyone's permanent ID. With no candidates, show E25. |
| R-CAN-08 | Vote counts start at 0 and cannot be edited manually. |
| R-CAN-09 | **Normalisation:** trim leading and trailing spaces, collapse runs of internal spaces into one space. |
| R-CAN-10 | **Allowed characters** in name and party: letters (A–Z, a–z), single spaces, hyphen `-`, period `.`, apostrophe `'`. Digits, tabs, and all other symbols are rejected (E17). |

## G.3 Voters

| ID | Rule |
|---|---|
| R-VOT-01 | Only registered voters can vote (E05 otherwise). |
| R-VOT-02 | A voter ID must be an integer from 1 to 999999 (E14 otherwise). |
| R-VOT-03 | Duplicate voter IDs cannot be registered (E15). Registering when `voter_count` is already 1000 shows E16. |
| R-VOT-04 | A voter who has voted is refused (E06). |
| R-VOT-05 | Voting is allowed only when state is `IN_PROGRESS`. The state is checked at Main Menu option 2, **before** asking for a voter ID (E07a if not started, E07b if closed). |
| R-VOT-06 | The ballot lists candidates as serials **1 to N** (mapped internally to `candidates[serial-1]`), then **NOTA as serial N+1**. The voter must choose exactly one. Non-numeric input shows E02. Numbers `0`, negatives, and above N+1 show E01. In both cases the voter is asked again. There is no cancel option on the ballot. |
| R-VOT-07 | After choosing, the screen shows the selected option and asks for y/n confirmation (R-INP-06). Answering **no** discards the choice, nothing is saved, and the program waits for Enter, clears the screen (R-VOT-09), and returns to the **Main Menu**. |
| R-VOT-08 | A confirmed vote cannot be changed or withdrawn. |
| R-VOT-09 | After a vote (confirmed, failed save, or cancelled at confirmation), wait for the user to press Enter (`read_line`) so they can read the outcome, then call `clear_screen()` (R-SYS-01) so the next voter cannot see the choice. After a successful vote the message says only that the vote was recorded, never which option was chosen. EOF at this wait follows R-SYS-03. |
| R-VOT-10 | The program never displays which option a specific voter chose. `View voters` shows only `Voted: Yes/No`. |
| R-VOT-11 | Admin can register one voter (option 4) or a range (option 5) only in `NOT_STARTED`. **Range registration, in this order:** (1) both IDs must be within 1 to 999999 (E14 otherwise); (2) if `start > end`, reject with E01; (3) compute `range_size = end - start + 1`; if `range_size > MAX_VOTERS`, reject with E16 immediately (no loop is run); (4) count how many IDs in `[start, end]` are **not** already registered (`new_count`); if `voter_count + new_count > MAX_VOTERS`, reject the **whole operation** with E16 and add nothing; (5) otherwise add all unregistered IDs in `[start, end]` and report how many existing IDs were skipped. |
| R-VOT-12 | Removing a voter is allowed only in `NOT_STARTED`, requires confirmation, and the ID must exist (E05 otherwise). With no voters, show E25. |

## G.4 Vote casting

| ID | Rule |
|---|---|
| R-CAST-01 | A valid vote increments exactly one counter (a candidate or NOTA) by exactly 1. |
| R-CAST-02 | The vote counter and the voter's `has_voted` flag change **together**. Before changing, take an in-memory snapshot. |
| R-CAST-03 | After the change, save immediately (Part J). |
| R-CAST-04 | If saving fails, **restore the snapshot** (counter and flag back to previous values), show E11, and treat the vote as not recorded. |
| R-CAST-05 | At all times: candidate votes + NOTA votes = number of voters with `has_voted = 1`. |

## G.5 Election control

| ID | Rule |
|---|---|
| R-ELE-01 | Start requires at least 2 candidates (E10) and at least 1 registered voter (E19). |
| R-ELE-02 | After starting, candidates and voters cannot be added or removed (E13). |
| R-ELE-03 | Closing is permanent until a full reset. |
| R-ELE-04 | Closing asks for y/n confirmation. |
| R-ELE-05 | Voting after closing is rejected (E07b). |

## G.6 Results

| ID | Rule |
|---|---|
| R-RES-01 | **Option 10:** in `NOT_STARTED` → E13. In `IN_PROGRESS` → show **turnout only**: `Votes cast: X / Y (Z.ZZ%)`, with no per-candidate or NOTA counts. In `CLOSED` → show full results. |
| R-RES-02 | Full results show, for each candidate: serial, name, party, votes, and percentage. Then NOTA votes and percentage, total votes (as defined in D.5), registered voters, and turnout. Percentages are of total votes. |
| R-RES-03 | The winner is the candidate with the highest votes, but **only if that count is greater than 0**. |
| R-RES-04 | NOTA is shown but can never be the winner. If NOTA has more votes than the top candidate(s) (and at least one candidate has votes > 0), still report the top candidate(s) (including ties) and add the note *"NOTA received more votes than the leading candidate(s)."* |
| R-RES-05 | If two or more candidates share the highest count (and it is above 0), declare a **tie** and list all tied candidates. No random choice. |
| R-RES-06 | If total votes = 0, print *"No votes cast. No winner declared."* |
| R-RES-07 | Percentages use `double` arithmetic: `(votes * 100.0) / total_votes`, printed to 2 decimals. Never divide by zero (if the total is 0, print `0.00%`). |
| R-RES-08 | Turnout = `(voted_count * 100.0) / registered_count`, same zero-division protection. |
| R-RES-09 | **A candidate with 0 votes can never be a winner or a tied winner.** If every vote went to NOTA (all candidates at 0, NOTA > 0), print *"No candidate received any votes. NOTA received 100.00% of votes. No winner declared."* |

## G.7 Reset

| ID | Rule |
|---|---|
| R-RST-01 | Reset requires admin login and the admin must type exactly `YES` (**uppercase, exact match**). Anything else prints "Reset cancelled." and changes nothing. A warning that reset is irreversible is shown first (R-RST-03). |
| R-RST-02 | Reset **deletes all candidates, deletes the entire voter registry, sets NOTA votes to 0, resets `next_candidate_id` to 1, and sets state to `NOT_STARTED`**. The admin password is kept. It follows the snapshot and rollback rule of R-SYS-02. |
| R-RST-03 | Reset is irreversible. |
| R-RST-04 | The admin password is never cleared by reset. |

## G.8 Data integrity and corruption

| ID | Rule |
|---|---|
| R-INT-01 | On load, validate all of: file size is exactly the expected size (no truncation, no extra bytes), magic and version match, checksum matches, `state` is a valid enum value, `candidate_count` is `0..MAX_CANDIDATES`, `voter_count` is `0..MAX_VOTERS`, `nota_votes >= 0`, `next_candidate_id >= 1`, for every candidate: `memchr(name, '\0', sizeof(name)) != NULL`, `memchr(party, '\0', sizeof(party)) != NULL`, `strlen(name)` in `1..49`, `strlen(party)` in `1..29`, `votes >= 0`, `id >= 1`, `id < next_candidate_id`, no duplicate candidate IDs; for every voter: `id` in `1..999999`, `has_voted` is `0` or `1`, no duplicate voter IDs; and R-CAST-05 holds. Counts are validated **before** they are used as loop limits. |
| R-INT-02 | If any check fails (or the file exists but cannot be read): print E12 (or E20 for a vote-total mismatch) and enter **LOCKED mode**. In LOCKED mode: voting is disabled (E21), admin functions other than recovery are unavailable, **Exit and EOF must NOT call `save_election()`** (to avoid overwriting the damaged evidence), and the only allowed actions are those in F.4. **Recovery Reset:** after a successful admin login, the admin types exactly `YES` (as R-RST-01), the election is reset per R-RST-02, saved, and `locked_mode` is cleared. If saving fails, show E23, restore the memory snapshot, and remain locked. Failed admin logins in LOCKED mode follow R-AUTH-03. If the admin is locked out, restart the program to try again. |
| R-INT-03 | Corrupted data is never silently loaded or partially used. |
| R-INT-04 | If `admin.dat` exists but is invalid (bad file size, magic, version, checksum, password length outside `6..20`, missing null-terminator, or invalid password characters per R-AUTH-08), print E22 and exit. The user must delete the `.dat` files manually. |
| R-INT-05 | If `election.dat` does not exist (and no backup exists, see R-INT-07), start a clean election (state `NOT_STARTED`, `next_candidate_id = 1`, everything else empty) and create the file. If it exists but is 0 bytes, treat it as a clean start and initialise the file. |
| R-INT-06 | If `admin.dat` is missing or 0 bytes **and** `election.dat` exists with size greater than 0, print E22 and exit (this stops anyone from taking over an existing election by deleting `admin.dat`). |
| R-INT-07 | **Backup recovery at startup, before anything else:** for each of `election.dat` and `admin.dat`, if the target file is missing and `<file>.bak` exists, rename `<file>.bak` back to the target. Any leftover `<file>.tmp` files are deleted. |

## G.9 System rules

| ID | Rule |
|---|---|
| R-SYS-01 | `clear_screen()` is the only place that clears the screen: use `system("cls")` under `#ifdef _WIN32`, otherwise `system("clear")`. |
| R-SYS-02 | **Save on every mutation with snapshot rollback:** candidate add/remove, voter register/remove, start, close, vote, password change, reset. Each takes an in-memory snapshot (as in R-CAST-02/04), saves immediately, and rolls back in memory on failure (E11 for votes, E23 for all others). Exit also saves (unless in LOCKED mode). If the save at exit fails, print E23 and exit anyway. |
| R-SYS-03 | On input EOF (`fgets` returns NULL) at any prompt: save data (unless in LOCKED mode) and exit cleanly. Never loop. |
| R-SYS-04 | A failed or invalid input (or failed disk save) never changes stored data. |
| R-SYS-05 | Errors use the fixed messages in Part K. Informational messages (for example "Candidate added.", "Reset cancelled.", "Nothing to show.") may use any clear wording. |

---

# PART H: INPUT HANDLING RULES (apply to every prompt)

| ID | Rule |
|---|---|
| R-INP-01 | Read every input with `fgets` into a buffer of `INPUT_BUF_SIZE` (128). Never use raw `scanf`. After reading, remove the trailing `\n` and also a trailing `\r` (so CRLF input works). |
| R-INP-02 | If no `\n` is found in the buffer, the line was too long: discard the rest of the line up to `\n` or EOF (only in this case, otherwise the program would hang waiting for extra input). The over-long input is then rejected. |
| R-INP-03 | **Integer parsing** uses `strtol`: skip leading whitespace and reject trailing non-whitespace; reject empty input; the **whole** token must be consumed (`"5abc"` is invalid); a leading `+` is rejected; a leading `-` is parsed and then rejected by range checks; check `errno == ERANGE` and `val < INT_MIN` or `val > INT_MAX` before casting to `int`. |
| R-INP-04 | All array indexes are bounds-checked before use. |
| R-INP-05 | All string copies are length-limited and null-terminated. |
| R-INP-06 | **Yes/No prompts** accept `y`, `yes`, `n`, `no` (case-insensitive, after trimming surrounding spaces). Anything else re-asks. The **only** exception is Reset, which needs exact uppercase `YES` (R-RST-01). |
| R-INP-07 | Password input is read with the same line rules, but is never trimmed (R-AUTH-08). |
| R-INP-08 | **Which error to show for numbers:** if parsing fails (empty, non-numeric, trailing junk, leading `+`, overflow) → **E02**. If parsing succeeds but the value is outside the allowed range → the specific range error: menu or ballot choice → E01, voter ID → E14, range start greater than end → E01, candidate ID not found → E24, voter ID not registered → E05. |

---

# PART I: BOUNDARY CONDITIONS (must all behave as shown)

## I.1 Numbers

| Situation | Behaviour |
|---|---|
| Menu choice `"abc"`, decimals, empty, `+5` | E02, menu shown again |
| Menu choice out of range (for example `99`, `-1`) | E01, menu shown again |
| Voter ID `0`, negative, `1000000` | E14 |
| Voter ID huge overflow value, letters, `+5` | E02, no crash |
| Voter ID `1` and `999999` | Accepted |
| Ballot choice `0`, negative, above `N+1` | E01, asked again |
| Ballot choice letters | E02, asked again |
| Ballot choice `N+1` | NOTA |

## I.2 Counts

| Situation | Behaviour |
|---|---|
| 0 or 1 candidate at start | E10 |
| 2 candidates at start | Allowed |
| 10th candidate | Accepted. 11th rejected (E08) |
| 0 voters at start | E19 |
| 1000th voter accepted, 1001st rejected | E16 |
| Range larger than 1000 IDs (for example `1` to `999999`) | E16 immediately, no long loop |
| 0 votes cast at close | R-RES-06 |
| Remove from empty candidate list or empty voter list | E25, no change |
| Remove candidate with a non-existent ID | E24, list unchanged |
| Remove voter with a non-existent ID | E05, list unchanged |

## I.3 Strings

| Situation | Behaviour |
|---|---|
| Empty or spaces-only name or party | E17 |
| Name of 49 chars | Accepted |
| Name of 50+ chars | E17, no overflow, no leftover input |
| Party of 29 chars accepted, 30+ chars rejected | E17 |
| `"  Anil   Kumar  "` | Stored as `"Anil Kumar"` |
| `"ravi"` when `"RAVI"` exists | E09 |
| Name or party with digits or symbols other than `- . '` | E17 |
| Password 5 chars, 21+ chars, contains space | E18 |
| Password 6 and 20 chars (valid chars) | Accepted |
| Confirmation password differs | E26 |

## I.4 State

| Situation | Behaviour |
|---|---|
| Voter login in `NOT_STARTED` | E07a |
| Voter login in `CLOSED` | E07b |
| Add or remove candidate or voter in `IN_PROGRESS` or `CLOSED` | E13 |
| Start twice, close before start, close twice | E13 |
| Results in `NOT_STARTED` | E13 |
| Results in `IN_PROGRESS` | Turnout only |
| Program killed mid-election, then restarted | State, votes, and flags restored |

## I.5 Files

| Situation | Behaviour |
|---|---|
| `election.dat` missing or empty (admin file fine) | Clean start |
| `admin.dat` missing but `election.dat` has data | E22, exit |
| Truncated, extended, or edited file (size, magic, checksum, null-terminator, or field failure) | E12, LOCKED mode |
| Vote totals ≠ flagged voters | E20, LOCKED mode |
| Cannot write file on vote (directory read-only, disk full) | E11, snapshot restored, vote not recorded |
| Cannot write file on admin mutation | E23, snapshot restored, change not recorded |
| `fclose` fails | Treated as a write failure (E11 / E23) |
| Exit or EOF in LOCKED mode | Does NOT overwrite `election.dat` |
| `election.dat` missing but `election.dat.bak` present | Backup restored at startup (R-INT-07) |

---

# PART J: FILE STORAGE, FORMAT, AND ATOMIC SAVING

Two files only:

| File | Content |
|---|---|
| `election.dat` | Everything in D.5 (`state`, `next_candidate_id`, `candidate_count`, `candidates`, `nota_votes`, `voter_count`, `voters`) |
| `admin.dat` | Magic, version, password length, password, checksum |

## J.1 Binary format (deterministic, no raw struct dumps)

Do **not** write structs with a single `fwrite` (compiler padding makes the checksum unreliable). Instead:

1. Build the file in a fixed-size `unsigned char` memory buffer, writing each field one by one in the order below. Integers are `int32_t` copied with `memcpy` (native byte order is acceptable). Strings are written as their **full fixed array** (50 bytes for name, 30 for party) after the array was zero-filled with `memset` before copying.
2. Compute a **32-bit FNV-1a checksum** over the payload bytes.
3. Write the header (`magic` 4 bytes, `version` int32, `checksum` int32) followed by the payload.

**`election.dat` payload order:** `state`, `next_candidate_id`, `candidate_count`, `nota_votes`, `voter_count`; then **all 10 candidate slots** (`id`, `votes`, `name[50]`, `party[30]`); then **all 1000 voter slots** (`id`, `has_voted`). Unused slots are written as zeros. Because the layout is fixed, the expected file size is a compile-time constant (compute it from the constants with macros, never hardcode it).

**`admin.dat` payload order:** `password_length` (int32), then `password[MAX_PASS_LEN + 1]` zero-padded. Header uses `ADMIN_MAGIC`.

**Loading:** read the whole file into the buffer, verify the exact size (R-INT-01), verify magic, version, and checksum, and only then decode fields and run the remaining validation checks.

## J.2 Atomic save procedure (`save_election` / `save_admin`, using `atomic_replace_file`)

1. Write the full data to `<file>.tmp`.
2. Check every `fwrite` return value.
3. `fflush`, then `fclose`, **and check the return value of `fclose`**.
4. Replace the old file: on Linux/macOS use `rename(tmp, target)`. On Windows (where `rename` fails if the target exists): remove any `<file>.bak`, rename the existing `target` (if it exists) to `<file>.bak`; if this rename fails, remove `<file>.tmp` and report failure; then rename `<file>.tmp` to `target`; if this fails, rename `.bak` back to `target`, remove `<file>.tmp`, and report failure; on success remove `<file>.bak`.
5. On any failure remove the `.tmp` file, return failure, and leave the previous good file intact.

---

# PART K: ERROR CATALOG (use these exact codes and messages)

| Code | Situation | Message |
|---|---|---|
| E01 | Out-of-range menu, ballot, or range-order choice | `"Invalid choice. Please try again."` |
| E02 | Non-numeric or malformed number | `"Please enter a valid number."` |
| E03 | Wrong admin password | `"Incorrect password. Attempts left: N"` |
| E04 | Admin locked | `"Too many failed attempts. Admin access is blocked for this session."` |
| E05 | Voter ID not registered | `"Voter ID not found."` |
| E06 | Already voted | `"This voter has already voted."` |
| E07a | Election not started | `"Election has not started."` |
| E07b | Election closed | `"Election is closed."` |
| E08 | Candidate limit reached | `"Maximum number of candidates reached."` |
| E09 | Duplicate candidate | `"A candidate with this name already exists."` |
| E10 | Fewer than 2 candidates | `"At least 2 candidates are required."` |
| E11 | File save failed during vote | `"Could not save data. Vote not recorded."` |
| E12 | Corrupted data | `"Data file appears corrupted."` |
| E13 | Action not allowed in this state | `"Action not allowed in current election state."` |
| E14 | Voter ID out of range | `"Voter ID must be between 1 and 999999."` |
| E15 | Duplicate voter ID | `"This Voter ID is already registered."` |
| E16 | Voter capacity reached | `"Maximum number of voters reached."` |
| E17 | Invalid name or party | `"Input cannot be empty, too long, or contain invalid characters."` |
| E18 | Invalid password | `"Password must be 6-20 characters (no spaces) and differ from the current password."` |
| E19 | No registered voters | `"At least 1 registered voter is required to start."` |
| E20 | Vote total mismatch | `"Data integrity error: vote totals do not match voter records."` |
| E21 | Locked mode action | `"System is locked due to data error. Admin recovery required."` |
| E22 | Admin file corrupted or missing with data present | `"Admin file is corrupted or missing. Delete the .dat files and restart."` |
| E23 | File save failed during admin/setup action | `"Could not save data. Change not recorded."` |
| E24 | Candidate ID not found | `"Candidate ID not found."` |
| E25 | Nothing to remove | `"There is nothing to remove."` |
| E26 | Password confirmation mismatch | `"Passwords do not match."` |

---

# PART L: REQUIRED CODE STRUCTURE

Sections in this order inside the source file:

1. Includes, `#define` constants, `typedef`s (`Candidate`, `Voter`, `Election`, `ElectionState`, `AppState`)
2. Utility: `print_error`, `clear_screen`, `pause_prompt`, `read_line`, `read_int`, `read_yes_no`, `read_password`, `normalise_text`, `is_valid_text`, `equals_ignore_case`
3. Persistence: `compute_checksum`, `atomic_replace_file`, `restore_backup_files`, `serialise_election`, `deserialise_election`, `load_admin`, `save_admin`, `validate_election` (R-INT-01), `init_election`, `load_election`, `save_election`
4. Auth: `first_run_setup`, `admin_login`, `change_password`
5. Candidates: `add_candidate`, `remove_candidate`, `view_candidates`
6. Voters: `find_voter`, `register_voter`, `register_voter_range`, `remove_voter`, `view_voters`
7. Voting: `show_ballot`, `cast_vote`, `voter_login_and_vote`
8. Election control: `start_election`, `close_election`, `reset_election`
9. Results: `show_turnout`, `show_results`
10. Menus: `admin_menu`, `locked_mode_menu`, `main_menu`
11. `main` (follows the startup order in F.1)

---

# PART M: TEST CASES (the code must pass all)

| # | Test | Expected |
|---|---|---|
| T01 | Valid admin login | Admin menu opens |
| T02 | Wrong password x3 | E04, admin locked, voters still work |
| T03 | Add valid candidate | Added with new permanent ID |
| T04 | Add duplicate (different case or extra spaces) | E09 |
| T05 | Add 11th candidate | E08 |
| T06 | Start with 1 candidate | E10 |
| T07 | Start with no voters | E19 |
| T08 | Valid vote | Count +1, voter flagged, saved, wait for Enter, then screen cleared |
| T09 | Same voter twice | E06 |
| T10 | Unregistered ID | E05 |
| T11 | Voter login before start | E07a |
| T12 | Voter login after close | E07b |
| T13 | Ballot choice `0`, `-1`, `99`, `"abc"` | `0`, `-1`, `99` give E01, `"abc"` gives E02, asked again each time |
| T14 | Answer `"no"` at confirmation | No vote saved, no flag, wait for Enter, screen cleared, back to Main Menu |
| T15 | Close with zero votes | `"No votes cast. No winner declared."` |
| T16 | Two candidates tied at top (above 0) | Tie, both listed |
| T17 | NOTA has most votes, one clear top candidate | Top candidate shown with NOTA-more-votes note, NOTA never winner |
| T18 | Single vote total | `100.00%` for that option, no errors |
| T19 | Exit mid-election, relaunch | State, votes, flags restored |
| T20 | Delete all data files, relaunch | Clean start, first-run password setup |
| T21 | Hand-edit `election.dat` (or break a string null-terminator) | E12, LOCKED mode, no crash |
| T22 | Reset with `YES` | Candidates, voters, NOTA cleared, `next_candidate_id=1`, state `NOT_STARTED`, password kept |
| T23 | Reset with `yes`, `y`, or Enter | "Reset cancelled.", nothing changes |
| T24 | 500-character input | Rejected, no crash, no leftover input in next prompt |
| T25 | Check totals | Candidate votes + NOTA = flagged voters |
| T26 | First run with no data files | Password creation prompt with confirmation |
| T27 | Remove candidate (`y`, `n`) | `y` removes only that ID, IDs not shifted or reused; `n` keeps list |
| T28 | 49 and 50 char names, spaces-only name, 29 and 30 char parties | Limits exact (checked after normalisation), blanks and oversize rejected |
| T29 | Voter IDs `0`, `-1`, `1`, `999999`, `1000000`, `9999999999999`, duplicate | Only `1` and `999999` accepted (first time) |
| T30 | Change password: 5 chars, 21 chars, same as old, with space, valid | Invalid rejected (E18), valid saved and required next login |
| T31 | 2 wrong logins, back to Main Menu, 1 more wrong | Locked after the 3rd wrong overall, voter login still works |
| T32 | Invalid state actions (add in progress, close before start, start twice, results before start) | E13 each |
| T33 | Valid NOTA vote | NOTA +1, voter flagged, candidates unchanged |
| T34 | Only NOTA votes, then close | R-RES-09 message, no winner or tie |
| T35 | Make the data directory read-only (Linux/macOS: `chmod a-w .`) or fill the disk, then cast a vote | E11, vote not counted, memory and disk in sync |
| T36 | Edit voter flags so totals mismatch | E20, LOCKED mode |
| T37 | Candidates 1, 2, 3 added, 2 removed | Ballot shows serials 1, 2, NOTA=3, and serial 2 maps to candidate with permanent ID 3 |
| T38 | Option 10 during `IN_PROGRESS` | Turnout only, no counts |
| T39 | Range registration `1` to `1000`, then `1` to `1001` | First succeeds, second rejected (E16), nothing added |
| T40 | ID 5 exists, register `1` to `1000` | Existing skipped, count reported, 999 added (total 1000) |
| T41 | Candidate with digits in name (`"Ravi2"`) | E17 |
| T42 | EOF (`Ctrl+D` / `Ctrl+Z`) at any prompt, including the Enter-wait | Data saved (if not LOCKED), clean exit, no loop |
| T43 | LOCKED mode: voter login | E21 |
| T44 | `+5` entered as a menu choice | E02 |
| T45 | LOCKED mode: Exit or EOF | `election.dat` is NOT overwritten |
| T46 | Range registration with `start > end` (for example `10` to `5`) | E01, nothing added |
| T47 | Delete `admin.dat` but keep a non-empty `election.dat`, relaunch | E22, program exits, no file created or changed |
| T48 | EOF at the first-run password prompt | Clean exit, no files created |
| T49 | Range registration `1` to `999999` | E16 immediately (finishes in well under one second) |
| T50 | Remove candidate with non-existent ID, remove with an empty list | E24 and E25, nothing changes |
| T51 | Password confirmation does not match | E26. First run asks again, change-password returns to Admin Menu with old password kept |
| T52 | LOCKED mode: Admin Login, then Recovery Reset with `YES` | Only recovery is offered, election reset and saved, `locked_mode` cleared, Main Menu appears. Failed save gives E23 and stays locked |
| T53 | Input with CRLF line endings | Treated as normal input |
| T54 | Delete `election.dat` but leave `election.dat.bak`, relaunch | Backup restored, data intact |
| T55 | Add a candidate while the data directory is read-only | E23, in-memory list unchanged |
| T56 | Cut bytes off the end of `election.dat` | E12, LOCKED mode |
| T57 | Menu input `"abc"` versus `"99"` | `"abc"` gives E02, `"99"` gives E01 |
| T58 | 3rd wrong password, then choose Admin Login again | E04 on the 3rd attempt, then E04 immediately without a password prompt |

---

# PART N: FINAL CHECKLIST (the AI must confirm each)

- [ ] Compiles with `-std=c99 -Wall -Wextra -pedantic` and zero warnings
- [ ] No POSIX-only functions used
- [ ] Zero global mutable variables (`AppState` / `Election` passed by pointer)
- [ ] No `gets`, raw `scanf`, `strcpy`, `strcat`, `sprintf`
- [ ] Every `ctype.h` call casts to `unsigned char`
- [ ] Every rule ID is implemented and listed in the traceability table
- [ ] No division by zero anywhere
- [ ] No buffer or array overflow possible
- [ ] Input draining happens **only** when no newline was read, and a trailing `\r` is stripped
- [ ] Files are written field by field (no raw struct dumps) with an FNV-1a checksum and exact size check
- [ ] `fclose` return values are checked
- [ ] Save uses the temp-file procedure from J.2, and backups are recovered at startup
- [ ] Startup follows the exact order in F.1
- [ ] Snapshot and rollback work on save failure for votes and all admin mutations
- [ ] LOCKED mode Exit/EOF does not overwrite `election.dat`
- [ ] EOF never causes an infinite loop (including at the Enter-wait after voting)
- [ ] All 58 test cases pass
