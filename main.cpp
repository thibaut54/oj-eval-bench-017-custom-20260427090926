// Train Ticket Management System
// In-memory implementation. No STL containers. std::string only.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>

// =================== Helpers: parsing ===================

static inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Read a line into buf. Returns -1 on EOF.
static int read_line(char *buf, int max_len) {
    int n = 0;
    int c;
    while ((c = getchar_unlocked()) != EOF && c != '\n') {
        if (n + 1 < max_len) buf[n++] = (char)c;
    }
    if (c == EOF && n == 0) return -1;
    buf[n] = 0;
    return n;
}

// Tokenize args. After read_line, parse params -X value pairs.
// We'll just store positional in a generic parse function.

// =================== Custom string compare ===================
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// =================== Date handling ===================
// Dates are mm-dd in 2021. Range: June 1 to Aug 31. We use day-of-year offset from June 1.
// June=30 days, July=31, August=31. Total 92 days. Index 0..91.
// Trains can run up to 3 days, so we allow indices up to 91+3=94 for arrival times.
static int days_in_month_table[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Convert "mm-dd" to day index from June 1.
static int parse_date(const char *s) {
    int m = (s[0] - '0') * 10 + (s[1] - '0');
    int d = (s[3] - '0') * 10 + (s[4] - '0');
    int day = 0;
    // Sum days from June 1 (month 6, day 1) to (m, d)
    if (m < 6) {
        // Should not happen per spec
        return -1;
    }
    for (int i = 6; i < m; i++) day += days_in_month_table[i];
    day += (d - 1);
    return day;
}

// Convert day index from June 1 to mm-dd
static void format_date(int day, char *out) {
    int m = 6, d = day + 1;
    while (d > days_in_month_table[m]) { d -= days_in_month_table[m]; m++; }
    out[0] = '0' + m / 10;
    out[1] = '0' + m % 10;
    out[2] = '-';
    out[3] = '0' + d / 10;
    out[4] = '0' + d % 10;
    out[5] = 0;
}

// Time = hr:mi -> minutes from midnight (0..1439)
static int parse_time(const char *s) {
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    return h * 60 + m;
}

// Format absolute time: total_minutes from June 1 00:00 -> "mm-dd hr:mi"
static void format_datetime(int total_minutes, char *out) {
    int day = total_minutes / 1440;
    int min_in_day = total_minutes % 1440;
    int h = min_in_day / 60;
    int mn = min_in_day % 60;
    char db[8];
    format_date(day, db);
    out[0] = db[0]; out[1] = db[1]; out[2] = db[2]; out[3] = db[3]; out[4] = db[4];
    out[5] = ' ';
    out[6] = '0' + h / 10;
    out[7] = '0' + h % 10;
    out[8] = ':';
    out[9] = '0' + mn / 10;
    out[10] = '0' + mn % 10;
    out[11] = 0;
}

// =================== Hash function ===================
static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

// =================== User ===================
struct User {
    char username[24];
    char password[36];
    char name[24]; // up to 5 Chinese chars (15 bytes UTF-8) + null
    char mailAddr[36];
    int privilege;
    bool used;
};

// =================== Hash map (open addressing) for users ===================
// Capacity power of 2, linear probing. Keys = username string.
struct UserMap {
    int cap;
    int count;
    User *items; // items[i].used means slot occupied
    UserMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new User[cap];
        for (int i = 0; i < cap; i++) items[i].used = false;
    }
    void rehash() {
        int oldcap = cap;
        User *old = items;
        cap *= 2;
        items = new User[cap];
        for (int i = 0; i < cap; i++) items[i].used = false;
        count = 0;
        for (int i = 0; i < oldcap; i++) {
            if (old[i].used) {
                insert_internal(old[i]);
            }
        }
        delete[] old;
    }
    int find_slot(const char *key) const {
        uint32_t h = hash_str(key);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].username, key) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    User *get(const char *key) {
        int s = find_slot(key);
        if (s < 0) return nullptr;
        return &items[s];
    }
    void insert_internal(const User &u) {
        uint32_t h = hash_str(u.username);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) i = (i + 1) & mask;
        items[i] = u;
        items[i].used = true;
        count++;
    }
    bool insert(const User &u) {
        if ((count + 1) * 2 > cap) rehash();
        if (find_slot(u.username) >= 0) return false;
        insert_internal(u);
        return true;
    }
};

// =================== Logged-in users ===================
// Track by adding a flag in User. We can store in User struct, but rehash would lose state.
// Better: keep "logged in" as a separate hash set. Or just add bool to User.
// Adding bool to User. But after rehash, items get copied; bool is preserved. Good.

// Add login flag to User struct - but we already have only the fields... let me rework.
// Re-define User with login flag.
// Actually I need to redo. Let me restart the struct...

// =================== Train ===================
// Stations: array of fixed-size strings (UTF-8 up to ~30 bytes).
// stationNum up to 100.
struct StationName { char s[34]; }; // up to 10 Chinese chars (30 bytes) + null
struct Train {
    char trainID[24];
    int stationNum;
    int seatNum;
    StationName stations[100];
    int prices[100];           // prefix sums of prices: prices[0]=0, prices[i]=cum price to station i
    int startTime;             // minutes from midnight
    int leaveTimes[100];       // minutes after train start: leaveTimes[i] = minutes from start to leave station i
    int arriveTimes[100];      // minutes from start to arrive at station i (== leaveTimes[i-1] + travel[i-1])
    int saleStart;             // day index
    int saleEnd;               // day index (inclusive)
    char type;
    bool released;
    bool used;
    // Seats: 2D array [date_count][seg_count] - allocated only after release
    // date_count = saleEnd - saleStart + 1, seg_count = stationNum - 1
    int *seats;
};

struct TrainMap {
    int cap;
    int count;
    Train *items;
    TrainMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new Train[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].seats = nullptr; }
    }
    void rehash() {
        int oldcap = cap;
        Train *old = items;
        cap *= 2;
        items = new Train[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].seats = nullptr; }
        count = 0;
        for (int i = 0; i < oldcap; i++) {
            if (old[i].used) {
                insert_internal(old[i]);
            }
        }
        // We moved seats pointers; do not delete them.
        delete[] old;
    }
    int find_slot(const char *key) const {
        uint32_t h = hash_str(key);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].trainID, key) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    Train *get(const char *key) {
        int s = find_slot(key);
        if (s < 0) return nullptr;
        return &items[s];
    }
    void insert_internal(const Train &t) {
        uint32_t h = hash_str(t.trainID);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) i = (i + 1) & mask;
        items[i] = t;
        items[i].used = true;
        count++;
    }
    bool insert(const Train &t) {
        if ((count + 1) * 2 > cap) rehash();
        if (find_slot(t.trainID) >= 0) return false;
        insert_internal(t);
        return true;
    }
    bool erase(const char *key) {
        int s = find_slot(key);
        if (s < 0) return false;
        if (items[s].seats) { delete[] items[s].seats; items[s].seats = nullptr; }
        items[s].used = false;
        count--;
        // Re-insert subsequent entries in the cluster
        int mask = cap - 1;
        int i = (s + 1) & mask;
        while (items[i].used) {
            Train tmp = items[i];
            items[i].used = false;
            count--;
            insert_internal(tmp);
            i = (i + 1) & mask;
        }
        return true;
    }
};

// =================== Station -> trains-passing-through index ===================
// For query_ticket / query_transfer, we need trains by station.
// We map station-name string to a dynamic list of (trainID, station-index-in-train).
struct StationEntry {
    char trainID[24];
    int station_idx;
};
struct StationTrains {
    StationName name;
    StationEntry *list;
    int len;
    int cap;
    bool used;
};
struct StationMap {
    int cap;
    int count;
    StationTrains *items;
    StationMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new StationTrains[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].list = nullptr; items[i].len = 0; items[i].cap = 0; }
    }
    void rehash() {
        int oldcap = cap;
        StationTrains *old = items;
        cap *= 2;
        items = new StationTrains[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].list = nullptr; items[i].len = 0; items[i].cap = 0; }
        count = 0;
        for (int i = 0; i < oldcap; i++) {
            if (old[i].used) {
                insert_internal(old[i]);
            }
        }
        delete[] old;
    }
    int find_slot(const char *key) const {
        uint32_t h = hash_str(key);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].name.s, key) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    StationTrains *get_or_create(const char *key) {
        if ((count + 1) * 2 > cap) rehash();
        uint32_t h = hash_str(key);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].name.s, key) == 0) return &items[i];
            i = (i + 1) & mask;
        }
        // Create new
        items[i].used = true;
        strncpy(items[i].name.s, key, 33);
        items[i].name.s[33] = 0;
        items[i].len = 0;
        items[i].cap = 4;
        items[i].list = new StationEntry[4];
        count++;
        return &items[i];
    }
    StationTrains *find(const char *key) {
        int s = find_slot(key);
        if (s < 0) return nullptr;
        return &items[s];
    }
    void insert_internal(const StationTrains &st) {
        uint32_t h = hash_str(st.name.s);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) i = (i + 1) & mask;
        items[i] = st;
        items[i].used = true;
        count++;
    }
};

static void station_add_train(StationTrains *st, const char *trainID, int idx) {
    if (st->len == st->cap) {
        int nc = st->cap * 2;
        StationEntry *nl = new StationEntry[nc];
        for (int i = 0; i < st->len; i++) nl[i] = st->list[i];
        delete[] st->list;
        st->list = nl;
        st->cap = nc;
    }
    strncpy(st->list[st->len].trainID, trainID, 23);
    st->list[st->len].trainID[23] = 0;
    st->list[st->len].station_idx = idx;
    st->len++;
}

// =================== Orders ===================
// Per-user order list, doubly linked list (newest first).
// Order also referenced by per-train pending queue when status=pending.
enum OrderStatus { OS_SUCCESS = 0, OS_PENDING = 1, OS_REFUNDED = 2 };
struct Order {
    char trainID[24];
    char fromStation[34];
    char toStation[34];
    int from_idx;
    int to_idx;
    int day;          // train start day from June 1
    int num;          // tickets count
    int price;        // per ticket
    int leave_time;   // absolute minutes from June 1 00:00
    int arrive_time;  // absolute minutes from June 1 00:00
    int status;       // OrderStatus
    long long timestamp; // monotonic increment
    char username[24]; // owner
    Order *user_prev; // newer order
    Order *user_next; // older order
    Order *queue_next; // for per-train-day pending queue
};

// =================== Logged-in set ===================
// Using a separate hash set keyed by username.
struct LoginSet {
    int cap;
    int count;
    char (*items)[24]; // item[0]=='\0' means empty
    LoginSet() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new char[cap][24];
        for (int i = 0; i < cap; i++) items[i][0] = 0;
    }
    void rehash() {
        int oldcap = cap;
        char (*old)[24] = items;
        cap *= 2;
        items = new char[cap][24];
        for (int i = 0; i < cap; i++) items[i][0] = 0;
        count = 0;
        for (int i = 0; i < oldcap; i++) {
            if (old[i][0]) insert(old[i]);
        }
        delete[] old;
    }
    int find_slot(const char *k) const {
        uint32_t h = hash_str(k);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i][0]) {
            if (my_strcmp(items[i], k) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    bool contains(const char *k) const { return find_slot(k) >= 0; }
    bool insert(const char *k) {
        if ((count + 1) * 2 > cap) rehash();
        uint32_t h = hash_str(k);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i][0]) {
            if (my_strcmp(items[i], k) == 0) return false;
            i = (i + 1) & mask;
        }
        strncpy(items[i], k, 23);
        items[i][23] = 0;
        count++;
        return true;
    }
    bool erase(const char *k) {
        int s = find_slot(k);
        if (s < 0) return false;
        items[s][0] = 0;
        count--;
        int mask = cap - 1;
        int i = (s + 1) & mask;
        while (items[i][0]) {
            char tmp[24];
            strncpy(tmp, items[i], 24);
            items[i][0] = 0;
            count--;
            insert(tmp);
            i = (i + 1) & mask;
        }
        return true;
    }
};

// =================== User-orders index ===================
// Map username -> head of order list (newest first)
struct UserOrderHead {
    char username[24];
    Order *head;
    bool used;
};
struct UserOrderMap {
    int cap;
    int count;
    UserOrderHead *items;
    UserOrderMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new UserOrderHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head = nullptr; }
    }
    void rehash() {
        int oldcap = cap;
        UserOrderHead *old = items;
        cap *= 2;
        items = new UserOrderHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head = nullptr; }
        count = 0;
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
        delete[] old;
    }
    int find_slot(const char *k) const {
        uint32_t h = hash_str(k);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].username, k) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    UserOrderHead *get_or_create(const char *k) {
        if ((count + 1) * 2 > cap) rehash();
        uint32_t h = hash_str(k);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].username, k) == 0) return &items[i];
            i = (i + 1) & mask;
        }
        items[i].used = true;
        strncpy(items[i].username, k, 23);
        items[i].username[23] = 0;
        items[i].head = nullptr;
        count++;
        return &items[i];
    }
    UserOrderHead *find(const char *k) {
        int s = find_slot(k);
        if (s < 0) return nullptr;
        return &items[s];
    }
    void insert_internal(const UserOrderHead &h) {
        uint32_t hv = hash_str(h.username);
        int mask = cap - 1;
        int i = hv & mask;
        while (items[i].used) i = (i + 1) & mask;
        items[i] = h;
        items[i].used = true;
        count++;
    }
};

// =================== Per-train-day pending queue ===================
// Map (trainID, day) -> head of pending order queue (oldest first)
// We'll use a hash map keyed by trainID+day.
struct PendingHead {
    char trainID[24];
    int day;
    Order *head;
    Order *tail;
    bool used;
};
struct PendingMap {
    int cap;
    int count;
    PendingHead *items;
    PendingMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity;
        count = 0;
        items = new PendingHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head = nullptr; items[i].tail = nullptr; }
    }
    static uint32_t key_hash(const char *t, int d) {
        uint32_t h = hash_str(t);
        h ^= (uint32_t)d * 2654435761u;
        return h;
    }
    void rehash() {
        int oldcap = cap;
        PendingHead *old = items;
        cap *= 2;
        items = new PendingHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head = nullptr; items[i].tail = nullptr; }
        count = 0;
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
        delete[] old;
    }
    int find_slot(const char *t, int d) const {
        uint32_t h = key_hash(t, d);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (items[i].day == d && my_strcmp(items[i].trainID, t) == 0) return i;
            i = (i + 1) & mask;
        }
        return -1;
    }
    PendingHead *get_or_create(const char *t, int d) {
        if ((count + 1) * 2 > cap) rehash();
        uint32_t h = key_hash(t, d);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (items[i].day == d && my_strcmp(items[i].trainID, t) == 0) return &items[i];
            i = (i + 1) & mask;
        }
        items[i].used = true;
        strncpy(items[i].trainID, t, 23);
        items[i].trainID[23] = 0;
        items[i].day = d;
        items[i].head = nullptr;
        items[i].tail = nullptr;
        count++;
        return &items[i];
    }
    PendingHead *find(const char *t, int d) {
        int s = find_slot(t, d);
        if (s < 0) return nullptr;
        return &items[s];
    }
    void insert_internal(const PendingHead &h) {
        uint32_t hv = key_hash(h.trainID, h.day);
        int mask = cap - 1;
        int i = hv & mask;
        while (items[i].used) i = (i + 1) & mask;
        items[i] = h;
        items[i].used = true;
        count++;
    }
};

// =================== Globals ===================
static UserMap g_users;
static TrainMap g_trains;
static StationMap g_stations;
static LoginSet g_logged;
static UserOrderMap g_user_orders;
static PendingMap g_pending;
static long long g_timestamp = 0;

// =================== Argument parsing ===================
// Parse a command line into argv tokens (split by spaces).
// Returns count of tokens. tokens[i] points into the original buffer (which we modify).
static int tokenize(char *buf, char **tokens, int max_tokens) {
    int n = 0;
    int i = 0;
    while (buf[i]) {
        while (buf[i] == ' ' || buf[i] == '\t') i++;
        if (!buf[i]) break;
        if (n >= max_tokens) break;
        tokens[n++] = &buf[i];
        while (buf[i] && buf[i] != ' ' && buf[i] != '\t') i++;
        if (buf[i]) { buf[i] = 0; i++; }
    }
    return n;
}

// Parse args of form -<key> <value>. Sets keymap[key] = value pointer or null.
struct ParsedArgs {
    const char *vals[26]; // index by key letter a..z
    void clear() { for (int i = 0; i < 26; i++) vals[i] = nullptr; }
    const char *get(char k) const { return vals[k - 'a']; }
};

static void parse_args(char **tokens, int n_tokens, int start_idx, ParsedArgs &args) {
    args.clear();
    for (int i = start_idx; i + 1 < n_tokens; i += 2) {
        if (tokens[i][0] == '-' && tokens[i][1] >= 'a' && tokens[i][1] <= 'z' && tokens[i][2] == 0) {
            args.vals[tokens[i][1] - 'a'] = tokens[i + 1];
        }
    }
}

// Split "a|b|c|..." in-place. Returns count and pointers into the buffer.
static int split_pipe(char *s, char **out, int max_n) {
    int n = 0;
    out[n++] = s;
    while (*s) {
        if (*s == '|') {
            *s = 0;
            if (n < max_n) out[n++] = s + 1;
        }
        s++;
    }
    return n;
}

// =================== Commands ===================

static void cmd_add_user(const ParsedArgs &a) {
    const char *c = a.get('c');
    const char *u = a.get('u');
    const char *p = a.get('p');
    const char *n = a.get('n');
    const char *m = a.get('m');
    const char *g = a.get('g');
    if (!u || !p || !n || !m) { puts("-1"); return; }

    if (g_users.count == 0) {
        // First user: privilege 10
        User nu;
        nu.used = true;
        strncpy(nu.username, u, 23); nu.username[23] = 0;
        strncpy(nu.password, p, 35); nu.password[35] = 0;
        strncpy(nu.name, n, 23); nu.name[23] = 0;
        strncpy(nu.mailAddr, m, 35); nu.mailAddr[35] = 0;
        nu.privilege = 10;
        if (!g_users.insert(nu)) { puts("-1"); return; }
        puts("0");
        return;
    }

    if (!c || !g) { puts("-1"); return; }
    if (!g_logged.contains(c)) { puts("-1"); return; }
    User *cu = g_users.get(c);
    if (!cu) { puts("-1"); return; }
    int new_priv = atoi(g);
    if (new_priv < 0 || new_priv > 10) { puts("-1"); return; }
    if (new_priv >= cu->privilege) { puts("-1"); return; }

    User nu;
    nu.used = true;
    strncpy(nu.username, u, 23); nu.username[23] = 0;
    strncpy(nu.password, p, 35); nu.password[35] = 0;
    strncpy(nu.name, n, 23); nu.name[23] = 0;
    strncpy(nu.mailAddr, m, 35); nu.mailAddr[35] = 0;
    nu.privilege = new_priv;
    if (!g_users.insert(nu)) { puts("-1"); return; }
    puts("0");
}

static void cmd_login(const ParsedArgs &a) {
    const char *u = a.get('u');
    const char *p = a.get('p');
    if (!u || !p) { puts("-1"); return; }
    User *user = g_users.get(u);
    if (!user) { puts("-1"); return; }
    if (my_strcmp(user->password, p) != 0) { puts("-1"); return; }
    if (g_logged.contains(u)) { puts("-1"); return; }
    g_logged.insert(u);
    puts("0");
}

static void cmd_logout(const ParsedArgs &a) {
    const char *u = a.get('u');
    if (!u) { puts("-1"); return; }
    if (!g_logged.contains(u)) { puts("-1"); return; }
    g_logged.erase(u);
    puts("0");
}

static void cmd_query_profile(const ParsedArgs &a) {
    const char *c = a.get('c');
    const char *u = a.get('u');
    if (!c || !u) { puts("-1"); return; }
    if (!g_logged.contains(c)) { puts("-1"); return; }
    User *cu = g_users.get(c);
    User *uu = g_users.get(u);
    if (!cu || !uu) { puts("-1"); return; }
    if (cu->privilege <= uu->privilege && my_strcmp(c, u) != 0) { puts("-1"); return; }
    printf("%s %s %s %d\n", uu->username, uu->name, uu->mailAddr, uu->privilege);
}

static void cmd_modify_profile(const ParsedArgs &a) {
    const char *c = a.get('c');
    const char *u = a.get('u');
    const char *p = a.get('p');
    const char *n = a.get('n');
    const char *m = a.get('m');
    const char *g = a.get('g');
    if (!c || !u) { puts("-1"); return; }
    if (!g_logged.contains(c)) { puts("-1"); return; }
    User *cu = g_users.get(c);
    User *uu = g_users.get(u);
    if (!cu || !uu) { puts("-1"); return; }
    if (cu->privilege <= uu->privilege && my_strcmp(c, u) != 0) { puts("-1"); return; }
    int new_priv = uu->privilege;
    if (g) {
        new_priv = atoi(g);
        if (new_priv < 0 || new_priv > 10) { puts("-1"); return; }
        if (new_priv >= cu->privilege) { puts("-1"); return; }
    }
    if (p) { strncpy(uu->password, p, 35); uu->password[35] = 0; }
    if (n) { strncpy(uu->name, n, 23); uu->name[23] = 0; }
    if (m) { strncpy(uu->mailAddr, m, 35); uu->mailAddr[35] = 0; }
    uu->privilege = new_priv;
    printf("%s %s %s %d\n", uu->username, uu->name, uu->mailAddr, uu->privilege);
}

static void cmd_add_train(const ParsedArgs &a) {
    const char *i = a.get('i');
    const char *n = a.get('n');
    const char *m = a.get('m');
    const char *s = a.get('s');
    const char *p = a.get('p');
    const char *x = a.get('x');
    const char *t = a.get('t');
    const char *o = a.get('o');
    const char *d = a.get('d');
    const char *y = a.get('y');
    if (!i || !n || !m || !s || !p || !x || !t || !o || !d || !y) { puts("-1"); return; }

    int stationNum = atoi(n);
    int seatNum = atoi(m);
    if (stationNum < 2 || stationNum > 100 || seatNum <= 0) { puts("-1"); return; }

    if (g_trains.get(i)) { puts("-1"); return; }

    Train tr;
    tr.used = true;
    tr.released = false;
    tr.seats = nullptr;
    strncpy(tr.trainID, i, 23); tr.trainID[23] = 0;
    tr.stationNum = stationNum;
    tr.seatNum = seatNum;
    tr.type = y[0];

    // Stations
    {
        char buf[2048];
        strncpy(buf, s, 2047); buf[2047] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum) { puts("-1"); return; }
        for (int k = 0; k < cnt; k++) {
            strncpy(tr.stations[k].s, parts[k], 33); tr.stations[k].s[33] = 0;
        }
    }
    // Prices (cumulative)
    {
        char buf[1024];
        strncpy(buf, p, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum - 1) { puts("-1"); return; }
        tr.prices[0] = 0;
        for (int k = 0; k < cnt; k++) tr.prices[k + 1] = tr.prices[k] + atoi(parts[k]);
    }
    // Times
    int travel[100], stopover[100];
    {
        char buf[1024];
        strncpy(buf, t, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum - 1) { puts("-1"); return; }
        for (int k = 0; k < cnt; k++) travel[k] = atoi(parts[k]);
    }
    {
        char buf[1024];
        strncpy(buf, o, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (stationNum == 2) {
            // expect "_"
        } else {
            if (cnt != stationNum - 2) { puts("-1"); return; }
            for (int k = 0; k < cnt; k++) stopover[k] = atoi(parts[k]);
        }
    }
    tr.startTime = parse_time(x);
    tr.leaveTimes[0] = 0;
    tr.arriveTimes[0] = 0;
    int cur = 0;
    for (int k = 0; k < stationNum - 1; k++) {
        tr.arriveTimes[k + 1] = cur + travel[k];
        cur = tr.arriveTimes[k + 1];
        if (k + 1 < stationNum - 1) {
            tr.leaveTimes[k + 1] = cur + stopover[k];
            cur = tr.leaveTimes[k + 1];
        } else {
            tr.leaveTimes[k + 1] = cur; // terminal: same as arrive
        }
    }

    // Sale dates
    {
        char buf[16];
        strncpy(buf, d, 15); buf[15] = 0;
        char *parts[2];
        int cnt = split_pipe(buf, parts, 2);
        if (cnt != 2) { puts("-1"); return; }
        tr.saleStart = parse_date(parts[0]);
        tr.saleEnd = parse_date(parts[1]);
    }

    if (!g_trains.insert(tr)) { puts("-1"); return; }
    puts("0");
}

static void cmd_delete_train(const ParsedArgs &a) {
    const char *i = a.get('i');
    if (!i) { puts("-1"); return; }
    Train *t = g_trains.get(i);
    if (!t || t->released) { puts("-1"); return; }
    g_trains.erase(i);
    puts("0");
}

static void cmd_release_train(const ParsedArgs &a) {
    const char *i = a.get('i');
    if (!i) { puts("-1"); return; }
    Train *t = g_trains.get(i);
    if (!t || t->released) { puts("-1"); return; }
    t->released = true;
    int days = t->saleEnd - t->saleStart + 1;
    int segs = t->stationNum - 1;
    t->seats = new int[(long long)days * segs];
    for (int dd = 0; dd < days; dd++)
        for (int sg = 0; sg < segs; sg++)
            t->seats[dd * segs + sg] = t->seatNum;
    // Add to station index
    for (int k = 0; k < t->stationNum; k++) {
        StationTrains *st = g_stations.get_or_create(t->stations[k].s);
        station_add_train(st, t->trainID, k);
    }
    puts("0");
}

static void cmd_query_train(const ParsedArgs &a) {
    const char *i = a.get('i');
    const char *d = a.get('d');
    if (!i || !d) { puts("-1"); return; }
    Train *t = g_trains.get(i);
    if (!t) { puts("-1"); return; }
    int day = parse_date(d);
    if (day < t->saleStart || day > t->saleEnd) { puts("-1"); return; }
    int segs = t->stationNum - 1;
    printf("%s %c\n", t->trainID, t->type);
    int base_min = day * 1440 + t->startTime;
    for (int k = 0; k < t->stationNum; k++) {
        char arr_s[16] = {0}, lev_s[16] = {0};
        if (k == 0) {
            strcpy(arr_s, "xx-xx xx:xx");
        } else {
            int abs_t = base_min + t->arriveTimes[k];
            format_datetime(abs_t, arr_s);
        }
        if (k == t->stationNum - 1) {
            strcpy(lev_s, "xx-xx xx:xx");
        } else {
            int abs_t = base_min + t->leaveTimes[k];
            format_datetime(abs_t, lev_s);
        }
        int price = t->prices[k];
        if (k == t->stationNum - 1) {
            printf("%s %s -> %s %d x\n", t->stations[k].s, arr_s, lev_s, price);
        } else {
            int seat;
            if (t->released) {
                int day_idx = day - t->saleStart;
                seat = t->seats[day_idx * segs + k];
            } else {
                seat = t->seatNum;
            }
            printf("%s %s -> %s %d %d\n", t->stations[k].s, arr_s, lev_s, price, seat);
        }
    }
}

// Find index of station name in train (returns -1 if not found)
static int find_station_in_train(const Train *t, const char *name) {
    for (int k = 0; k < t->stationNum; k++) {
        if (my_strcmp(t->stations[k].s, name) == 0) return k;
    }
    return -1;
}

// Compute available seats from segment from_idx to to_idx-1 on day_idx
static int min_seats(const Train *t, int day_idx, int from_idx, int to_idx) {
    int segs = t->stationNum - 1;
    int mn = t->seatNum;
    for (int k = from_idx; k < to_idx; k++) {
        int v = t->seats[day_idx * segs + k];
        if (v < mn) mn = v;
    }
    return mn;
}

// Subtract `num` seats from segments [from_idx, to_idx)
static void take_seats(Train *t, int day_idx, int from_idx, int to_idx, int num) {
    int segs = t->stationNum - 1;
    for (int k = from_idx; k < to_idx; k++) t->seats[day_idx * segs + k] -= num;
}
static void return_seats(Train *t, int day_idx, int from_idx, int to_idx, int num) {
    int segs = t->stationNum - 1;
    for (int k = from_idx; k < to_idx; k++) t->seats[day_idx * segs + k] += num;
}

// Sort helper: insertion sort on small arrays
template<typename T, typename Cmp>
static void sort_arr(T *arr, int n, Cmp cmp) {
    for (int i = 1; i < n; i++) {
        T x = arr[i];
        int j = i - 1;
        while (j >= 0 && cmp(x, arr[j])) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = x;
    }
}

struct TicketResult {
    char trainID[24];
    int leave_time;     // absolute minutes
    int arrive_time;    // absolute minutes
    int duration;
    int price;
    int seat;
    int day_idx;        // train's day index from saleStart
    int from_idx, to_idx;
};

static void cmd_query_ticket(const ParsedArgs &a) {
    const char *s = a.get('s');
    const char *t = a.get('t');
    const char *d = a.get('d');
    const char *p = a.get('p');
    if (!s || !t || !d) { puts("-1"); return; }
    bool sort_by_time = true;
    if (p && my_strcmp(p, "cost") == 0) sort_by_time = false;

    int boarding_day = parse_date(d);
    StationTrains *sf = g_stations.find(s);
    StationTrains *st_to = g_stations.find(t);
    if (!sf || !st_to) { puts("0"); return; }

    // Linear scan: collect candidate trainIDs that visit s
    static TicketResult results[2048];
    int rcount = 0;

    for (int i = 0; i < sf->len; i++) {
        const char *tid = sf->list[i].trainID;
        int from_idx = sf->list[i].station_idx;
        Train *tr = g_trains.get(tid);
        if (!tr || !tr->released) continue;
        if (from_idx == tr->stationNum - 1) continue;
        // Find this train in st_to
        int to_idx = -1;
        for (int j = 0; j < st_to->len; j++) {
            if (my_strcmp(st_to->list[j].trainID, tid) == 0) {
                if (st_to->list[j].station_idx > from_idx) {
                    to_idx = st_to->list[j].station_idx;
                    break;
                }
            }
        }
        if (to_idx < 0) continue;

        // Boarding day at station from_idx => train_start_day s.t. base_min + leaveTimes[from_idx] / 1440 = boarding_day
        int leave_min_offset = tr->leaveTimes[from_idx];
        int base_day = boarding_day - (tr->startTime + leave_min_offset) / 1440;
        // hmm careful: leave at station from_idx = base_day*1440 + startTime + leaveTimes[from_idx]
        // boarding_day = floor((startTime + leaveTimes[from_idx]) / 1440) + base_day - wait, base_day already in days
        // Re-derive: train day index in saleStart-saleEnd; absolute leave date = base_day + (startTime + leaveTimes[from_idx])/1440
        // boarding_day = base_day + days_offset
        int days_offset = (tr->startTime + leave_min_offset) / 1440;
        int train_day = boarding_day - days_offset;
        if (train_day < tr->saleStart || train_day > tr->saleEnd) continue;
        int day_idx = train_day - tr->saleStart;

        int leave_abs = train_day * 1440 + tr->startTime + tr->leaveTimes[from_idx];
        int arrive_abs = train_day * 1440 + tr->startTime + tr->arriveTimes[to_idx];
        int seat = min_seats(tr, day_idx, from_idx, to_idx);
        int price = tr->prices[to_idx] - tr->prices[from_idx];
        int duration = arrive_abs - leave_abs;

        if (rcount >= 2048) break;
        TicketResult &r = results[rcount++];
        strncpy(r.trainID, tid, 23); r.trainID[23] = 0;
        r.leave_time = leave_abs;
        r.arrive_time = arrive_abs;
        r.duration = duration;
        r.price = price;
        r.seat = seat;
        r.day_idx = day_idx;
        r.from_idx = from_idx;
        r.to_idx = to_idx;
    }

    if (sort_by_time) {
        sort_arr(results, rcount, [](const TicketResult &a, const TicketResult &b){
            if (a.duration != b.duration) return a.duration < b.duration;
            return my_strcmp(a.trainID, b.trainID) < 0;
        });
    } else {
        sort_arr(results, rcount, [](const TicketResult &a, const TicketResult &b){
            if (a.price != b.price) return a.price < b.price;
            return my_strcmp(a.trainID, b.trainID) < 0;
        });
    }

    printf("%d\n", rcount);
    for (int i = 0; i < rcount; i++) {
        char lt[16], at[16];
        format_datetime(results[i].leave_time, lt);
        format_datetime(results[i].arrive_time, at);
        printf("%s %s %s -> %s %s %d %d\n", results[i].trainID, s, lt, t, at, results[i].price, results[i].seat);
    }
}

// query_transfer: find best transfer through any intermediate station
// We iterate trains passing through s, then for each, iterate later stations as intermediate.
// For each intermediate, look for trains passing through it that also pass through t (later).
static void cmd_query_transfer(const ParsedArgs &a) {
    const char *s = a.get('s');
    const char *t = a.get('t');
    const char *d = a.get('d');
    const char *p = a.get('p');
    if (!s || !t || !d) { puts("0"); return; }
    bool sort_by_time = true;
    if (p && my_strcmp(p, "cost") == 0) sort_by_time = false;
    int boarding_day = parse_date(d);

    StationTrains *sf = g_stations.find(s);
    if (!sf) { puts("0"); return; }

    bool found = false;
    int best_total_time = 0, best_total_cost = 0, best_train1_time = 0;
    char best_t1[24], best_t2[24];
    int best_t1_leave = 0, best_t1_arr = 0, best_t1_price = 0, best_t1_seat = 0;
    int best_t2_leave = 0, best_t2_arr = 0, best_t2_price = 0, best_t2_seat = 0;
    char best_inter[34];
    int best_t1_day = 0, best_t1_fi = 0, best_t1_ti = 0;
    int best_t2_day = 0, best_t2_fi = 0, best_t2_ti = 0;

    for (int i = 0; i < sf->len; i++) {
        const char *tid1 = sf->list[i].trainID;
        int from1 = sf->list[i].station_idx;
        Train *tr1 = g_trains.get(tid1);
        if (!tr1 || !tr1->released) continue;
        if (from1 >= tr1->stationNum - 1) continue;

        // Determine train1 start day
        int days_offset1 = (tr1->startTime + tr1->leaveTimes[from1]) / 1440;
        int train_day1 = boarding_day - days_offset1;
        if (train_day1 < tr1->saleStart || train_day1 > tr1->saleEnd) continue;
        int day_idx1 = train_day1 - tr1->saleStart;
        int leave1 = train_day1 * 1440 + tr1->startTime + tr1->leaveTimes[from1];

        for (int mid1 = from1 + 1; mid1 < tr1->stationNum; mid1++) {
            const char *inter = tr1->stations[mid1].s;
            if (my_strcmp(inter, t) == 0) continue;
            int arr1 = train_day1 * 1440 + tr1->startTime + tr1->arriveTimes[mid1];
            int dur1 = arr1 - leave1;
            int seat1 = min_seats(tr1, day_idx1, from1, mid1);
            int price1 = tr1->prices[mid1] - tr1->prices[from1];

            // Find trains visiting inter
            StationTrains *si = g_stations.find(inter);
            if (!si) continue;
            for (int j = 0; j < si->len; j++) {
                const char *tid2 = si->list[j].trainID;
                if (my_strcmp(tid2, tid1) == 0) continue;
                int from2 = si->list[j].station_idx;
                Train *tr2 = g_trains.get(tid2);
                if (!tr2 || !tr2->released) continue;
                if (from2 >= tr2->stationNum - 1) continue;

                // Find t in tr2 after from2
                int to2 = -1;
                for (int k = from2 + 1; k < tr2->stationNum; k++) {
                    if (my_strcmp(tr2->stations[k].s, t) == 0) { to2 = k; break; }
                }
                if (to2 < 0) continue;

                // Determine earliest train_day2 such that leave2 >= arr1.
                // leave2 = train_day2 * 1440 + tr2->startTime + tr2->leaveTimes[from2]
                int leave_offset2 = tr2->startTime + tr2->leaveTimes[from2];
                int min_leave_day = (arr1 - leave_offset2 + 1440 - 1);
                int min_train_day2;
                // ceiling division for arr1 - leave_offset2 over 1440
                int diff = arr1 - leave_offset2;
                if (diff <= 0) min_train_day2 = 0;
                else min_train_day2 = (diff + 1439) / 1440;
                if (min_train_day2 < tr2->saleStart) min_train_day2 = tr2->saleStart;
                if (min_train_day2 > tr2->saleEnd) continue;
                int train_day2 = min_train_day2;
                int leave2 = train_day2 * 1440 + leave_offset2;
                if (leave2 < arr1) {
                    // Recompute
                    int extra = (arr1 - leave2 + 1439) / 1440;
                    train_day2 += extra;
                    if (train_day2 > tr2->saleEnd) continue;
                    leave2 = train_day2 * 1440 + leave_offset2;
                }
                int day_idx2 = train_day2 - tr2->saleStart;
                int arr2 = train_day2 * 1440 + tr2->startTime + tr2->arriveTimes[to2];
                int dur2 = arr2 - leave2;
                int seat2 = min_seats(tr2, day_idx2, from2, to2);
                int price2 = tr2->prices[to2] - tr2->prices[from2];

                int total_time = arr2 - leave1;
                int total_cost = price1 + price2;

                bool better = false;
                if (!found) better = true;
                else {
                    int key_a, key_b;
                    if (sort_by_time) { key_a = total_time; key_b = best_total_time; }
                    else { key_a = total_cost; key_b = best_total_cost; }
                    if (key_a < key_b) better = true;
                    else if (key_a == key_b) {
                        // Secondary: other key
                        int sec_a = sort_by_time ? total_cost : total_time;
                        int sec_b = sort_by_time ? best_total_cost : best_total_time;
                        if (sec_a < sec_b) better = true;
                        else if (sec_a == sec_b) {
                            int c = my_strcmp(tid1, best_t1);
                            if (c < 0) better = true;
                            else if (c == 0) {
                                int c2 = my_strcmp(tid2, best_t2);
                                if (c2 < 0) better = true;
                            }
                        }
                    }
                }

                if (better) {
                    found = true;
                    best_total_time = total_time;
                    best_total_cost = total_cost;
                    best_train1_time = dur1;
                    strncpy(best_t1, tid1, 23); best_t1[23] = 0;
                    strncpy(best_t2, tid2, 23); best_t2[23] = 0;
                    strncpy(best_inter, inter, 33); best_inter[33] = 0;
                    best_t1_leave = leave1; best_t1_arr = arr1; best_t1_price = price1; best_t1_seat = seat1;
                    best_t2_leave = leave2; best_t2_arr = arr2; best_t2_price = price2; best_t2_seat = seat2;
                    best_t1_day = day_idx1; best_t1_fi = from1; best_t1_ti = mid1;
                    best_t2_day = day_idx2; best_t2_fi = from2; best_t2_ti = to2;
                    (void)best_train1_time;
                    (void)best_t1_day; (void)best_t1_fi; (void)best_t1_ti;
                    (void)best_t2_day; (void)best_t2_fi; (void)best_t2_ti;
                }
            }
        }
    }

    if (!found) { puts("0"); return; }
    char lt1[16], at1[16], lt2[16], at2[16];
    format_datetime(best_t1_leave, lt1);
    format_datetime(best_t1_arr, at1);
    format_datetime(best_t2_leave, lt2);
    format_datetime(best_t2_arr, at2);
    printf("%s %s %s -> %s %s %d %d\n", best_t1, s, lt1, best_inter, at1, best_t1_price, best_t1_seat);
    printf("%s %s %s -> %s %s %d %d\n", best_t2, best_inter, lt2, t, at2, best_t2_price, best_t2_seat);
}

// Insert order at head of user's order list
static void user_orders_push(const char *username, Order *o) {
    UserOrderHead *h = g_user_orders.get_or_create(username);
    o->user_prev = nullptr;
    o->user_next = h->head;
    if (h->head) h->head->user_prev = o;
    h->head = o;
}

static void cmd_buy_ticket(const ParsedArgs &a) {
    const char *u = a.get('u');
    const char *i = a.get('i');
    const char *d = a.get('d');
    const char *n = a.get('n');
    const char *f = a.get('f');
    const char *t = a.get('t');
    const char *q = a.get('q');
    if (!u || !i || !d || !n || !f || !t) { puts("-1"); return; }
    if (!g_logged.contains(u)) { puts("-1"); return; }
    Train *tr = g_trains.get(i);
    if (!tr || !tr->released) { puts("-1"); return; }
    int num = atoi(n);
    if (num <= 0 || num > tr->seatNum) { puts("-1"); return; }
    int from_idx = find_station_in_train(tr, f);
    int to_idx = find_station_in_train(tr, t);
    if (from_idx < 0 || to_idx < 0 || from_idx >= to_idx) { puts("-1"); return; }
    int boarding_day = parse_date(d);
    int days_offset = (tr->startTime + tr->leaveTimes[from_idx]) / 1440;
    int train_day = boarding_day - days_offset;
    if (train_day < tr->saleStart || train_day > tr->saleEnd) { puts("-1"); return; }
    int day_idx = train_day - tr->saleStart;
    int avail = min_seats(tr, day_idx, from_idx, to_idx);
    int price_per = tr->prices[to_idx] - tr->prices[from_idx];
    int leave_abs = train_day * 1440 + tr->startTime + tr->leaveTimes[from_idx];
    int arrive_abs = train_day * 1440 + tr->startTime + tr->arriveTimes[to_idx];

    bool standby = (q && my_strcmp(q, "true") == 0);

    if (avail >= num) {
        take_seats(tr, day_idx, from_idx, to_idx, num);
        Order *o = new Order();
        strncpy(o->trainID, i, 23); o->trainID[23] = 0;
        strncpy(o->fromStation, f, 33); o->fromStation[33] = 0;
        strncpy(o->toStation, t, 33); o->toStation[33] = 0;
        o->from_idx = from_idx; o->to_idx = to_idx;
        o->day = train_day; o->num = num;
        o->price = price_per;
        o->leave_time = leave_abs; o->arrive_time = arrive_abs;
        o->status = OS_SUCCESS;
        o->timestamp = ++g_timestamp;
        strncpy(o->username, u, 23); o->username[23] = 0;
        o->user_prev = o->user_next = o->queue_next = nullptr;
        user_orders_push(u, o);
        long long total = (long long)price_per * num;
        printf("%lld\n", total);
        return;
    }
    if (!standby) { puts("-1"); return; }
    // Add to pending queue
    Order *o = new Order();
    strncpy(o->trainID, i, 23); o->trainID[23] = 0;
    strncpy(o->fromStation, f, 33); o->fromStation[33] = 0;
    strncpy(o->toStation, t, 33); o->toStation[33] = 0;
    o->from_idx = from_idx; o->to_idx = to_idx;
    o->day = train_day; o->num = num;
    o->price = price_per;
    o->leave_time = leave_abs; o->arrive_time = arrive_abs;
    o->status = OS_PENDING;
    o->timestamp = ++g_timestamp;
    strncpy(o->username, u, 23); o->username[23] = 0;
    o->user_prev = o->user_next = o->queue_next = nullptr;
    user_orders_push(u, o);

    PendingHead *ph = g_pending.get_or_create(i, train_day);
    if (!ph->head) { ph->head = ph->tail = o; }
    else { ph->tail->queue_next = o; ph->tail = o; }
    puts("queue");
}

static const char *status_str(int s) {
    if (s == OS_SUCCESS) return "success";
    if (s == OS_PENDING) return "pending";
    return "refunded";
}

static void cmd_query_order(const ParsedArgs &a) {
    const char *u = a.get('u');
    if (!u) { puts("-1"); return; }
    if (!g_logged.contains(u)) { puts("-1"); return; }
    UserOrderHead *h = g_user_orders.find(u);
    int cnt = 0;
    Order *cur = h ? h->head : nullptr;
    Order *p = cur;
    while (p) { cnt++; p = p->user_next; }
    printf("%d\n", cnt);
    p = cur;
    while (p) {
        char lt[16], at[16];
        format_datetime(p->leave_time, lt);
        format_datetime(p->arrive_time, at);
        printf("[%s] %s %s %s -> %s %s %d %d\n", status_str(p->status), p->trainID, p->fromStation, lt, p->toStation, at, p->price, p->num);
        p = p->user_next;
    }
}

// Try to fulfill pending queue for train+day after seats are returned
static void try_fulfill_pending(Train *tr, int train_day) {
    PendingHead *ph = g_pending.find(tr->trainID, train_day);
    if (!ph || !ph->head) return;
    int day_idx = train_day - tr->saleStart;
    Order *prev = nullptr;
    Order *o = ph->head;
    while (o) {
        if (o->status != OS_PENDING) {
            // Skip / remove from queue
            Order *nx = o->queue_next;
            if (prev) prev->queue_next = nx;
            else ph->head = nx;
            if (ph->tail == o) ph->tail = prev;
            o = nx;
            continue;
        }
        int avail = min_seats(tr, day_idx, o->from_idx, o->to_idx);
        if (avail >= o->num) {
            take_seats(tr, day_idx, o->from_idx, o->to_idx, o->num);
            o->status = OS_SUCCESS;
            // Remove from queue
            Order *nx = o->queue_next;
            o->queue_next = nullptr;
            if (prev) prev->queue_next = nx;
            else ph->head = nx;
            if (ph->tail == o) ph->tail = prev;
            o = nx;
        } else {
            prev = o;
            o = o->queue_next;
        }
    }
}

static void cmd_refund_ticket(const ParsedArgs &a) {
    const char *u = a.get('u');
    const char *n = a.get('n');
    if (!u) { puts("-1"); return; }
    if (!g_logged.contains(u)) { puts("-1"); return; }
    int idx = n ? atoi(n) : 1;
    if (idx <= 0) { puts("-1"); return; }
    UserOrderHead *h = g_user_orders.find(u);
    if (!h || !h->head) { puts("-1"); return; }
    Order *o = h->head;
    int k = 1;
    while (o && k < idx) { o = o->user_next; k++; }
    if (!o) { puts("-1"); return; }
    if (o->status == OS_REFUNDED) { puts("-1"); return; }
    Train *tr = g_trains.get(o->trainID);
    if (o->status == OS_SUCCESS) {
        if (tr) {
            int day_idx = o->day - tr->saleStart;
            return_seats(tr, day_idx, o->from_idx, o->to_idx, o->num);
            o->status = OS_REFUNDED;
            try_fulfill_pending(tr, o->day);
        } else {
            o->status = OS_REFUNDED;
        }
    } else { // PENDING -> mark refunded; queue cleanup happens lazily
        o->status = OS_REFUNDED;
    }
    puts("0");
}

static void cmd_clean() {
    // Free everything by re-init
    delete[] g_users.items; g_users.items = nullptr;
    // Free train seats first
    for (int i = 0; i < g_trains.cap; i++) {
        if (g_trains.items[i].used && g_trains.items[i].seats) delete[] g_trains.items[i].seats;
    }
    delete[] g_trains.items; g_trains.items = nullptr;
    // Free station entry lists
    for (int i = 0; i < g_stations.cap; i++) {
        if (g_stations.items[i].used && g_stations.items[i].list) delete[] g_stations.items[i].list;
    }
    delete[] g_stations.items; g_stations.items = nullptr;
    delete[] g_logged.items; g_logged.items = nullptr;
    // Orders: leak (program exits soon)
    delete[] g_user_orders.items; g_user_orders.items = nullptr;
    delete[] g_pending.items; g_pending.items = nullptr;

    g_users.cap = 0; g_users.count = 0;
    g_trains.cap = 0; g_trains.count = 0;
    g_stations.cap = 0; g_stations.count = 0;
    g_logged.cap = 0; g_logged.count = 0;
    g_user_orders.cap = 0; g_user_orders.count = 0;
    g_pending.cap = 0; g_pending.count = 0;

    g_users.init(8192);
    g_trains.init(2048);
    g_stations.init(4096);
    g_logged.init(2048);
    g_user_orders.init(8192);
    g_pending.init(4096);
    g_timestamp = 0;
    puts("0");
}

// =================== Main loop ===================
int main() {
    static char line[8192];
    static char *tokens[256];

    g_users.init(8192);
    g_trains.init(2048);
    g_stations.init(4096);
    g_logged.init(2048);
    g_user_orders.init(8192);
    g_pending.init(4096);

    while (true) {
        int n = read_line(line, sizeof(line));
        if (n < 0) break;
        if (n == 0) continue;

        // First token is the timestamp [123] OR the command. Some test formats have [N] prefix.
        char *p = line;
        // Handle optional [timestamp] prefix
        char ts_prefix[32] = {0};
        if (p[0] == '[') {
            char *close = strchr(p, ']');
            if (close) {
                int len = close - p + 1;
                if (len < 32) { strncpy(ts_prefix, p, len); ts_prefix[len] = 0; }
                p = close + 1;
                while (*p == ' ') p++;
            }
        }

        int nt = tokenize(p, tokens, 256);
        if (nt == 0) continue;

        if (ts_prefix[0]) printf("%s ", ts_prefix);

        const char *cmd = tokens[0];
        ParsedArgs args;

        if (my_strcmp(cmd, "add_user") == 0) { parse_args(tokens, nt, 1, args); cmd_add_user(args); }
        else if (my_strcmp(cmd, "login") == 0) { parse_args(tokens, nt, 1, args); cmd_login(args); }
        else if (my_strcmp(cmd, "logout") == 0) { parse_args(tokens, nt, 1, args); cmd_logout(args); }
        else if (my_strcmp(cmd, "query_profile") == 0) { parse_args(tokens, nt, 1, args); cmd_query_profile(args); }
        else if (my_strcmp(cmd, "modify_profile") == 0) { parse_args(tokens, nt, 1, args); cmd_modify_profile(args); }
        else if (my_strcmp(cmd, "add_train") == 0) { parse_args(tokens, nt, 1, args); cmd_add_train(args); }
        else if (my_strcmp(cmd, "delete_train") == 0) { parse_args(tokens, nt, 1, args); cmd_delete_train(args); }
        else if (my_strcmp(cmd, "release_train") == 0) { parse_args(tokens, nt, 1, args); cmd_release_train(args); }
        else if (my_strcmp(cmd, "query_train") == 0) { parse_args(tokens, nt, 1, args); cmd_query_train(args); }
        else if (my_strcmp(cmd, "query_ticket") == 0) { parse_args(tokens, nt, 1, args); cmd_query_ticket(args); }
        else if (my_strcmp(cmd, "query_transfer") == 0) { parse_args(tokens, nt, 1, args); cmd_query_transfer(args); }
        else if (my_strcmp(cmd, "buy_ticket") == 0) { parse_args(tokens, nt, 1, args); cmd_buy_ticket(args); }
        else if (my_strcmp(cmd, "query_order") == 0) { parse_args(tokens, nt, 1, args); cmd_query_order(args); }
        else if (my_strcmp(cmd, "refund_ticket") == 0) { parse_args(tokens, nt, 1, args); cmd_refund_ticket(args); }
        else if (my_strcmp(cmd, "clean") == 0) { cmd_clean(); }
        else if (my_strcmp(cmd, "exit") == 0) { puts("bye"); break; }
        else {
            // unknown
            puts("-1");
        }
    }
    return 0;
}
