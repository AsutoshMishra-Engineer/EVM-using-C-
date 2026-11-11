#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

/* ==================== 1. CONSTANTS AND DATA TYPES ==================== */

#define MAX_CANDIDATES 10
#define MIN_CANDIDATES_TO_START 2
#define MAX_NAME_LEN 49
#define MAX_PARTY_LEN 29
#define MAX_VOTERS 1000
#define MIN_VOTER_ID 1
#define MAX_VOTER_ID 999999
#define MIN_PASS_LEN 6
#define MAX_PASS_LEN 20
#define MAX_LOGIN_ATTEMPTS 3
#define INPUT_BUF_SIZE 128

#define DATA_FILE "election.dat"
#define ADMIN_FILE "admin.dat"
#define ELECTION_MAGIC "EVM1"
#define ADMIN_MAGIC "EVMA"
#define FILE_VERSION 1

/* Fixed binary sizes derived from constants so struct padding never affects files */
#define HEADER_BYTES (4 + 4 + 4)
#define CAND_BYTES (4 + 4 + (MAX_NAME_LEN + 1) + (MAX_PARTY_LEN + 1))
#define VOTER_BYTES (4 + 4)
#define ELECTION_PAYLOAD_BYTES ((5 * 4) + (MAX_CANDIDATES * CAND_BYTES) + (MAX_VOTERS * VOTER_BYTES))
#define ELECTION_FILE_BYTES (HEADER_BYTES + ELECTION_PAYLOAD_BYTES)
#define ADMIN_PAYLOAD_BYTES (4 + (MAX_PASS_LEN + 1))
#define ADMIN_FILE_BYTES (HEADER_BYTES + ADMIN_PAYLOAD_BYTES)

typedef enum {
    NOT_STARTED = 0,
    IN_PROGRESS = 1,
    CLOSED = 2
} ElectionState;

typedef struct {
    int id;
    char name[MAX_NAME_LEN + 1];
    char party[MAX_PARTY_LEN + 1];
    int votes;
} Candidate;

typedef struct {
    int id;
    int has_voted;
} Voter;

typedef struct {
    ElectionState state;
    int next_candidate_id;
    int candidate_count;
    Candidate candidates[MAX_CANDIDATES];
    int nota_votes;
    int voter_count;
    Voter voters[MAX_VOTERS];
} Election;

typedef struct {
    Election election;
    char admin_password[MAX_PASS_LEN + 1];
    int failed_attempts;
    int admin_locked;
    int locked_mode;
} AppState;

/* ==================== 2. UTILITY FUNCTIONS ==================== */

/* Prints the standard Part K error message for the given code (enforces R-SYS-05, R-AUTH-09). */
void print_error(const char *code, int attempts_left) {
    if (strcmp(code, "E01") == 0) printf("Error [E01]: Invalid choice. Please try again.\n");
    else if (strcmp(code, "E02") == 0) printf("Error [E02]: Please enter a valid number.\n");
    else if (strcmp(code, "E03") == 0) printf("Error [E03]: Incorrect password. Attempts left: %d\n", attempts_left);
    else if (strcmp(code, "E04") == 0) printf("Error [E04]: Too many failed attempts. Admin access is blocked for this session.\n");
    else if (strcmp(code, "E05") == 0) printf("Error [E05]: Voter ID not found.\n");
    else if (strcmp(code, "E06") == 0) printf("Error [E06]: This voter has already voted.\n");
    else if (strcmp(code, "E07a") == 0) printf("Error [E07a]: Election has not started.\n");
    else if (strcmp(code, "E07b") == 0) printf("Error [E07b]: Election is closed.\n");
    else if (strcmp(code, "E08") == 0) printf("Error [E08]: Maximum number of candidates reached.\n");
    else if (strcmp(code, "E09") == 0) printf("Error [E09]: A candidate with this name already exists.\n");
    else if (strcmp(code, "E10") == 0) printf("Error [E10]: At least 2 candidates are required.\n");
    else if (strcmp(code, "E11") == 0) printf("Error [E11]: Could not save data. Vote not recorded.\n");
    else if (strcmp(code, "E12") == 0) printf("Error [E12]: Data file appears corrupted.\n");
    else if (strcmp(code, "E13") == 0) printf("Error [E13]: Action not allowed in current election state.\n");
    else if (strcmp(code, "E14") == 0) printf("Error [E14]: Voter ID must be between 1 and 999999.\n");
    else if (strcmp(code, "E15") == 0) printf("Error [E15]: This Voter ID is already registered.\n");
    else if (strcmp(code, "E16") == 0) printf("Error [E16]: Maximum number of voters reached.\n");
    else if (strcmp(code, "E17") == 0) printf("Error [E17]: Input cannot be empty, too long, or contain invalid characters.\n");
    else if (strcmp(code, "E18") == 0) printf("Error [E18]: Password must be 6-20 characters (no spaces) and differ from the current password.\n");
    else if (strcmp(code, "E19") == 0) printf("Error [E19]: At least 1 registered voter is required to start.\n");
    else if (strcmp(code, "E20") == 0) printf("Error [E20]: Data integrity error: vote totals do not match voter records.\n");
    else if (strcmp(code, "E21") == 0) printf("Error [E21]: System is locked due to data error. Admin recovery required.\n");
    else if (strcmp(code, "E22") == 0) printf("Error [E22]: Admin file is corrupted or missing. Delete the .dat files and restart.\n");
    else if (strcmp(code, "E23") == 0) printf("Error [E23]: Could not save data. Change not recorded.\n");
    else if (strcmp(code, "E24") == 0) printf("Error [E24]: Candidate ID not found.\n");
    else if (strcmp(code, "E25") == 0) printf("Error [E25]: There is nothing to remove.\n");
    else if (strcmp(code, "E26") == 0) printf("Error [E26]: Passwords do not match.\n");
}

/* Clears the terminal screen on Windows, Linux, and macOS (enforces R-SYS-01, R-VOT-09). */
void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

/* Reads one line safely, strips CRLF/LF, and drains only overlong lines (enforces R-INP-01, R-INP-02, R-SYS-03). */
int read_line(char *out, size_t out_size, int *eof) {
    char buf[INPUT_BUF_SIZE];
    char *nl;
    size_t len;
    int ch;

    if (fgets(buf, (int)sizeof(buf), stdin) == NULL) {
        *eof = 1;
        return -1;
    }

    nl = strchr(buf, '\n');
    if (nl == NULL) {
        /* Line was longer than our buffer, so discard the rest up to Enter or EOF */
        while ((ch = getchar()) != '\n' && ch != EOF) {}
        return 0;
    }

    *nl = '\0';
    len = (size_t)(nl - buf);
    if (len > 0 && buf[len - 1] == '\r') {
        buf[--len] = '\0';
    }
    if (out_size == 0 || len >= out_size) {
        return 0;
    }

    memcpy(out, buf, len + 1);
    return 1;
}

/* Pauses until the user presses Enter or EOF occurs (enforces R-VOT-09, R-SYS-03). */
void pause_prompt(int *eof) {
    char dummy[INPUT_BUF_SIZE];
    printf("Press Enter to continue...");
    fflush(stdout);
    (void)read_line(dummy, sizeof(dummy), eof);
}

/* Reads and strictly validates an integer with strtol (enforces R-INP-01, R-INP-02, R-INP-03, R-INP-08). */
int read_int(const char *prompt, int *out, int *eof) {
    char line[INPUT_BUF_SIZE];
    char *p;
    char *end;
    long val;
    int res;

    printf("%s", prompt);
    fflush(stdout);
    res = read_line(line, sizeof(line), eof);
    if (res == -1) return -1;
    if (res == 0) {
        print_error("E02", 0);
        return 0;
    }

    p = line;
    while (*p != '\0' && isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '+') {
        print_error("E02", 0);
        return 0;
    }

    errno = 0;
    val = strtol(p, &end, 10);
    if (end == p) {
        print_error("E02", 0);
        return 0;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) end++;

    if (*end != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
        print_error("E02", 0);
        return 0;
    }

    *out = (int)val;
    return 1;
}

/* Compares two strings without considering uppercase/lowercase (enforces R-CAN-04, R-INP-06). */
int equals_ignore_case(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        i++;
    }
    return (a[i] == '\0' && b[i] == '\0');
}

/* Trims leading/trailing spaces and reduces multiple spaces to one (enforces R-CAN-09). */
void normalise_text(const char *src, char *dst, size_t dst_size) {
    size_t i = 0, j = 0;
    int prev_space = 1;

    if (dst_size == 0) return;
    while (src[i] != '\0' && j + 1 < dst_size) {
        if (src[i] == ' ') {
            if (!prev_space) {
                dst[j++] = ' ';
                prev_space = 1;
            }
        } else {
            dst[j++] = src[i];
            prev_space = 0;
        }
        i++;
    }
    if (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

/* Checks that text is non-empty and has only letters, spaces, '-', '.', or '\'' (enforces R-CAN-10). */
int is_valid_text(const char *s) {
    size_t i;
    if (s[0] == '\0') return 0;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalpha(c) && c != ' ' && c != '-' && c != '.' && c != '\'') {
            return 0;
        }
    }
    return 1;
}

/* Keeps asking until the user enters y/yes or n/no, or hits EOF (enforces R-INP-06, R-SYS-03). */
int read_yes_no(const char *prompt, int *eof) {
    char line[INPUT_BUF_SIZE], norm[INPUT_BUF_SIZE];
    int res;

    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        res = read_line(line, sizeof(line), eof);
        if (res == -1) return -1;
        if (res == 1) {
            normalise_text(line, norm, sizeof(norm));
            if (equals_ignore_case(norm, "y") || equals_ignore_case(norm, "yes")) return 1;
            if (equals_ignore_case(norm, "n") || equals_ignore_case(norm, "no")) return 0;
        }
        printf("Please enter y/yes or n/no.\n");
    }
}

/* Reads a raw password without trimming and checks length and ASCII 33..126 if requested (enforces R-AUTH-08, R-INP-07). */
int read_password(const char *prompt, char *out, size_t out_size, int check_format, int *eof) {
    char line[INPUT_BUF_SIZE];
    size_t len, i;
    int res;

    printf("%s", prompt);
    fflush(stdout);
    res = read_line(line, sizeof(line), eof);
    if (res == -1) return -1;
    if (res == 0) {
        if (check_format) print_error("E18", 0);
        return 0;
    }

    len = strlen(line);
    if (check_format) {
        if (len < MIN_PASS_LEN || len > MAX_PASS_LEN) {
            print_error("E18", 0);
            return 0;
        }
        for (i = 0; i < len; i++) {
            unsigned char c = (unsigned char)line[i];
            if (c < 33 || c > 126) {
                print_error("E18", 0);
                return 0;
            }
        }
    }
    if (len >= out_size) return 0;
    memcpy(out, line, len + 1);
    return 1;
}

/* ==================== 3. PERSISTENCE FUNCTIONS ==================== */

/* Computes a 32-bit FNV-1a checksum over payload bytes (enforces Part J.1, R-INT-01). */
uint32_t compute_checksum(const unsigned char *data, size_t len) {
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Safely swaps temp file into target path, keeping a backup on Windows if needed (enforces Part J.2). */
int atomic_replace_file(const char *tmp, const char *target, const char *bak) {
#ifdef _WIN32
    FILE *fp = fopen(target, "rb");
    int had_target = 0;
    (void)remove(bak);
    if (fp != NULL) {
        fclose(fp);
        had_target = 1;
        if (rename(target, bak) != 0) {
            (void)remove(tmp);
            return 0;
        }
    }
    if (rename(tmp, target) != 0) {
        if (had_target) (void)rename(bak, target);
        (void)remove(tmp);
        return 0;
    }
    if (had_target) (void)remove(bak);
    return 1;
#else
    (void)bak;
    if (rename(tmp, target) != 0) {
        (void)remove(tmp);
        return 0;
    }
    return 1;
#endif
}

/* Writes a complete buffer to a .tmp file, checks fclose, and replaces the target (enforces Part J.2). */
int write_buffer_safely(const char *target, const unsigned char *buf, size_t len) {
    char tmp[64], bak[64];
    FILE *fp;

    snprintf(tmp, sizeof(tmp), "%s.tmp", target);
    snprintf(bak, sizeof(bak), "%s.bak", target);

    fp = fopen(tmp, "wb");
    if (fp == NULL) return 0;
    if (fwrite(buf, 1, len, fp) != len || fflush(fp) != 0) {
        fclose(fp);
        (void)remove(tmp);
        return 0;
    }
    if (fclose(fp) != 0) {
        (void)remove(tmp);
        return 0;
    }
    return atomic_replace_file(tmp, target, bak);
}

/* Restores missing .dat files from .bak and deletes any leftover .tmp files at startup (enforces R-INT-07). */
void restore_backup_files(void) {
    const char *files[2] = {DATA_FILE, ADMIN_FILE};
    int i;

    for (i = 0; i < 2; i++) {
        char tmp[64], bak[64];
        FILE *fp;

        snprintf(tmp, sizeof(tmp), "%s.tmp", files[i]);
        snprintf(bak, sizeof(bak), "%s.bak", files[i]);
        (void)remove(tmp);

        fp = fopen(files[i], "rb");
        if (fp != NULL) {
            fclose(fp);
        } else {
            fp = fopen(bak, "rb");
            if (fp != NULL) {
                fclose(fp);
                (void)rename(bak, files[i]);
            }
        }
    }
}

/* Packs Election fields into a fixed-size byte buffer with header and checksum (enforces Part J.1). */
void serialise_election(const Election *e, unsigned char *buf) {
    size_t pos = HEADER_BYTES;
    int32_t vals[5], ver = FILE_VERSION, id, v;
    uint32_t sum;
    int i;

    memset(buf, 0, ELECTION_FILE_BYTES);
    vals[0] = (int32_t)e->state;
    vals[1] = (int32_t)e->next_candidate_id;
    vals[2] = (int32_t)e->candidate_count;
    vals[3] = (int32_t)e->nota_votes;
    vals[4] = (int32_t)e->voter_count;
    memcpy(buf + pos, vals, sizeof(vals));
    pos += sizeof(vals);

    for (i = 0; i < MAX_CANDIDATES; i++) {
        if (i < e->candidate_count) {
            id = (int32_t)e->candidates[i].id;
            v = (int32_t)e->candidates[i].votes;
            memcpy(buf + pos, &id, 4);
            memcpy(buf + pos + 4, &v, 4);
            memcpy(buf + pos + 8, e->candidates[i].name, MAX_NAME_LEN + 1);
            memcpy(buf + pos + 8 + MAX_NAME_LEN + 1, e->candidates[i].party, MAX_PARTY_LEN + 1);
        }
        pos += CAND_BYTES;
    }

    for (i = 0; i < MAX_VOTERS; i++) {
        if (i < e->voter_count) {
            id = (int32_t)e->voters[i].id;
            v = (int32_t)e->voters[i].has_voted;
            memcpy(buf + pos, &id, 4);
            memcpy(buf + pos + 4, &v, 4);
        }
        pos += VOTER_BYTES;
    }

    sum = compute_checksum(buf + HEADER_BYTES, ELECTION_PAYLOAD_BYTES);
    memcpy(buf, ELECTION_MAGIC, 4);
    memcpy(buf + 4, &ver, 4);
    memcpy(buf + 8, &sum, 4);
}

/* Unpacks an election.dat buffer after verifying magic, version, and checksum (enforces Part J.1, R-INT-01). */
int deserialise_election(const unsigned char *buf, Election *e) {
    size_t pos = HEADER_BYTES;
    int32_t ver, vals[5], id, v;
    uint32_t stored_sum, calc_sum;
    int i;

    memcpy(&ver, buf + 4, 4);
    memcpy(&stored_sum, buf + 8, 4);
    calc_sum = compute_checksum(buf + HEADER_BYTES, ELECTION_PAYLOAD_BYTES);
    if (memcmp(buf, ELECTION_MAGIC, 4) != 0 || ver != FILE_VERSION || stored_sum != calc_sum) {
        return 0;
    }

    memset(e, 0, sizeof(*e));
    memcpy(vals, buf + pos, sizeof(vals));
    pos += sizeof(vals);
    if (vals[0] < NOT_STARTED || vals[0] > CLOSED ||
        vals[2] < 0 || vals[2] > MAX_CANDIDATES ||
        vals[4] < 0 || vals[4] > MAX_VOTERS) {
        return 0;
    }

    e->state = (ElectionState)vals[0];
    e->next_candidate_id = (int)vals[1];
    e->candidate_count = (int)vals[2];
    e->nota_votes = (int)vals[3];
    e->voter_count = (int)vals[4];

    for (i = 0; i < MAX_CANDIDATES; i++) {
        if (i < e->candidate_count) {
            memcpy(&id, buf + pos, 4);
            memcpy(&v, buf + pos + 4, 4);
            e->candidates[i].id = (int)id;
            e->candidates[i].votes = (int)v;
            memcpy(e->candidates[i].name, buf + pos + 8, MAX_NAME_LEN + 1);
            memcpy(e->candidates[i].party, buf + pos + 8 + MAX_NAME_LEN + 1, MAX_PARTY_LEN + 1);
        }
        pos += CAND_BYTES;
    }

    for (i = 0; i < MAX_VOTERS; i++) {
        if (i < e->voter_count) {
            memcpy(&id, buf + pos, 4);
            memcpy(&v, buf + pos + 4, 4);
            e->voters[i].id = (int)id;
            e->voters[i].has_voted = (int)v;
        }
        pos += VOTER_BYTES;
    }
    return 1;
}

/* Loads and validates admin.dat; returns 1 loaded, 0 missing/empty, -1 corrupted (enforces R-INT-04, R-INT-06). */
int load_admin(char *password_out) {
    unsigned char buf[ADMIN_FILE_BYTES + 1];
    FILE *fp;
    size_t n;
    int32_t ver, plen;
    uint32_t stored_sum;
    int i;

    errno = 0;
    fp = fopen(ADMIN_FILE, "rb");
    if (fp == NULL) {
#ifdef ENOENT
        if (errno != ENOENT) return -1;
#endif
        return 0;
    }
    n = fread(buf, 1, sizeof(buf), fp);
    if (ferror(fp)) { fclose(fp); return -1; }
    fclose(fp);

    if (n == 0) return 0;
    if (n != ADMIN_FILE_BYTES) return -1;

    memcpy(&ver, buf + 4, 4);
    memcpy(&stored_sum, buf + 8, 4);
    if (memcmp(buf, ADMIN_MAGIC, 4) != 0 || ver != FILE_VERSION ||
        stored_sum != compute_checksum(buf + HEADER_BYTES, ADMIN_PAYLOAD_BYTES)) {
        return -1;
    }

    memcpy(&plen, buf + HEADER_BYTES, 4);
    if (plen < MIN_PASS_LEN || plen > MAX_PASS_LEN) return -1;
    if (memchr(buf + HEADER_BYTES + 4, '\0', MAX_PASS_LEN + 1) == NULL) return -1;
    if ((int32_t)strlen((const char *)(buf + HEADER_BYTES + 4)) != plen) return -1;

    for (i = 0; i < plen; i++) {
        unsigned char c = buf[HEADER_BYTES + 4 + (size_t)i];
        if (c < 33 || c > 126) return -1;
    }

    memcpy(password_out, buf + HEADER_BYTES + 4, (size_t)plen + 1);
    return 1;
}

/* Saves admin.dat atomically with header and checksum (enforces Part J.2, R-SYS-02). */
int save_admin(const char *password) {
    unsigned char buf[ADMIN_FILE_BYTES];
    int32_t ver = FILE_VERSION;
    int32_t plen = (int32_t)strlen(password);
    uint32_t sum;

    memset(buf, 0, sizeof(buf));
    memcpy(buf + HEADER_BYTES, &plen, 4);
    memcpy(buf + HEADER_BYTES + 4, password, (size_t)plen + 1);

    sum = compute_checksum(buf + HEADER_BYTES, ADMIN_PAYLOAD_BYTES);
    memcpy(buf, ADMIN_MAGIC, 4);
    memcpy(buf + 4, &ver, 4);
    memcpy(buf + 8, &sum, 4);

    return write_buffer_safely(ADMIN_FILE, buf, sizeof(buf));
}

/* Validates all Election invariants; returns 0 OK, 1 E12 error, 2 E20 vote mismatch (enforces R-INT-01, R-CAST-05). */
int validate_election(const Election *e) {
    int i, j, sum_votes = 0, flagged = 0;

    if (e->state < NOT_STARTED || e->state > CLOSED ||
        e->candidate_count < 0 || e->candidate_count > MAX_CANDIDATES ||
        e->voter_count < 0 || e->voter_count > MAX_VOTERS ||
        e->nota_votes < 0 || e->next_candidate_id < 1) {
        return 1;
    }

    for (i = 0; i < e->candidate_count; i++) {
        size_t nlen, plen;
        if (e->candidates[i].id < 1 || e->candidates[i].id >= e->next_candidate_id || e->candidates[i].votes < 0) return 1;
        if (memchr(e->candidates[i].name, '\0', MAX_NAME_LEN + 1) == NULL ||
            memchr(e->candidates[i].party, '\0', MAX_PARTY_LEN + 1) == NULL) return 1;
        nlen = strlen(e->candidates[i].name);
        plen = strlen(e->candidates[i].party);
        if (nlen < 1 || nlen > MAX_NAME_LEN || plen < 1 || plen > MAX_PARTY_LEN) return 1;
        for (j = i + 1; j < e->candidate_count; j++) {
            if (e->candidates[i].id == e->candidates[j].id) return 1;
        }
        sum_votes += e->candidates[i].votes;
    }

    for (i = 0; i < e->voter_count; i++) {
        if (e->voters[i].id < MIN_VOTER_ID || e->voters[i].id > MAX_VOTER_ID) return 1;
        if (e->voters[i].has_voted != 0 && e->voters[i].has_voted != 1) return 1;
        for (j = i + 1; j < e->voter_count; j++) {
            if (e->voters[i].id == e->voters[j].id) return 1;
        }
        if (e->voters[i].has_voted == 1) flagged++;
    }

    return (sum_votes + e->nota_votes == flagged) ? 0 : 2;
}

/* Sets an Election struct to a clean NOT_STARTED state (enforces R-INT-05, R-RST-02). */
void init_election(Election *e) {
    memset(e, 0, sizeof(*e));
    e->state = NOT_STARTED;
    e->next_candidate_id = 1;
}

/* Saves election.dat atomically (enforces Part J.2, R-SYS-02). */
int save_election(const Election *e) {
    unsigned char buf[ELECTION_FILE_BYTES];
    serialise_election(e, buf);
    return write_buffer_safely(DATA_FILE, buf, sizeof(buf));
}

/* Loads election.dat or creates a fresh file; returns 1 OK, -1 locked mode, -2 fatal save error (enforces R-INT-01..05). */
int load_election(Election *e) {
    unsigned char buf[ELECTION_FILE_BYTES + 1];
    FILE *fp;
    size_t n;
    int vres;

    errno = 0;
    fp = fopen(DATA_FILE, "rb");
    if (fp == NULL) {
#ifdef ENOENT
        if (errno != ENOENT) { print_error("E12", 0); init_election(e); return -1; }
#endif
        init_election(e);
        if (!save_election(e)) { print_error("E23", 0); return -2; }
        return 1;
    }

    n = fread(buf, 1, sizeof(buf), fp);
    if (ferror(fp)) { fclose(fp); print_error("E12", 0); init_election(e); return -1; }
    fclose(fp);

    if (n == 0) {
        init_election(e);
        if (!save_election(e)) { print_error("E23", 0); return -2; }
        return 1;
    }
    if (n != ELECTION_FILE_BYTES || !deserialise_election(buf, e)) {
        print_error("E12", 0);
        init_election(e);
        return -1;
    }

    vres = validate_election(e);
    if (vres != 0) {
        print_error(vres == 2 ? "E20" : "E12", 0);
        init_election(e);
        return -1;
    }
    return 1;
}

/* ==================== 4. AUTH FUNCTIONS ==================== */

/* Prompts at first startup to create and save the admin password (enforces R-AUTH-05, R-AUTH-07, R-AUTH-08). */
int first_run_setup(AppState *app, int *eof) {
    char p1[MAX_PASS_LEN + 1], p2[MAX_PASS_LEN + 1];
    int res;

    printf("=== First-Run Admin Setup ===\n");
    printf("Create an admin password (6-20 characters, no spaces).\n");

    for (;;) {
        res = read_password("Enter new admin password: ", p1, sizeof(p1), 1, eof);
        if (res == -1) return -1;
        if (res == 0) continue;

        res = read_password("Confirm admin password: ", p2, sizeof(p2), 0, eof);
        if (res == -1) return -1;
        if (res == 0 || strcmp(p1, p2) != 0) {
            print_error("E26", 0);
            continue;
        }
        if (!save_admin(p1)) {
            print_error("E23", 0);
            return -2;
        }
        memcpy(app->admin_password, p1, strlen(p1) + 1);
        printf("Admin password saved.\n\n");
        return 1;
    }
}

/* Checks one admin password attempt and locks admin access after 3 consecutive failures (enforces R-AUTH-01..03, R-AUTH-09). */
int admin_login(AppState *app, int *eof) {
    char attempt[INPUT_BUF_SIZE];
    int res;

    if (app->admin_locked) {
        print_error("E04", 0);
        return 0;
    }
    res = read_password("Enter admin password: ", attempt, sizeof(attempt), 0, eof);
    if (res == -1) return -1;

    if (res == 1 && strcmp(attempt, app->admin_password) == 0) {
        app->failed_attempts = 0;
        return 1;
    }

    app->failed_attempts++;
    if (app->failed_attempts >= MAX_LOGIN_ATTEMPTS) {
        app->admin_locked = 1;
        print_error("E04", 0);
    } else {
        print_error("E03", MAX_LOGIN_ATTEMPTS - app->failed_attempts);
    }
    return 0;
}

/* Changes the admin password with validation and rollback if saving fails (enforces R-AUTH-05, R-AUTH-08, R-AUTH-10, R-SYS-02). */
void change_password(AppState *app, int *eof) {
    char np[MAX_PASS_LEN + 1], cp[MAX_PASS_LEN + 1], old_snap[MAX_PASS_LEN + 1];
    int res;

    res = read_password("Enter new password (6-20 chars, no spaces): ", np, sizeof(np), 1, eof);
    if (res != 1) return;
    if (strcmp(np, app->admin_password) == 0) {
        print_error("E18", 0);
        return;
    }

    res = read_password("Confirm new password: ", cp, sizeof(cp), 0, eof);
    if (res == -1) return;
    if (res == 0 || strcmp(np, cp) != 0) {
        print_error("E26", 0);
        return;
    }

    memcpy(old_snap, app->admin_password, sizeof(old_snap));
    memcpy(app->admin_password, np, strlen(np) + 1);
    if (!save_admin(app->admin_password)) {
        memcpy(app->admin_password, old_snap, sizeof(app->admin_password));
        print_error("E23", 0);
        return;
    }
    printf("Admin password updated.\n");
}

/* ==================== 5. CANDIDATE FUNCTIONS ==================== */

/* Reads, normalises, and validates candidate or party text up to max_len (enforces R-CAN-03, R-CAN-05, R-CAN-09, R-CAN-10). */
int read_candidate_field(const char *prompt, char *out, size_t max_len, int *eof) {
    char raw[INPUT_BUF_SIZE], norm[INPUT_BUF_SIZE];
    int res;

    printf("%s", prompt);
    fflush(stdout);
    res = read_line(raw, sizeof(raw), eof);
    if (res == -1) return -1;
    if (res == 0) { print_error("E17", 0); return 0; }

    normalise_text(raw, norm, sizeof(norm));
    if (strlen(norm) > max_len || !is_valid_text(norm)) {
        print_error("E17", 0);
        return 0;
    }
    snprintf(out, max_len + 1, "%s", norm);
    return 1;
}

/* Adds a unique candidate in NOT_STARTED state and saves (enforces R-CAN-01..06, R-CAN-08..10, R-SYS-02). */
void add_candidate(Election *e, int *eof) {
    char name[MAX_NAME_LEN + 1], party[MAX_PARTY_LEN + 1];
    Election snap;
    int i, res;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (e->candidate_count >= MAX_CANDIDATES) { print_error("E08", 0); return; }

    res = read_candidate_field("Enter candidate name: ", name, MAX_NAME_LEN, eof);
    if (res != 1) return;
    for (i = 0; i < e->candidate_count; i++) {
        if (equals_ignore_case(e->candidates[i].name, name)) {
            print_error("E09", 0);
            return;
        }
    }

    res = read_candidate_field("Enter party name (type Independent if none): ", party, MAX_PARTY_LEN, eof);
    if (res != 1) return;

    snap = *e;
    e->candidates[e->candidate_count].id = e->next_candidate_id;
    e->candidates[e->candidate_count].votes = 0;
    snprintf(e->candidates[e->candidate_count].name, MAX_NAME_LEN + 1, "%s", name);
    snprintf(e->candidates[e->candidate_count].party, MAX_PARTY_LEN + 1, "%s", party);
    e->candidate_count++;
    e->next_candidate_id++;

    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Candidate added with ID %d.\n", e->candidates[e->candidate_count - 1].id);
}

/* Removes a candidate by permanent ID after y/n confirmation (enforces R-CAN-01, R-CAN-06, R-CAN-07, R-SYS-02). */
void remove_candidate(Election *e, int *eof) {
    int id, idx = -1, confirm, i;
    Election snap;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (e->candidate_count == 0) { print_error("E25", 0); return; }
    if (read_int("Enter permanent Candidate ID to remove: ", &id, eof) != 1) return;

    for (i = 0; i < e->candidate_count; i++) {
        if (e->candidates[i].id == id) { idx = i; break; }
    }
    if (idx == -1) { print_error("E24", 0); return; }

    printf("Remove candidate [%d] %s (%s)?\n", e->candidates[idx].id, e->candidates[idx].name, e->candidates[idx].party);
    confirm = read_yes_no("Confirm removal (y/n): ", eof);
    if (confirm != 1) {
        if (confirm == 0) printf("Removal cancelled.\n");
        return;
    }

    snap = *e;
    for (i = idx; i < e->candidate_count - 1; i++) {
        e->candidates[i] = e->candidates[i + 1];
    }
    e->candidate_count--;
    memset(&e->candidates[e->candidate_count], 0, sizeof(Candidate));

    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Candidate removed.\n");
}

/* Prints all registered candidates without vote counts (enforces Part F.3, R-CAN-08). */
void view_candidates(const Election *e) {
    int i;
    if (e->candidate_count == 0) {
        printf("No candidates registered.\n");
        return;
    }
    printf("\n--- Registered Candidates (%d) ---\n", e->candidate_count);
    for (i = 0; i < e->candidate_count; i++) {
        printf("ID: %-4d | Name: %-25s | Party: %s\n",
               e->candidates[i].id, e->candidates[i].name, e->candidates[i].party);
    }
    printf("Builtin Option: NOTA (None of the Above)\n");
}

/* ==================== 6. VOTER FUNCTIONS ==================== */

/* Returns the array index of voter_id or -1 if not found (enforces R-VOT-01, R-VOT-03). */
int find_voter(const Election *e, int voter_id) {
    int i;
    for (i = 0; i < e->voter_count; i++) {
        if (e->voters[i].id == voter_id) return i;
    }
    return -1;
}

/* Registers a single voter in NOT_STARTED state and saves (enforces R-VOT-02, R-VOT-03, R-VOT-11, R-SYS-02). */
void register_voter(Election *e, int *eof) {
    int id;
    Election snap;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (e->voter_count >= MAX_VOTERS) { print_error("E16", 0); return; }
    if (read_int("Enter Voter ID (1-999999): ", &id, eof) != 1) return;
    if (id < MIN_VOTER_ID || id > MAX_VOTER_ID) { print_error("E14", 0); return; }
    if (find_voter(e, id) != -1) { print_error("E15", 0); return; }

    snap = *e;
    e->voters[e->voter_count].id = id;
    e->voters[e->voter_count].has_voted = 0;
    e->voter_count++;

    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Voter %d registered.\n", id);
}

/* Registers a range of voter IDs atomically in exact rule order (enforces R-VOT-02, R-VOT-03, R-VOT-11, R-SYS-02). */
void register_voter_range(Election *e, int *eof) {
    int start_id, end_id, id, new_count = 0, skipped = 0, added = 0;
    Election snap;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (read_int("Enter start Voter ID (1-999999): ", &start_id, eof) != 1) return;
    if (start_id < MIN_VOTER_ID || start_id > MAX_VOTER_ID) { print_error("E14", 0); return; }
    if (read_int("Enter end Voter ID (1-999999): ", &end_id, eof) != 1) return;
    if (end_id < MIN_VOTER_ID || end_id > MAX_VOTER_ID) { print_error("E14", 0); return; }
    if (start_id > end_id) { print_error("E01", 0); return; }

    if (end_id - start_id + 1 > MAX_VOTERS) { print_error("E16", 0); return; }

    for (id = start_id; id <= end_id; id++) {
        if (find_voter(e, id) == -1) new_count++;
        else skipped++;
    }
    if (e->voter_count + new_count > MAX_VOTERS) { print_error("E16", 0); return; }

    snap = *e;
    for (id = start_id; id <= end_id; id++) {
        if (find_voter(e, id) == -1) {
            e->voters[e->voter_count].id = id;
            e->voters[e->voter_count].has_voted = 0;
            e->voter_count++;
            added++;
        }
    }

    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Registered %d new voter(s), skipped %d already registered.\n", added, skipped);
}

/* Removes a registered voter in NOT_STARTED state after confirmation (enforces R-VOT-12, R-SYS-02). */
void remove_voter(Election *e, int *eof) {
    int id, idx, confirm, i;
    Election snap;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (e->voter_count == 0) { print_error("E25", 0); return; }
    if (read_int("Enter Voter ID to remove: ", &id, eof) != 1) return;
    if (id < MIN_VOTER_ID || id > MAX_VOTER_ID) { print_error("E14", 0); return; }

    idx = find_voter(e, id);
    if (idx == -1) { print_error("E05", 0); return; }

    printf("Remove Voter ID %d?\n", id);
    confirm = read_yes_no("Confirm removal (y/n): ", eof);
    if (confirm != 1) {
        if (confirm == 0) printf("Removal cancelled.\n");
        return;
    }

    snap = *e;
    for (i = idx; i < e->voter_count - 1; i++) {
        e->voters[i] = e->voters[i + 1];
    }
    e->voter_count--;
    memset(&e->voters[e->voter_count], 0, sizeof(Voter));

    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Voter removed.\n");
}

/* Lists registered voter IDs and Voted: Yes/No without showing choices (enforces R-VOT-10, Part F.3). */
void view_voters(const Election *e) {
    int i;
    if (e->voter_count == 0) {
        printf("No voters registered.\n");
        return;
    }
    printf("\n--- Registered Voters (%d) ---\n", e->voter_count);
    for (i = 0; i < e->voter_count; i++) {
        printf("Voter ID: %-6d | Voted: %s\n", e->voters[i].id, e->voters[i].has_voted ? "Yes" : "No");
    }
}

/* ==================== 7. VOTING FUNCTIONS ==================== */

/* Displays serials 1..N plus NOTA (N+1) and loops until a valid ballot choice is entered (enforces R-VOT-06). */
int show_ballot(const Election *e, int *eof) {
    int i, choice, nota = e->candidate_count + 1;

    printf("\n=== BALLOT ===\n");
    for (i = 0; i < e->candidate_count; i++) {
        printf("%2d. %s (%s)\n", i + 1, e->candidates[i].name, e->candidates[i].party);
    }
    printf("%2d. NOTA (None of the Above)\n", nota);

    for (;;) {
        int res = read_int("Select your choice: ", &choice, eof);
        if (res == -1) return -1;
        if (res == 0) continue;
        if (choice < 1 || choice > nota) {
            print_error("E01", 0);
            continue;
        }
        return choice;
    }
}

/* Applies a vote and voter flag together, saves immediately, and rolls back on failure (enforces R-CAST-01..05, R-VOT-08). */
int cast_vote(Election *e, int voter_idx, int choice) {
    Election snap = *e;

    if (choice == e->candidate_count + 1) e->nota_votes++;
    else e->candidates[choice - 1].votes++;
    e->voters[voter_idx].has_voted = 1;

    if (!save_election(e)) {
        *e = snap;
        print_error("E11", 0);
        return 0;
    }
    return 1;
}

/* Runs voter login, ballot selection, confirmation, save, pause, and screen clear (enforces R-VOT-01..09). */
void voter_login_and_vote(Election *e, int *eof) {
    int id, idx, choice, confirm;

    if (e->state == NOT_STARTED) { print_error("E07a", 0); return; }
    if (e->state == CLOSED) { print_error("E07b", 0); return; }

    if (read_int("Enter your Voter ID (1-999999): ", &id, eof) != 1) return;
    if (id < MIN_VOTER_ID || id > MAX_VOTER_ID) { print_error("E14", 0); return; }

    idx = find_voter(e, id);
    if (idx == -1) { print_error("E05", 0); return; }
    if (e->voters[idx].has_voted) { print_error("E06", 0); return; }

    choice = show_ballot(e, eof);
    if (choice == -1) return;

    if (choice == e->candidate_count + 1) {
        printf("You selected: NOTA (None of the Above)\n");
    } else {
        printf("You selected: %s (%s)\n", e->candidates[choice - 1].name, e->candidates[choice - 1].party);
    }

    confirm = read_yes_no("Confirm your vote? This cannot be undone (y/n): ", eof);
    if (confirm == -1) return;
    if (confirm == 0) {
        printf("Vote cancelled. Nothing was saved.\n");
    } else if (cast_vote(e, idx, choice)) {
        printf("Your vote has been recorded. Thank you for voting.\n");
    }

    pause_prompt(eof);
    if (!*eof) clear_screen();
}

/* ==================== 8. ELECTION CONTROL FUNCTIONS ==================== */

/* Starts the election if at least 2 candidates and 1 voter exist (enforces R-ELE-01, R-ELE-02, R-SYS-02). */
void start_election(Election *e) {
    Election snap;

    if (e->state != NOT_STARTED) { print_error("E13", 0); return; }
    if (e->candidate_count < MIN_CANDIDATES_TO_START) { print_error("E10", 0); return; }
    if (e->voter_count < 1) { print_error("E19", 0); return; }

    snap = *e;
    e->state = IN_PROGRESS;
    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Election is now IN PROGRESS.\n");
}

/* Permanently closes an active election after y/n confirmation (enforces R-ELE-03, R-ELE-04, R-SYS-02). */
void close_election(Election *e, int *eof) {
    int confirm;
    Election snap;

    if (e->state != IN_PROGRESS) { print_error("E13", 0); return; }
    confirm = read_yes_no("Are you sure you want to close the election? This is permanent (y/n): ", eof);
    if (confirm != 1) {
        if (confirm == 0) printf("Close cancelled.\n");
        return;
    }

    snap = *e;
    e->state = CLOSED;
    if (!save_election(e)) {
        *e = snap;
        print_error("E23", 0);
        return;
    }
    printf("Election is now CLOSED.\n");
}

/* Resets all election data after exact uppercase YES while keeping admin password (enforces R-RST-01..04, R-INT-02, R-SYS-02). */
int reset_election(AppState *app, int *eof) {
    char line[INPUT_BUF_SIZE];
    Election snap;
    int res;

    printf("WARNING: Reset is IRREVERSIBLE. All candidates, voters, and votes will be deleted.\n");
    printf("Type YES (exact uppercase) to confirm reset: ");
    fflush(stdout);

    res = read_line(line, sizeof(line), eof);
    if (res == -1) return -1;
    if (res != 1 || strcmp(line, "YES") != 0) {
        printf("Reset cancelled.\n");
        return 0;
    }

    snap = app->election;
    init_election(&app->election);
    if (!save_election(&app->election)) {
        app->election = snap;
        print_error("E23", 0);
        return 0;
    }
    app->locked_mode = 0;
    printf("Election has been reset to NOT_STARTED.\n");
    return 1;
}

/* ==================== 9. RESULT FUNCTIONS ==================== */

/* Prints overall turnout percentage without per-candidate counts (enforces R-RES-01, R-RES-08). */
void show_turnout(const Election *e) {
    int i, voted = 0;
    double turnout = 0.0;

    for (i = 0; i < e->voter_count; i++) {
        if (e->voters[i].has_voted) voted++;
    }
    if (e->voter_count > 0) {
        turnout = (voted * 100.0) / e->voter_count;
    }
    printf("Votes cast: %d / %d (%.2f%%)\n", voted, e->voter_count, turnout);
}

/* Displays full CLOSED results, percentages, winner, tie, and NOTA notes (enforces R-RES-01..09). */
void show_results(const Election *e) {
    int i, total_votes = e->nota_votes, voted = 0, max_votes = 0, ties = 0;
    double turnout = 0.0, nota_pct = 0.0;

    if (e->state == NOT_STARTED) { print_error("E13", 0); return; }
    if (e->state == IN_PROGRESS) { show_turnout(e); return; }

    for (i = 0; i < e->candidate_count; i++) {
        total_votes += e->candidates[i].votes;
        if (e->candidates[i].votes > max_votes) max_votes = e->candidates[i].votes;
    }
    for (i = 0; i < e->voter_count; i++) {
        if (e->voters[i].has_voted) voted++;
    }

    printf("\n==================== ELECTION RESULTS ====================\n");
    for (i = 0; i < e->candidate_count; i++) {
        double pct = (total_votes > 0) ? (e->candidates[i].votes * 100.0) / total_votes : 0.0;
        printf("%2d. %-22s (%-15s) : %5d votes (%.2f%%)\n",
               i + 1, e->candidates[i].name, e->candidates[i].party, e->candidates[i].votes, pct);
    }

    if (total_votes > 0) nota_pct = (e->nota_votes * 100.0) / total_votes;
    if (e->voter_count > 0) turnout = (voted * 100.0) / e->voter_count;
    printf("    %-40s : %5d votes (%.2f%%)\n", "NOTA (None of the Above)", e->nota_votes, nota_pct);
    printf("----------------------------------------------------------\n");
    printf("Total Votes Cast  : %d\n", total_votes);
    printf("Registered Voters : %d\n", e->voter_count);
    printf("Turnout           : %.2f%%\n", turnout);
    printf("----------------------------------------------------------\n");

    if (total_votes == 0) {
        printf("No votes cast. No winner declared.\n");
        return;
    }
    if (max_votes == 0) {
        printf("No candidate received any votes. NOTA received 100.00%% of votes. No winner declared.\n");
        return;
    }

    for (i = 0; i < e->candidate_count; i++) {
        if (e->candidates[i].votes == max_votes) ties++;
    }
    if (ties == 1) {
        for (i = 0; i < e->candidate_count; i++) {
            if (e->candidates[i].votes == max_votes) {
                printf("Winner: %s (%s) with %d votes.\n",
                       e->candidates[i].name, e->candidates[i].party, e->candidates[i].votes);
                break;
            }
        }
    } else {
        printf("Result is a TIE between:\n");
        for (i = 0; i < e->candidate_count; i++) {
            if (e->candidates[i].votes == max_votes) {
                printf("  - %s (%s) with %d votes\n",
                       e->candidates[i].name, e->candidates[i].party, e->candidates[i].votes);
            }
        }
    }
    if (e->nota_votes > max_votes) {
        printf("Note: NOTA received more votes than the leading candidate(s).\n");
    }
}

/* ==================== 10. MENU FUNCTIONS ==================== */

/* Displays and handles the Admin Menu until logout or EOF (enforces Part F.3, R-AUTH-06). */
void admin_menu(AppState *app, int *eof) {
    int choice, res;

    for (;;) {
        printf("\n=== ADMIN MENU ===\n");
        printf(" 1. Add candidate        2. Remove candidate     3. View candidates\n");
        printf(" 4. Register one voter   5. Register voter range 6. View voters\n");
        printf(" 7. Remove voter         8. Start election       9. Close election\n");
        printf("10. View results/turnout 11. Reset election      12. Change admin password\n");
        printf(" 0. Logout\n");

        res = read_int("Enter choice: ", &choice, eof);
        if (res == -1) return;
        if (res == 0) continue;

        switch (choice) {
            case 1: add_candidate(&app->election, eof); break;
            case 2: remove_candidate(&app->election, eof); break;
            case 3: view_candidates(&app->election); break;
            case 4: register_voter(&app->election, eof); break;
            case 5: register_voter_range(&app->election, eof); break;
            case 6: view_voters(&app->election); break;
            case 7: remove_voter(&app->election, eof); break;
            case 8: start_election(&app->election); break;
            case 9: close_election(&app->election, eof); break;
            case 10: show_results(&app->election); break;
            case 11: (void)reset_election(app, eof); break;
            case 12: change_password(app, eof); break;
            case 0: printf("Logged out.\n"); return;
            default: print_error("E01", 0); break;
        }
        if (*eof) return;
    }
}

/* Displays the restricted Locked Mode Menu for recovery reset only (enforces Part F.4, R-INT-02). */
void locked_mode_menu(AppState *app, int *eof) {
    int choice, res, auth;

    while (app->locked_mode) {
        printf("\n=== LOCKED MODE MENU ===\n");
        printf("1. Admin Login (Recovery Reset only)\n");
        printf("2. Voter Login\n");
        printf("3. Exit\n");

        res = read_int("Enter choice: ", &choice, eof);
        if (res == -1) return;
        if (res == 0) continue;

        if (choice == 1) {
            auth = admin_login(app, eof);
            if (auth == -1) return;
            if (auth == 1) {
                printf("\n--- Recovery Reset ---\n");
                if (reset_election(app, eof) == -1) return;
            }
        } else if (choice == 2) {
            print_error("E21", 0);
        } else if (choice == 3) {
            return;
        } else {
            print_error("E01", 0);
        }
    }
}

/* Displays and handles the Main Menu until Exit or EOF (enforces Part F.2, R-SYS-02, R-SYS-03). */
void main_menu(AppState *app, int *eof) {
    int choice, res, auth;

    for (;;) {
        printf("\n=== ELECTRONIC VOTING MACHINE ===\n");
        printf("1. Admin Login\n2. Voter Login\n3. Exit\n");

        res = read_int("Enter choice: ", &choice, eof);
        if (res == -1) return;
        if (res == 0) continue;

        if (choice == 1) {
            auth = admin_login(app, eof);
            if (auth == -1) return;
            if (auth == 1) admin_menu(app, eof);
        } else if (choice == 2) {
            voter_login_and_vote(&app->election, eof);
        } else if (choice == 3) {
            return;
        } else {
            print_error("E01", 0);
        }
        if (*eof) return;
    }
}

/* ==================== 11. MAIN ==================== */

/* Runs startup recovery, loads admin and election files, launches menus, and exits safely (enforces Part F.1, R-SYS-02, R-SYS-03). */
int main(void) {
    AppState app;
    int eof = 0, admin_res, elec_res;

    memset(&app, 0, sizeof(app));
    init_election(&app.election);
    restore_backup_files();

    admin_res = load_admin(app.admin_password);
    if (admin_res == -1) {
        print_error("E22", 0);
        return 1;
    }
    if (admin_res == 0) {
        int setup_res;
        FILE *fp = fopen(DATA_FILE, "rb");
        if (fp != NULL) {
            unsigned char b;
            size_t got = fread(&b, 1, 1, fp);
            fclose(fp);
            if (got > 0) {
                print_error("E22", 0);
                return 1;
            }
        }
        setup_res = first_run_setup(&app, &eof);
        if (setup_res == -1) return 0;
        if (setup_res != 1) return 1;
    }

    elec_res = load_election(&app.election);
    if (elec_res == -2) return 1;
    if (elec_res == -1) app.locked_mode = 1;

    if (app.locked_mode) locked_mode_menu(&app, &eof);
    if (!app.locked_mode && !eof) main_menu(&app, &eof);

    if (!app.locked_mode && !save_election(&app.election)) {
        print_error("E23", 0);
    }
    printf("Exiting EVM. Goodbye.\n");
    return 0;
}
