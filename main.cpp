// Train Ticket Management System with persistence
// In-memory ops + binary save/load on startup/exit. No STL containers (std::string only).

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>

#define STATE_FILE "state.bin"

// ===== misc =====
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// ===== I/O =====
static int read_line(char *buf, int max_len) {
    int n = 0; int c;
    while ((c = getchar_unlocked()) != EOF && c != '\n') {
        if (n + 1 < max_len) buf[n++] = (char)c;
    }
    if (c == EOF && n == 0) return -1;
    buf[n] = 0;
    return n;
}

// ===== date/time =====
static int days_in_month_table[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

static int parse_date(const char *s) {
    int m = (s[0]-'0')*10 + (s[1]-'0');
    int d = (s[3]-'0')*10 + (s[4]-'0');
    int day = 0;
    if (m < 6) return -1;
    for (int i = 6; i < m; i++) day += days_in_month_table[i];
    day += (d - 1);
    return day;
}
static void format_date(int day, char *out) {
    int m = 6, d = day + 1;
    while (d > days_in_month_table[m]) { d -= days_in_month_table[m]; m++; }
    out[0]='0'+m/10; out[1]='0'+m%10; out[2]='-';
    out[3]='0'+d/10; out[4]='0'+d%10; out[5]=0;
}
static int parse_time(const char *s) {
    int h = (s[0]-'0')*10 + (s[1]-'0');
    int m = (s[3]-'0')*10 + (s[4]-'0');
    return h*60 + m;
}
static void format_datetime(int total_minutes, char *out) {
    int day = total_minutes / 1440;
    int min_in_day = total_minutes % 1440;
    int h = min_in_day / 60;
    int mn = min_in_day % 60;
    char db[8]; format_date(day, db);
    out[0]=db[0]; out[1]=db[1]; out[2]=db[2]; out[3]=db[3]; out[4]=db[4];
    out[5]=' ';
    out[6]='0'+h/10; out[7]='0'+h%10;
    out[8]=':';
    out[9]='0'+mn/10; out[10]='0'+mn%10;
    out[11]=0;
}

// ===== hash =====
static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

// ===== User =====
struct User {
    char username[24];
    char password[36];
    char name[24];
    char mailAddr[36];
    int privilege;
    bool used;
};

// ===== Station name =====
struct StationName { char s[34]; };

// ===== Train =====
struct Train {
    char trainID[24];
    int stationNum;
    int seatNum;
    StationName stations[100];
    int prices[100];
    int startTime;
    int leaveTimes[100];
    int arriveTimes[100];
    int saleStart;
    int saleEnd;
    char type;
    bool released;
    bool used;
    int *seats; // runtime pointer; days*segs ints
};

// ===== Order =====
enum OrderStatus { OS_SUCCESS = 0, OS_PENDING = 1, OS_REFUNDED = 2 };
struct Order {
    char trainID[24];
    char fromStation[34];
    char toStation[34];
    int from_idx, to_idx;
    int day;
    int num;
    int price;
    int leave_time;
    int arrive_time;
    int status;
    long long timestamp;
    char username[24];
    int user_prev_id;   // -1 if none
    int user_next_id;
    int queue_next_id;
};

// ===== Globals =====
static Order *g_orders = nullptr;
static int g_orders_count = 0;
static int g_orders_cap = 0;
static long long g_timestamp = 0;

static int new_order_slot() {
    if (g_orders_count == g_orders_cap) {
        int nc = g_orders_cap ? g_orders_cap * 2 : 1024;
        Order *no = new Order[nc];
        if (g_orders_count > 0) memcpy(no, g_orders, sizeof(Order) * g_orders_count);
        delete[] g_orders;
        g_orders = no;
        g_orders_cap = nc;
    }
    return g_orders_count++;
}

// ===== UserMap (hash map) =====
struct UserMap {
    int cap;
    int count;
    User *items;
    UserMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity; count = 0;
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
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
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
        return s < 0 ? nullptr : &items[s];
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

// ===== TrainMap =====
struct TrainMap {
    int cap;
    int count;
    Train *items;
    TrainMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity; count = 0;
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
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
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
        return s < 0 ? nullptr : &items[s];
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

// ===== StationMap (station name -> list of (trainID, idx)) =====
struct StationEntry { char trainID[24]; int station_idx; };
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
        cap = capacity; count = 0;
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
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
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
    StationTrains *find(const char *key) {
        int s = find_slot(key);
        return s < 0 ? nullptr : &items[s];
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
        items[i].used = true;
        strncpy(items[i].name.s, key, 33); items[i].name.s[33] = 0;
        items[i].len = 0;
        items[i].cap = 4;
        items[i].list = new StationEntry[4];
        count++;
        return &items[i];
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
    strncpy(st->list[st->len].trainID, trainID, 23); st->list[st->len].trainID[23] = 0;
    st->list[st->len].station_idx = idx;
    st->len++;
}

// ===== UserOrderMap: username -> head (order index, -1 = none) =====
struct UserOrderHead {
    char username[24];
    int head_id; // newest first
    bool used;
};
struct UserOrderMap {
    int cap;
    int count;
    UserOrderHead *items;
    UserOrderMap() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity; count = 0;
        items = new UserOrderHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head_id = -1; }
    }
    void rehash() {
        int oldcap = cap;
        UserOrderHead *old = items;
        cap *= 2;
        items = new UserOrderHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head_id = -1; }
        count = 0;
        for (int i = 0; i < oldcap; i++) if (old[i].used) insert_internal(old[i]);
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
    UserOrderHead *find(const char *key) {
        int s = find_slot(key);
        return s < 0 ? nullptr : &items[s];
    }
    UserOrderHead *get_or_create(const char *key) {
        if ((count + 1) * 2 > cap) rehash();
        uint32_t h = hash_str(key);
        int mask = cap - 1;
        int i = h & mask;
        while (items[i].used) {
            if (my_strcmp(items[i].username, key) == 0) return &items[i];
            i = (i + 1) & mask;
        }
        items[i].used = true;
        strncpy(items[i].username, key, 23); items[i].username[23] = 0;
        items[i].head_id = -1;
        count++;
        return &items[i];
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

// ===== PendingMap: (trainID, day) -> queue of order ids =====
struct PendingHead {
    char trainID[24];
    int day;
    int head_id;
    int tail_id;
    bool used;
};
struct PendingMap {
    int cap;
    int count;
    PendingHead *items;
    PendingMap() : cap(0), count(0), items(nullptr) {}
    static uint32_t key_hash(const char *t, int d) {
        uint32_t h = hash_str(t);
        h ^= (uint32_t)d * 2654435761u;
        return h;
    }
    void init(int capacity) {
        cap = capacity; count = 0;
        items = new PendingHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head_id = -1; items[i].tail_id = -1; }
    }
    void rehash() {
        int oldcap = cap;
        PendingHead *old = items;
        cap *= 2;
        items = new PendingHead[cap];
        for (int i = 0; i < cap; i++) { items[i].used = false; items[i].head_id = -1; items[i].tail_id = -1; }
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
    PendingHead *find(const char *t, int d) {
        int s = find_slot(t, d);
        return s < 0 ? nullptr : &items[s];
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
        strncpy(items[i].trainID, t, 23); items[i].trainID[23] = 0;
        items[i].day = d;
        items[i].head_id = -1;
        items[i].tail_id = -1;
        count++;
        return &items[i];
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

// ===== LoginSet (transient — not persisted) =====
struct LoginSet {
    int cap;
    int count;
    char (*items)[24];
    LoginSet() : cap(0), count(0), items(nullptr) {}
    void init(int capacity) {
        cap = capacity; count = 0;
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
        for (int i = 0; i < oldcap; i++) if (old[i][0]) insert(old[i]);
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
        strncpy(items[i], k, 23); items[i][23] = 0;
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
            char tmp[24]; strncpy(tmp, items[i], 24);
            items[i][0] = 0;
            count--;
            insert(tmp);
            i = (i + 1) & mask;
        }
        return true;
    }
};

// ===== Globals =====
static UserMap g_users;
static TrainMap g_trains;
static StationMap g_stations;
static UserOrderMap g_user_orders;
static PendingMap g_pending;
static LoginSet g_logged;

// ===== Persistence =====
// Format (binary, little-endian assumed; same machine for save & load on OJ):
//   uint32 MAGIC
//   long long g_timestamp
//   int g_users.count, then [User struct] * count (only used)
//   int g_trains.count, then for each: [Train w/o seats], then int days, int segs, then seats[days*segs]
//   int g_orders_count, then [Order struct] * count
//   int g_user_orders.count, then for each: UserOrderHead struct
//   int g_pending.count, then for each: PendingHead struct

#define MAGIC 0xC0FFEE17u

static bool save_state() {
    FILE *f = fopen(STATE_FILE, "wb");
    if (!f) return false;
    uint32_t magic = MAGIC;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&g_timestamp, sizeof(g_timestamp), 1, f);

    // Users
    fwrite(&g_users.count, sizeof(int), 1, f);
    for (int i = 0; i < g_users.cap; i++) {
        if (g_users.items[i].used) fwrite(&g_users.items[i], sizeof(User), 1, f);
    }

    // Trains
    fwrite(&g_trains.count, sizeof(int), 1, f);
    for (int i = 0; i < g_trains.cap; i++) {
        if (!g_trains.items[i].used) continue;
        Train &t = g_trains.items[i];
        // Save struct WITHOUT the seats pointer effectively (we ignore the pointer value on load).
        fwrite(&t, sizeof(Train), 1, f);
        int days = t.released ? (t.saleEnd - t.saleStart + 1) : 0;
        int segs = t.released ? (t.stationNum - 1) : 0;
        fwrite(&days, sizeof(int), 1, f);
        fwrite(&segs, sizeof(int), 1, f);
        if (days > 0 && segs > 0 && t.seats) fwrite(t.seats, sizeof(int), (long long)days * segs, f);
    }

    // Orders
    fwrite(&g_orders_count, sizeof(int), 1, f);
    if (g_orders_count > 0) fwrite(g_orders, sizeof(Order), g_orders_count, f);

    // User orders heads
    fwrite(&g_user_orders.count, sizeof(int), 1, f);
    for (int i = 0; i < g_user_orders.cap; i++) {
        if (g_user_orders.items[i].used) fwrite(&g_user_orders.items[i], sizeof(UserOrderHead), 1, f);
    }

    // Pending heads
    fwrite(&g_pending.count, sizeof(int), 1, f);
    for (int i = 0; i < g_pending.cap; i++) {
        if (g_pending.items[i].used) fwrite(&g_pending.items[i], sizeof(PendingHead), 1, f);
    }

    fclose(f);
    return true;
}

static bool load_state() {
    FILE *f = fopen(STATE_FILE, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != MAGIC) { fclose(f); return false; }
    if (fread(&g_timestamp, sizeof(g_timestamp), 1, f) != 1) { fclose(f); return false; }

    int n;
    // Users
    if (fread(&n, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    for (int i = 0; i < n; i++) {
        User u;
        if (fread(&u, sizeof(User), 1, f) != 1) { fclose(f); return false; }
        g_users.insert(u);
    }

    // Trains
    if (fread(&n, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    for (int i = 0; i < n; i++) {
        Train t;
        if (fread(&t, sizeof(Train), 1, f) != 1) { fclose(f); return false; }
        t.seats = nullptr;
        int days = 0, segs = 0;
        if (fread(&days, sizeof(int), 1, f) != 1) { fclose(f); return false; }
        if (fread(&segs, sizeof(int), 1, f) != 1) { fclose(f); return false; }
        if (days > 0 && segs > 0) {
            t.seats = new int[(long long)days * segs];
            if ((int)fread(t.seats, sizeof(int), (long long)days * segs, f) != days * segs) {
                fclose(f); return false;
            }
        }
        g_trains.insert(t);
        // Rebuild station index
        if (t.released) {
            // Re-find inserted train (since insert copies)
            Train *tin = g_trains.get(t.trainID);
            for (int k = 0; k < t.stationNum; k++) {
                StationTrains *st = g_stations.get_or_create(t.stations[k].s);
                station_add_train(st, t.trainID, k);
            }
            (void)tin;
        }
    }

    // Orders
    if (fread(&g_orders_count, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    if (g_orders_count > 0) {
        g_orders_cap = 1024;
        while (g_orders_cap < g_orders_count) g_orders_cap *= 2;
        g_orders = new Order[g_orders_cap];
        if ((int)fread(g_orders, sizeof(Order), g_orders_count, f) != g_orders_count) { fclose(f); return false; }
    }

    // User orders heads
    if (fread(&n, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    for (int i = 0; i < n; i++) {
        UserOrderHead h;
        if (fread(&h, sizeof(UserOrderHead), 1, f) != 1) { fclose(f); return false; }
        if ((g_user_orders.count + 1) * 2 > g_user_orders.cap) g_user_orders.rehash();
        g_user_orders.insert_internal(h);
    }

    // Pending heads
    if (fread(&n, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    for (int i = 0; i < n; i++) {
        PendingHead h;
        if (fread(&h, sizeof(PendingHead), 1, f) != 1) { fclose(f); return false; }
        if ((g_pending.count + 1) * 2 > g_pending.cap) g_pending.rehash();
        g_pending.insert_internal(h);
    }

    fclose(f);
    return true;
}

// ===== Argument parsing =====
static int tokenize(char *buf, char **tokens, int max_tokens) {
    int n = 0; int i = 0;
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
struct ParsedArgs {
    const char *vals[26];
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
static int split_pipe(char *s, char **out, int max_n) {
    int n = 0;
    out[n++] = s;
    while (*s) {
        if (*s == '|') { *s = 0; if (n < max_n) out[n++] = s + 1; }
        s++;
    }
    return n;
}

// ===== Helpers =====
static int find_station_in_train(const Train *t, const char *name) {
    for (int k = 0; k < t->stationNum; k++) if (my_strcmp(t->stations[k].s, name) == 0) return k;
    return -1;
}
static int min_seats(const Train *t, int day_idx, int from_idx, int to_idx) {
    int segs = t->stationNum - 1;
    int mn = t->seatNum;
    for (int k = from_idx; k < to_idx; k++) {
        int v = t->seats[day_idx * segs + k];
        if (v < mn) mn = v;
    }
    return mn;
}
static void take_seats(Train *t, int day_idx, int from_idx, int to_idx, int num) {
    int segs = t->stationNum - 1;
    for (int k = from_idx; k < to_idx; k++) t->seats[day_idx * segs + k] -= num;
}
static void return_seats(Train *t, int day_idx, int from_idx, int to_idx, int num) {
    int segs = t->stationNum - 1;
    for (int k = from_idx; k < to_idx; k++) t->seats[day_idx * segs + k] += num;
}

// Insertion sort
template<typename T, typename Cmp>
static void sort_arr(T *arr, int n, Cmp cmp) {
    for (int i = 1; i < n; i++) {
        T x = arr[i];
        int j = i - 1;
        while (j >= 0 && cmp(x, arr[j])) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = x;
    }
}

// ===== Order list helpers =====
static void user_orders_push(const char *username, int oid) {
    UserOrderHead *h = g_user_orders.get_or_create(username);
    g_orders[oid].user_prev_id = -1;
    g_orders[oid].user_next_id = h->head_id;
    if (h->head_id >= 0) g_orders[h->head_id].user_prev_id = oid;
    h->head_id = oid;
}

// ===== Commands =====

static void cmd_add_user(const ParsedArgs &a) {
    const char *c = a.get('c'), *u = a.get('u'), *p = a.get('p');
    const char *n = a.get('n'), *m = a.get('m'), *g = a.get('g');
    if (!u || !p || !n || !m) { puts("-1"); return; }

    if (g_users.count == 0) {
        User nu;
        nu.used = true;
        strncpy(nu.username, u, 23); nu.username[23] = 0;
        strncpy(nu.password, p, 35); nu.password[35] = 0;
        strncpy(nu.name, n, 23); nu.name[23] = 0;
        strncpy(nu.mailAddr, m, 35); nu.mailAddr[35] = 0;
        nu.privilege = 10;
        if (!g_users.insert(nu)) { puts("-1"); return; }
        puts("0"); return;
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
    const char *u = a.get('u'), *p = a.get('p');
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
    const char *c = a.get('c'), *u = a.get('u');
    if (!c || !u) { puts("-1"); return; }
    if (!g_logged.contains(c)) { puts("-1"); return; }
    User *cu = g_users.get(c);
    User *uu = g_users.get(u);
    if (!cu || !uu) { puts("-1"); return; }
    if (cu->privilege <= uu->privilege && my_strcmp(c, u) != 0) { puts("-1"); return; }
    printf("%s %s %s %d\n", uu->username, uu->name, uu->mailAddr, uu->privilege);
}

static void cmd_modify_profile(const ParsedArgs &a) {
    const char *c = a.get('c'), *u = a.get('u'), *p = a.get('p');
    const char *n = a.get('n'), *m = a.get('m'), *g = a.get('g');
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
    const char *i = a.get('i'), *n = a.get('n'), *m = a.get('m'), *s = a.get('s');
    const char *p = a.get('p'), *x = a.get('x'), *t = a.get('t'), *o = a.get('o');
    const char *d = a.get('d'), *y = a.get('y');
    if (!i || !n || !m || !s || !p || !x || !t || !o || !d || !y) { puts("-1"); return; }

    int stationNum = atoi(n);
    int seatNum = atoi(m);
    if (stationNum < 2 || stationNum > 100 || seatNum <= 0) { puts("-1"); return; }
    if (g_trains.get(i)) { puts("-1"); return; }

    Train tr;
    memset(&tr, 0, sizeof(Train));
    tr.used = true; tr.released = false; tr.seats = nullptr;
    strncpy(tr.trainID, i, 23); tr.trainID[23] = 0;
    tr.stationNum = stationNum;
    tr.seatNum = seatNum;
    tr.type = y[0];

    {
        char buf[2048]; strncpy(buf, s, 2047); buf[2047] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum) { puts("-1"); return; }
        for (int k = 0; k < cnt; k++) { strncpy(tr.stations[k].s, parts[k], 33); tr.stations[k].s[33] = 0; }
    }
    {
        char buf[1024]; strncpy(buf, p, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum - 1) { puts("-1"); return; }
        tr.prices[0] = 0;
        for (int k = 0; k < cnt; k++) tr.prices[k + 1] = tr.prices[k] + atoi(parts[k]);
    }
    int travel[100], stopover[100];
    {
        char buf[1024]; strncpy(buf, t, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (cnt != stationNum - 1) { puts("-1"); return; }
        for (int k = 0; k < cnt; k++) travel[k] = atoi(parts[k]);
    }
    {
        char buf[1024]; strncpy(buf, o, 1023); buf[1023] = 0;
        char *parts[100];
        int cnt = split_pipe(buf, parts, 100);
        if (stationNum > 2) {
            if (cnt != stationNum - 2) { puts("-1"); return; }
            for (int k = 0; k < cnt; k++) stopover[k] = atoi(parts[k]);
        }
    }
    tr.startTime = parse_time(x);
    tr.leaveTimes[0] = 0; tr.arriveTimes[0] = 0;
    int cur = 0;
    for (int k = 0; k < stationNum - 1; k++) {
        tr.arriveTimes[k + 1] = cur + travel[k];
        cur = tr.arriveTimes[k + 1];
        if (k + 1 < stationNum - 1) {
            tr.leaveTimes[k + 1] = cur + stopover[k];
            cur = tr.leaveTimes[k + 1];
        } else {
            tr.leaveTimes[k + 1] = cur;
        }
    }
    {
        char buf[16]; strncpy(buf, d, 15); buf[15] = 0;
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
    for (int k = 0; k < t->stationNum; k++) {
        StationTrains *st = g_stations.get_or_create(t->stations[k].s);
        station_add_train(st, t->trainID, k);
    }
    puts("0");
}

static void cmd_query_train(const ParsedArgs &a) {
    const char *i = a.get('i'), *d = a.get('d');
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
        if (k == 0) strcpy(arr_s, "xx-xx xx:xx");
        else format_datetime(base_min + t->arriveTimes[k], arr_s);
        if (k == t->stationNum - 1) strcpy(lev_s, "xx-xx xx:xx");
        else format_datetime(base_min + t->leaveTimes[k], lev_s);
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

struct TicketResult {
    char trainID[24];
    int leave_time, arrive_time, duration, price, seat;
};

static void cmd_query_ticket(const ParsedArgs &a) {
    const char *s = a.get('s'), *t = a.get('t'), *d = a.get('d'), *p = a.get('p');
    if (!s || !t || !d) { puts("-1"); return; }
    bool sort_by_time = !(p && my_strcmp(p, "cost") == 0);
    int boarding_day = parse_date(d);
    StationTrains *sf = g_stations.find(s);
    StationTrains *st_to = g_stations.find(t);
    if (!sf || !st_to) { puts("0"); return; }

    static TicketResult results[8192];
    int rcount = 0;
    for (int i = 0; i < sf->len; i++) {
        const char *tid = sf->list[i].trainID;
        int from_idx = sf->list[i].station_idx;
        Train *tr = g_trains.get(tid);
        if (!tr || !tr->released) continue;
        if (from_idx >= tr->stationNum - 1) continue;
        int to_idx = -1;
        for (int j = 0; j < st_to->len; j++) {
            if (my_strcmp(st_to->list[j].trainID, tid) == 0 && st_to->list[j].station_idx > from_idx) {
                to_idx = st_to->list[j].station_idx; break;
            }
        }
        if (to_idx < 0) continue;
        int leave_off = tr->leaveTimes[from_idx];
        int days_offset = (tr->startTime + leave_off) / 1440;
        int train_day = boarding_day - days_offset;
        if (train_day < tr->saleStart || train_day > tr->saleEnd) continue;
        int day_idx = train_day - tr->saleStart;
        int leave_abs = train_day * 1440 + tr->startTime + leave_off;
        int arrive_abs = train_day * 1440 + tr->startTime + tr->arriveTimes[to_idx];
        int seat = min_seats(tr, day_idx, from_idx, to_idx);
        int price = tr->prices[to_idx] - tr->prices[from_idx];
        if (rcount >= 8192) break;
        TicketResult &r = results[rcount++];
        strncpy(r.trainID, tid, 23); r.trainID[23] = 0;
        r.leave_time = leave_abs; r.arrive_time = arrive_abs;
        r.duration = arrive_abs - leave_abs; r.price = price; r.seat = seat;
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

static void cmd_query_transfer(const ParsedArgs &a) {
    const char *s = a.get('s'), *t = a.get('t'), *d = a.get('d'), *p = a.get('p');
    if (!s || !t || !d) { puts("0"); return; }
    bool sort_by_time = !(p && my_strcmp(p, "cost") == 0);
    int boarding_day = parse_date(d);
    StationTrains *sf = g_stations.find(s);
    if (!sf) { puts("0"); return; }

    bool found = false;
    int best_total_time = 0, best_total_cost = 0;
    char best_t1[24], best_t2[24];
    int best_t1_leave = 0, best_t1_arr = 0, best_t1_price = 0, best_t1_seat = 0;
    int best_t2_leave = 0, best_t2_arr = 0, best_t2_price = 0, best_t2_seat = 0;
    char best_inter[34];

    for (int i = 0; i < sf->len; i++) {
        const char *tid1 = sf->list[i].trainID;
        int from1 = sf->list[i].station_idx;
        Train *tr1 = g_trains.get(tid1);
        if (!tr1 || !tr1->released) continue;
        if (from1 >= tr1->stationNum - 1) continue;
        int days_offset1 = (tr1->startTime + tr1->leaveTimes[from1]) / 1440;
        int train_day1 = boarding_day - days_offset1;
        if (train_day1 < tr1->saleStart || train_day1 > tr1->saleEnd) continue;
        int day_idx1 = train_day1 - tr1->saleStart;
        int leave1 = train_day1 * 1440 + tr1->startTime + tr1->leaveTimes[from1];

        for (int mid1 = from1 + 1; mid1 < tr1->stationNum; mid1++) {
            const char *inter = tr1->stations[mid1].s;
            if (my_strcmp(inter, t) == 0) continue;
            int arr1 = train_day1 * 1440 + tr1->startTime + tr1->arriveTimes[mid1];
            int seat1 = min_seats(tr1, day_idx1, from1, mid1);
            int price1 = tr1->prices[mid1] - tr1->prices[from1];

            StationTrains *si = g_stations.find(inter);
            if (!si) continue;
            for (int j = 0; j < si->len; j++) {
                const char *tid2 = si->list[j].trainID;
                if (my_strcmp(tid2, tid1) == 0) continue;
                int from2 = si->list[j].station_idx;
                Train *tr2 = g_trains.get(tid2);
                if (!tr2 || !tr2->released) continue;
                if (from2 >= tr2->stationNum - 1) continue;
                int to2 = -1;
                for (int k = from2 + 1; k < tr2->stationNum; k++)
                    if (my_strcmp(tr2->stations[k].s, t) == 0) { to2 = k; break; }
                if (to2 < 0) continue;

                int leave_off2 = tr2->startTime + tr2->leaveTimes[from2];
                // earliest train_day2 such that leave2 = train_day2 * 1440 + leave_off2 >= arr1
                int min_train_day2;
                int diff = arr1 - leave_off2;
                if (diff <= 0) min_train_day2 = 0;
                else min_train_day2 = (diff + 1439) / 1440;
                if (min_train_day2 < tr2->saleStart) min_train_day2 = tr2->saleStart;
                if (min_train_day2 > tr2->saleEnd) continue;
                int train_day2 = min_train_day2;
                int leave2 = train_day2 * 1440 + leave_off2;
                if (leave2 < arr1) {
                    int extra = (arr1 - leave2 + 1439) / 1440;
                    train_day2 += extra;
                    if (train_day2 > tr2->saleEnd) continue;
                    leave2 = train_day2 * 1440 + leave_off2;
                }
                int day_idx2 = train_day2 - tr2->saleStart;
                int arr2 = train_day2 * 1440 + tr2->startTime + tr2->arriveTimes[to2];
                int seat2 = min_seats(tr2, day_idx2, from2, to2);
                int price2 = tr2->prices[to2] - tr2->prices[from2];

                int total_time = arr2 - leave1;
                int total_cost = price1 + price2;

                bool better = false;
                if (!found) better = true;
                else {
                    int ka, kb, sa, sb;
                    if (sort_by_time) { ka = total_time; kb = best_total_time; sa = total_cost; sb = best_total_cost; }
                    else { ka = total_cost; kb = best_total_cost; sa = total_time; sb = best_total_time; }
                    if (ka < kb) better = true;
                    else if (ka == kb) {
                        if (sa < sb) better = true;
                        else if (sa == sb) {
                            int c = my_strcmp(tid1, best_t1);
                            if (c < 0) better = true;
                            else if (c == 0) {
                                if (my_strcmp(tid2, best_t2) < 0) better = true;
                            }
                        }
                    }
                }

                if (better) {
                    found = true;
                    best_total_time = total_time;
                    best_total_cost = total_cost;
                    strncpy(best_t1, tid1, 23); best_t1[23] = 0;
                    strncpy(best_t2, tid2, 23); best_t2[23] = 0;
                    strncpy(best_inter, inter, 33); best_inter[33] = 0;
                    best_t1_leave = leave1; best_t1_arr = arr1; best_t1_price = price1; best_t1_seat = seat1;
                    best_t2_leave = leave2; best_t2_arr = arr2; best_t2_price = price2; best_t2_seat = seat2;
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

static void cmd_buy_ticket(const ParsedArgs &a) {
    const char *u = a.get('u'), *i = a.get('i'), *d = a.get('d');
    const char *n = a.get('n'), *f = a.get('f'), *t = a.get('t'), *q = a.get('q');
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
    int oid = new_order_slot();
    Order &o = g_orders[oid];
    strncpy(o.trainID, i, 23); o.trainID[23] = 0;
    strncpy(o.fromStation, f, 33); o.fromStation[33] = 0;
    strncpy(o.toStation, t, 33); o.toStation[33] = 0;
    o.from_idx = from_idx; o.to_idx = to_idx;
    o.day = train_day; o.num = num; o.price = price_per;
    o.leave_time = leave_abs; o.arrive_time = arrive_abs;
    o.timestamp = ++g_timestamp;
    strncpy(o.username, u, 23); o.username[23] = 0;
    o.user_prev_id = -1; o.user_next_id = -1; o.queue_next_id = -1;

    if (avail >= num) {
        take_seats(tr, day_idx, from_idx, to_idx, num);
        o.status = OS_SUCCESS;
        user_orders_push(u, oid);
        long long total = (long long)price_per * num;
        printf("%lld\n", total);
        return;
    }
    if (!standby) {
        // Roll back order slot (we appended)
        g_orders_count--;
        puts("-1"); return;
    }
    o.status = OS_PENDING;
    user_orders_push(u, oid);
    PendingHead *ph = g_pending.get_or_create(i, train_day);
    if (ph->head_id < 0) { ph->head_id = oid; ph->tail_id = oid; }
    else { g_orders[ph->tail_id].queue_next_id = oid; ph->tail_id = oid; }
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
    int p = h ? h->head_id : -1;
    while (p >= 0) { cnt++; p = g_orders[p].user_next_id; }
    printf("%d\n", cnt);
    p = h ? h->head_id : -1;
    while (p >= 0) {
        Order &o = g_orders[p];
        char lt[16], at[16];
        format_datetime(o.leave_time, lt);
        format_datetime(o.arrive_time, at);
        printf("[%s] %s %s %s -> %s %s %d %d\n", status_str(o.status), o.trainID, o.fromStation, lt, o.toStation, at, o.price, o.num);
        p = o.user_next_id;
    }
}

static void try_fulfill_pending(Train *tr, int train_day) {
    PendingHead *ph = g_pending.find(tr->trainID, train_day);
    if (!ph || ph->head_id < 0) return;
    int day_idx = train_day - tr->saleStart;
    int prev = -1;
    int oid = ph->head_id;
    while (oid >= 0) {
        Order &o = g_orders[oid];
        if (o.status != OS_PENDING) {
            int nx = o.queue_next_id;
            if (prev < 0) ph->head_id = nx; else g_orders[prev].queue_next_id = nx;
            if (ph->tail_id == oid) ph->tail_id = prev;
            oid = nx;
            continue;
        }
        int avail = min_seats(tr, day_idx, o.from_idx, o.to_idx);
        if (avail >= o.num) {
            take_seats(tr, day_idx, o.from_idx, o.to_idx, o.num);
            o.status = OS_SUCCESS;
            int nx = o.queue_next_id;
            o.queue_next_id = -1;
            if (prev < 0) ph->head_id = nx; else g_orders[prev].queue_next_id = nx;
            if (ph->tail_id == oid) ph->tail_id = prev;
            oid = nx;
        } else {
            prev = oid;
            oid = o.queue_next_id;
        }
    }
}

static void cmd_refund_ticket(const ParsedArgs &a) {
    const char *u = a.get('u'), *n = a.get('n');
    if (!u) { puts("-1"); return; }
    if (!g_logged.contains(u)) { puts("-1"); return; }
    int idx = n ? atoi(n) : 1;
    if (idx <= 0) { puts("-1"); return; }
    UserOrderHead *h = g_user_orders.find(u);
    if (!h || h->head_id < 0) { puts("-1"); return; }
    int oid = h->head_id;
    int k = 1;
    while (oid >= 0 && k < idx) { oid = g_orders[oid].user_next_id; k++; }
    if (oid < 0) { puts("-1"); return; }
    Order &o = g_orders[oid];
    if (o.status == OS_REFUNDED) { puts("-1"); return; }
    Train *tr = g_trains.get(o.trainID);
    if (o.status == OS_SUCCESS) {
        if (tr) {
            int day_idx = o.day - tr->saleStart;
            return_seats(tr, day_idx, o.from_idx, o.to_idx, o.num);
            o.status = OS_REFUNDED;
            try_fulfill_pending(tr, o.day);
        } else {
            o.status = OS_REFUNDED;
        }
    } else {
        o.status = OS_REFUNDED;
    }
    puts("0");
}

static void cmd_clean() {
    // Reset all globals
    if (g_users.items) delete[] g_users.items;
    g_users.items = nullptr; g_users.cap = 0; g_users.count = 0;

    for (int i = 0; i < g_trains.cap; i++) {
        if (g_trains.items[i].used && g_trains.items[i].seats) delete[] g_trains.items[i].seats;
    }
    if (g_trains.items) delete[] g_trains.items;
    g_trains.items = nullptr; g_trains.cap = 0; g_trains.count = 0;

    for (int i = 0; i < g_stations.cap; i++) {
        if (g_stations.items[i].used && g_stations.items[i].list) delete[] g_stations.items[i].list;
    }
    if (g_stations.items) delete[] g_stations.items;
    g_stations.items = nullptr; g_stations.cap = 0; g_stations.count = 0;

    if (g_user_orders.items) delete[] g_user_orders.items;
    g_user_orders.items = nullptr; g_user_orders.cap = 0; g_user_orders.count = 0;

    if (g_pending.items) delete[] g_pending.items;
    g_pending.items = nullptr; g_pending.cap = 0; g_pending.count = 0;

    if (g_logged.items) delete[] g_logged.items;
    g_logged.items = nullptr; g_logged.cap = 0; g_logged.count = 0;

    if (g_orders) delete[] g_orders;
    g_orders = nullptr; g_orders_count = 0; g_orders_cap = 0;
    g_timestamp = 0;

    g_users.init(256);
    g_trains.init(64);
    g_stations.init(256);
    g_user_orders.init(256);
    g_pending.init(256);
    g_logged.init(64);

    // Remove state file
    remove(STATE_FILE);

    puts("0");
}

int main() {
    static char line[8192];
    static char *tokens[256];

    g_users.init(256);
    g_trains.init(64);
    g_stations.init(256);
    g_user_orders.init(256);
    g_pending.init(256);
    g_logged.init(64);

    load_state(); // Will silently fail on first run

    while (true) {
        int n = read_line(line, sizeof(line));
        if (n < 0) break;
        if (n == 0) continue;

        char *p = line;
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
        else if (my_strcmp(cmd, "exit") == 0) {
            puts("bye");
            save_state();
            return 0;
        }
        else { puts("-1"); }
    }
    // EOF without explicit exit
    save_state();
    return 0;
}
