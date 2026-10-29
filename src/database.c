/*
 * FoodRush – Smart Online Food Ordering System
 * database.c – SQLite persistence layer
 *
 * Uses per-call sqlite3_open / sqlite3_close so each thread gets its own
 * connection (SQLite is not safe to share across threads without care).
 * Inventory deductions use a single serialised mutex to prevent overselling.
 */

#include "database.h"
#include <sqlite3.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms)*1000)
#endif

/* ── Inventory serialisation ─────────────────────────────────────────────── */
static pthread_mutex_t g_inventory_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Simple SHA-256 password hashing ────────────────────────────────────── */
/*
 * We implement a basic hash here so we don't need an external crypto lib.
 * In production use libsodium argon2id – but that requires pacman install.
 * For the demo, we use SHA-256 with a fixed salt prefix.
 */
#define SALT_PREFIX "FoodRush$2024$"

/* Tiny SHA-256 (public-domain) ------------------------------------------- */
typedef uint32_t u32;
typedef uint64_t u64;

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
    0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
    0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))

static void sha256_compress(u32 h[8], const unsigned char *block)
{
    u32 w[64], a,b,c,d,e,f,g,hh, s0,s1,ch,maj,t1,t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((u32)block[i*4]<<24)|((u32)block[i*4+1]<<16)|
               ((u32)block[i*4+2]<<8)|(u32)block[i*4+3];
    for (i = 16; i < 64; i++) {
        s0 = ROR32(w[i-15],7)^ROR32(w[i-15],18)^(w[i-15]>>3);
        s1 = ROR32(w[i-2],17)^ROR32(w[i-2],19)^(w[i-2]>>10);
        w[i] = w[i-16]+s0+w[i-7]+s1;
    }
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];hh=h[7];
    for (i = 0; i < 64; i++) {
        s1  = ROR32(e,6)^ROR32(e,11)^ROR32(e,25);
        ch  = (e&f)^(~e&g);
        t1  = hh+s1+ch+K[i]+w[i];
        s0  = ROR32(a,2)^ROR32(a,13)^ROR32(a,22);
        maj = (a&b)^(a&c)^(b&c);
        t2  = s0+maj;
        hh=g; g=f; f=e; e=d+t1;
        d=c;  c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;
    h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

static void sha256(const unsigned char *in, size_t len, unsigned char out[32])
{
    u32 h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char block[64];
    u64 bits = (u64)len * 8;
    size_t i;

    while (len >= 64) {
        sha256_compress(h, in);
        in += 64; len -= 64;
    }
    memcpy(block, in, len);
    block[len] = 0x80;
    if (len < 56) {
        memset(block+len+1, 0, 55-len);
    } else {
        memset(block+len+1, 0, 63-len);
        sha256_compress(h, block);
        memset(block, 0, 56);
    }
    for (i = 0; i < 8; i++) block[56+i] = (unsigned char)(bits >> (56-8*i));
    sha256_compress(h, block);
    for (i = 0; i < 8; i++) {
        out[i*4]   = (h[i]>>24)&0xff;
        out[i*4+1] = (h[i]>>16)&0xff;
        out[i*4+2] = (h[i]>>8)&0xff;
        out[i*4+3] =  h[i]&0xff;
    }
}

static void hash_password(const char *plain, char hex_out[65])
{
    char salted[512];
    snprintf(salted, sizeof(salted), "%s%s", SALT_PREFIX, plain);
    unsigned char digest[32];
    sha256((unsigned char *)salted, strlen(salted), digest);
    for (int i = 0; i < 32; i++)
        sprintf(hex_out + i*2, "%02x", digest[i]);
    hex_out[64] = '\0';
}

/* ── Session store ───────────────────────────────────────────────────────── */
#define MAX_SESSIONS 512

static session_t  g_sessions[MAX_SESSIONS];
static int        g_session_count = 0;
static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;

void session_store_init(void)
{
    memset(g_sessions, 0, sizeof(g_sessions));
}

static void gen_token(char *buf, int len)
{
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)buf);
    for (int i = 0; i < len-1; i++)
        buf[i] = chars[rand() % (sizeof(chars)-1)];
    buf[len-1] = '\0';
}

char *session_create(int user_id, int role)
{
    pthread_mutex_lock(&g_session_mutex);
    /* evict expired */
    time_t now = time(NULL);
    for (int i = 0; i < g_session_count; ) {
        if (g_sessions[i].expires < now) {
            g_sessions[i] = g_sessions[--g_session_count];
        } else {
            i++;
        }
    }
    if (g_session_count >= MAX_SESSIONS) {
        pthread_mutex_unlock(&g_session_mutex);
        return NULL;
    }
    session_t *s = &g_sessions[g_session_count++];
    gen_token(s->token, sizeof(s->token));
    s->user_id = user_id;
    s->role    = role;
    s->expires = now + 86400; /* 24 h */
    pthread_mutex_unlock(&g_session_mutex);
    return s->token;
}

session_t *session_lookup(const char *token)
{
    if (!token || !*token) return NULL;
    pthread_mutex_lock(&g_session_mutex);
    time_t now = time(NULL);
    for (int i = 0; i < g_session_count; i++) {
        if (strcmp(g_sessions[i].token, token) == 0) {
            if (g_sessions[i].expires >= now) {
                pthread_mutex_unlock(&g_session_mutex);
                return &g_sessions[i];
            } else {
                /* expired */
                g_sessions[i] = g_sessions[--g_session_count];
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
    return NULL;
}

void session_destroy(const char *token)
{
    if (!token) return;
    pthread_mutex_lock(&g_session_mutex);
    for (int i = 0; i < g_session_count; i++) {
        if (strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i] = g_sessions[--g_session_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}

/* ── Database helpers ────────────────────────────────────────────────────── */

static sqlite3 *g_db = NULL;   /* used only for schema init */

static sqlite3 *db_open_new(void)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        LOG_ERR("sqlite3_open failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA busy_timeout=5000;", NULL, NULL, NULL);
    return db;
}

/* ── Schema initialisation ───────────────────────────────────────────────── */

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username      TEXT UNIQUE NOT NULL,"
    "  email         TEXT UNIQUE NOT NULL,"
    "  password_hash TEXT NOT NULL,"
    "  full_name     TEXT,"
    "  phone         TEXT,"
    "  address       TEXT,"
    "  role          INTEGER DEFAULT 0,"
    "  created_at    TEXT DEFAULT (datetime('now'))"
    ");"

    "CREATE TABLE IF NOT EXISTS menu_items ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name          TEXT NOT NULL,"
    "  description   TEXT,"
    "  category      TEXT,"
    "  price         REAL NOT NULL,"
    "  stock         INTEGER DEFAULT 0,"
    "  rating        REAL DEFAULT 4.0,"
    "  rating_count  INTEGER DEFAULT 0,"
    "  image_url     TEXT,"
    "  is_available  INTEGER DEFAULT 1,"
    "  created_at    TEXT DEFAULT (datetime('now'))"
    ");"

    "CREATE TABLE IF NOT EXISTS orders ("
    "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_id           TEXT UNIQUE NOT NULL,"
    "  user_id            INTEGER,"
    "  customer_name      TEXT,"
    "  customer_email     TEXT,"
    "  customer_phone     TEXT,"
    "  delivery_address   TEXT,"
    "  total_amount       REAL,"
    "  status             INTEGER DEFAULT 0,"
    "  worker_thread_id   INTEGER,"
    "  processing_time_ms INTEGER,"
    "  created_at         TEXT DEFAULT (datetime('now')),"
    "  updated_at         TEXT DEFAULT (datetime('now'))"
    ");"

    "CREATE TABLE IF NOT EXISTS order_items ("
    "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_id       TEXT NOT NULL,"
    "  menu_item_id   INTEGER NOT NULL,"
    "  item_name      TEXT,"
    "  quantity       INTEGER,"
    "  unit_price     REAL,"
    "  subtotal       REAL"
    ");"

    "CREATE TABLE IF NOT EXISTS processing_logs ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_id    TEXT,"
    "  thread_id   INTEGER,"
    "  action      TEXT,"
    "  result      TEXT,"
    "  duration_ms INTEGER,"
    "  timestamp   TEXT DEFAULT (datetime('now'))"
    ");";

static const char *SEED_SQL =
    "INSERT OR IGNORE INTO users (username,email,password_hash,full_name,phone,address,role) VALUES"
    "('admin','admin@foodrush.com','placeholder','FoodRush Admin','+91-9000000000','HQ, Chennai',1),"
    "('john','john@example.com','placeholder','John Smith','+91-9000000001','12, MG Road, Bangalore',0),"
    "('priya','priya@example.com','placeholder','Priya Sharma','+91-9000000002','45, Park Street, Mumbai',0);";

/* Seed menu items with real Unsplash food images */
static const char *MENU_SEED_SQL =
    "INSERT INTO menu_items (name,description,category,price,stock,rating,rating_count,image_url,is_available) VALUES"
    "('Margherita Pizza','Classic tomato sauce, fresh mozzarella, basil leaves on hand-tossed crust','Pizza',299,50,4.7,234,'https://images.unsplash.com/photo-1565299624946-b28f40a0ae38?w=600',1),"
    "('Pepperoni Pizza','Loaded with premium pepperoni slices, mozzarella and tangy marinara','Pizza',349,40,4.8,312,'https://images.unsplash.com/photo-1628840042765-356cda07504e?w=600',1),"
    "('BBQ Chicken Pizza','Smoky BBQ sauce, grilled chicken, red onions, cheddar and mozzarella','Pizza',379,35,4.6,198,'https://images.unsplash.com/photo-1513104890138-7c749659a591?w=600',1),"
    "('Truffle Mushroom Pizza','Wild portobello mushrooms, truffle oil, mozzarella, parmesan and fresh thyme','Pizza',399,40,4.8,245,'https://images.unsplash.com/photo-1574071318508-1cdbab80d002?w=600',1),"
    "('Paneer Tikka Pizza','Spiced tandoori paneer, bell peppers, red onions with mint drizzle on thin crust','Pizza',349,45,4.7,215,'https://images.unsplash.com/photo-1593560708920-61dd98c46a4e?w=600',1),"
    "('Classic Burger','Juicy beef patty, lettuce, tomato, cheese, pickles in a brioche bun','Burgers',199,60,4.5,445,'https://images.unsplash.com/photo-1568901346375-23c9450c58cd?w=600',1),"
    "('Double Smash Burger','Two smashed patties, caramelised onions, secret sauce, double cheese','Burgers',279,45,4.9,567,'https://images.unsplash.com/photo-1550547660-d9450f859349?w=600',1),"
    "('Crispy Chicken Burger','Fried chicken fillet, coleslaw, chipotle mayo, brioche bun','Burgers',229,55,4.6,389,'https://images.unsplash.com/photo-1606755962773-d324e0a13086?w=600',1),"
    "('Smoky BBQ Bacon Burger','Charbroiled patty with glazed bacon, cheddar, crispy onion rings and smoky sauce','Burgers',289,50,4.8,410,'https://images.unsplash.com/photo-1586190848861-99aa4a171e90?w=600',1),"
    "('Gourmet Veggie Beast','Crispy vegetable patty, guacamole, pickled jalapenos, Monterey Jack cheese','Burgers',219,60,4.5,280,'https://images.unsplash.com/photo-1525059696034-4967a8e1dca2?w=600',1),"
    "('Paneer Tikka','Tandoor-grilled paneer cubes marinated in spiced yoghurt, with mint chutney','Indian',249,40,4.7,276,'https://images.unsplash.com/photo-1567337710282-00832b415979?w=600',1),"
    "('Butter Chicken','Tender chicken in rich, creamy tomato-butter gravy served with naan','Indian',329,50,4.8,589,'https://images.unsplash.com/photo-1603894584373-5ac82b2ae398?w=600',1),"
    "('Biryani Royale','Fragrant basmati rice layered with spiced mutton, saffron and crispy onions','Indian',369,30,4.9,701,'https://images.unsplash.com/photo-1563379091339-03b21ab4a4f8?w=600',1),"
    "('Hyderabadi Dum Biryani','Slow-cooked long grain basmati rice, tender chicken marinated with authentic spices & saffron','Indian',349,60,4.9,890,'https://images.unsplash.com/photo-1589302168068-964664d93dc0?w=600',1),"
    "('Dal Makhani Royale','Black lentils slow cooked overnight with butter and cream, served with jeera rice','Indian',239,55,4.8,430,'https://images.unsplash.com/photo-1546833999-b9f581a1996d?w=600',1),"
    "('Garlic Butter Naan Basket','Tandoori baked fluffy naan bread glazed with roasted garlic and butter (3 pcs)','Indian',99,120,4.7,620,'https://images.unsplash.com/photo-1626074353765-517a681e40be?w=600',1),"
    "('Caesar Salad','Romaine lettuce, parmesan shavings, croutons, classic Caesar dressing','Salads',179,25,4.3,134,'https://images.unsplash.com/photo-1546793665-c74683f339c1?w=600',1),"
    "('Greek Salad','Fresh cucumbers, tomatoes, olives, feta cheese, red onions, olive oil','Salads',199,20,4.4,112,'https://images.unsplash.com/photo-1540189549336-e6e99c3679fe?w=600',1),"
    "('Mediterranean Quinoa Salad','Organic quinoa, cherry tomatoes, cucumbers, kalamata olives, feta & lemon vinaigrette','Salads',229,30,4.6,180,'https://images.unsplash.com/photo-1512621776951-a57141f2eefd?w=600',1),"
    "('Chocolate Lava Cake','Warm chocolate cake with molten centre, served with vanilla ice cream','Desserts',149,35,4.9,423,'https://images.unsplash.com/photo-1624353365286-3f8d62daad51?w=600',1),"
    "('Mango Cheesecake','Creamy cheesecake with fresh alphonso mango topping on biscuit base','Desserts',169,30,4.8,312,'https://images.unsplash.com/photo-1565958011703-44f9829ba187?w=600',1),"
    "('Sizzling Brownie Sundae','Hot fudge walnut brownie served on a sizzler plate with vanilla gelato','Desserts',189,40,4.9,510,'https://images.unsplash.com/photo-1606313564200-e75d5e30476c?w=600',1),"
    "('Gulab Jamun with Rabri','Warm khoya dumplings steeped in rose cardamom syrup, served with creamy thickened rabri','Desserts',139,50,4.8,390,'https://images.unsplash.com/photo-1589301760014-d929f3979dbc?w=600',1),"
    "('Cold Brew Coffee','Single-origin cold-brew coffee, smooth and naturally sweet','Drinks',99,100,4.6,234,'https://images.unsplash.com/photo-1461023058943-07fcbe16d735?w=600',1),"
    "('Mango Lassi','Creamy blended mango and yoghurt drink, lightly spiced with cardamom','Drinks',89,80,4.7,345,'https://images.unsplash.com/photo-1553361371-9b22f78e8b1d?w=600',1),"
    "('Strawberry Mint Mojito','Crushed fresh strawberries, fresh mint leaves, lime juice, club soda & ice','Drinks',119,90,4.7,310,'https://images.unsplash.com/photo-1513558161293-cdaf765ed2fd?w=600',1),"
    "('Iced Caramel Macchiato','Freshly pulled espresso layered with steamed milk and decadent caramel drizzle','Drinks',129,85,4.8,450,'https://images.unsplash.com/photo-1517256064527-09c73fc73e38?w=600',1),"
    "('Masala Chai','Traditional spiced Indian tea brewed with ginger, cardamom and cinnamon','Drinks',59,150,4.5,567,'https://images.unsplash.com/photo-1571934811356-5cc061b6821f?w=600',1);";

int db_init(void)
{
    sqlite3 *db = db_open_new();
    if (!db) return -1;

    char *err = NULL;
    int rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LOG_ERR("Schema creation failed: %s", err);
        sqlite3_free(err);
        sqlite3_close(db);
        return -1;
    }

    /* Seed default users with hashed passwords */
    char admin_hash[65], john_hash[65], priya_hash[65];
    hash_password("admin123",   admin_hash);
    hash_password("password123", john_hash);
    hash_password("password123", priya_hash);

    char seed_users[2048];
    snprintf(seed_users, sizeof(seed_users),
        "INSERT OR IGNORE INTO users "
        "(username,email,password_hash,full_name,phone,address,role) VALUES"
        "('admin','admin@foodrush.com','%s','FoodRush Admin','+91-9000000000','HQ, Chennai',1),"
        "('john','john@example.com','%s','John Smith','+91-9000000001','12, MG Road, Bangalore',0),"
        "('priya','priya@example.com','%s','Priya Sharma','+91-9000000002','45, Park Street, Mumbai',0);",
        admin_hash, john_hash, priya_hash);

    rc = sqlite3_exec(db, seed_users, NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); }

    int item_count = 0;
    sqlite3_stmt *cnt_stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM menu_items;", -1, &cnt_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(cnt_stmt) == SQLITE_ROW) {
            item_count = sqlite3_column_int(cnt_stmt, 0);
        }
        sqlite3_finalize(cnt_stmt);
    }
    if (item_count < 25) {
        sqlite3_exec(db, "DELETE FROM menu_items;", NULL, NULL, NULL);
        rc = sqlite3_exec(db, MENU_SEED_SQL, NULL, NULL, &err);
        if (rc != SQLITE_OK) { sqlite3_free(err); }
    }

    sqlite3_close(db);
    LOG_INFO("Database initialised: %s", DB_PATH);
    return 0;
}

void db_close(void)
{
    /* No persistent connection */
}

/* ── Users ──────────────────────────────────────────────────────────────── */

result_t db_create_user(const char *username, const char *email,
                        const char *password_plain, const char *full_name,
                        const char *phone, const char *address, int role,
                        int *out_id)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;

    char hash[65];
    hash_password(password_plain, hash);

    const char *sql =
        "INSERT INTO users (username,email,password_hash,full_name,phone,address,role)"
        " VALUES (?,?,?,?,?,?,?);";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,username,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,2,email,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,3,hash,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,4,full_name,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,5,phone,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,6,address,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,7,role);
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            if (out_id) *out_id = (int)sqlite3_last_insert_rowid(db);
            res = RESULT_OK;
        } else if (rc == SQLITE_CONSTRAINT) {
            res = RESULT_CONFLICT;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_authenticate_user(const char *username, const char *password_plain,
                              db_user_t *out)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;

    char hash[65];
    hash_password(password_plain, hash);

    const char *sql =
        "SELECT id,username,email,password_hash,full_name,phone,address,role,created_at"
        " FROM users WHERE username=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_NOT_FOUND;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,username,-1,SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *stored = (const char *)sqlite3_column_text(stmt,3);
            if (stored && strcmp(stored, hash) == 0) {
                if (out) {
                    out->id = sqlite3_column_int(stmt,0);
                    SAFE_STRNCPY(out->username,  (const char*)sqlite3_column_text(stmt,1), MAX_USERNAME_LEN);
                    SAFE_STRNCPY(out->email,     (const char*)sqlite3_column_text(stmt,2), MAX_EMAIL_LEN);
                    SAFE_STRNCPY(out->password_hash, stored, 129);
                    const char *fn = (const char*)sqlite3_column_text(stmt,4);
                    SAFE_STRNCPY(out->full_name, fn ? fn : "", MAX_NAME_LEN);
                    const char *ph = (const char*)sqlite3_column_text(stmt,5);
                    SAFE_STRNCPY(out->phone, ph ? ph : "", MAX_PHONE_LEN);
                    const char *ad = (const char*)sqlite3_column_text(stmt,6);
                    SAFE_STRNCPY(out->address, ad ? ad : "", MAX_ADDRESS_LEN);
                    out->role = sqlite3_column_int(stmt,7);
                    const char *ca = (const char*)sqlite3_column_text(stmt,8);
                    SAFE_STRNCPY(out->created_at, ca ? ca : "", 32);
                }
                res = RESULT_OK;
            } else {
                res = RESULT_UNAUTHORIZED;
            }
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_get_user_by_id(int id, db_user_t *out)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "SELECT id,username,email,password_hash,full_name,phone,address,role,created_at"
        " FROM users WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_NOT_FOUND;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,id);
        if (sqlite3_step(stmt) == SQLITE_ROW && out) {
            out->id = sqlite3_column_int(stmt,0);
            SAFE_STRNCPY(out->username,  (const char*)sqlite3_column_text(stmt,1),MAX_USERNAME_LEN);
            SAFE_STRNCPY(out->email,     (const char*)sqlite3_column_text(stmt,2),MAX_EMAIL_LEN);
            SAFE_STRNCPY(out->full_name, (const char*)sqlite3_column_text(stmt,4),MAX_NAME_LEN);
            SAFE_STRNCPY(out->phone,     (const char*)sqlite3_column_text(stmt,5),MAX_PHONE_LEN);
            SAFE_STRNCPY(out->address,   (const char*)sqlite3_column_text(stmt,6),MAX_ADDRESS_LEN);
            out->role = sqlite3_column_int(stmt,7);
            res = RESULT_OK;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

/* ── Menu ───────────────────────────────────────────────────────────────── */

static void fill_menu_item(sqlite3_stmt *s, db_menu_item_t *m)
{
    m->id           = sqlite3_column_int(s,0);
    SAFE_STRNCPY(m->name,        (const char*)sqlite3_column_text(s,1), MAX_NAME_LEN);
    const char *d = (const char*)sqlite3_column_text(s,2);
    SAFE_STRNCPY(m->description, d ? d : "", MAX_DESC_LEN);
    const char *c = (const char*)sqlite3_column_text(s,3);
    SAFE_STRNCPY(m->category,    c ? c : "", 64);
    m->price        = sqlite3_column_double(s,4);
    m->stock        = sqlite3_column_int(s,5);
    m->rating       = sqlite3_column_double(s,6);
    m->rating_count = sqlite3_column_int(s,7);
    const char *img = (const char*)sqlite3_column_text(s,8);
    SAFE_STRNCPY(m->image_url, img ? img : "", 512);
    m->is_available = sqlite3_column_int(s,9);
    const char *ca  = (const char*)sqlite3_column_text(s,10);
    SAFE_STRNCPY(m->created_at, ca ? ca : "", 32);
}

result_t db_get_menu_items(db_menu_item_t *items, int *count)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "SELECT id,name,description,category,price,stock,rating,rating_count,"
        "image_url,is_available,created_at FROM menu_items ORDER BY category,name;";
    sqlite3_stmt *stmt = NULL;
    *count = 0;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && *count < MAX_MENU_ITEMS) {
            fill_menu_item(stmt, &items[(*count)++]);
        }
        res = RESULT_OK;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_get_menu_item_by_id(int id, db_menu_item_t *out)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "SELECT id,name,description,category,price,stock,rating,rating_count,"
        "image_url,is_available,created_at FROM menu_items WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_NOT_FOUND;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            fill_menu_item(stmt, out);
            res = RESULT_OK;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_create_menu_item(db_menu_item_t *m, int *out_id)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "INSERT INTO menu_items (name,description,category,price,stock,rating,"
        "rating_count,image_url,is_available) VALUES (?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,m->name,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,2,m->description,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,3,m->category,-1,SQLITE_STATIC);
        sqlite3_bind_double(stmt,4,m->price);
        sqlite3_bind_int(stmt,5,m->stock);
        sqlite3_bind_double(stmt,6,m->rating);
        sqlite3_bind_int(stmt,7,m->rating_count);
        sqlite3_bind_text(stmt,8,m->image_url,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,9,m->is_available);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            if (out_id) *out_id = (int)sqlite3_last_insert_rowid(db);
            res = RESULT_OK;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_update_menu_item(db_menu_item_t *m)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "UPDATE menu_items SET name=?,description=?,category=?,price=?,stock=?,"
        "rating=?,is_available=?,image_url=? WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,m->name,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,2,m->description,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,3,m->category,-1,SQLITE_STATIC);
        sqlite3_bind_double(stmt,4,m->price);
        sqlite3_bind_int(stmt,5,m->stock);
        sqlite3_bind_double(stmt,6,m->rating);
        sqlite3_bind_int(stmt,7,m->is_available);
        sqlite3_bind_text(stmt,8,m->image_url,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,9,m->id);
        if (sqlite3_step(stmt) == SQLITE_DONE) res = RESULT_OK;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

/* ── Stock deduction – uses passed-in connection (must hold transaction) ── */
result_t db_update_menu_stock(int item_id, int delta, sqlite3 *db)
{
    /* delta < 0 means consuming stock; delta > 0 means restoring */
    const char *sql =
        "UPDATE menu_items SET stock = stock + ? WHERE id=? AND stock + ? >= 0;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_INSUFFICIENT_STOCK;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,delta);
        sqlite3_bind_int(stmt,2,item_id);
        sqlite3_bind_int(stmt,3,delta);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            if (sqlite3_changes(db) > 0) res = RESULT_OK;
        }
    }
    sqlite3_finalize(stmt);
    return res;
}

/* ── Orders ─────────────────────────────────────────────────────────────── */

/*
 * CRITICAL CONCURRENCY PATH:
 *   1. Lock g_inventory_mutex
 *   2. Open dedicated connection
 *   3. BEGIN IMMEDIATE transaction (blocks other writers)
 *   4. Validate & deduct stock for every item
 *   5. Insert order + order_items
 *   6. COMMIT
 *   7. Unlock g_inventory_mutex
 * This guarantees stock never goes negative even under concurrent load.
 */
result_t db_create_order(db_order_t *o)
{
    pthread_mutex_lock(&g_inventory_mutex);

    sqlite3 *db = db_open_new();
    if (!db) {
        pthread_mutex_unlock(&g_inventory_mutex);
        return RESULT_ERR;
    }

    char *err = NULL;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(db);
        pthread_mutex_unlock(&g_inventory_mutex);
        return RESULT_ERR;
    }

    /* Deduct stock for each item */
    for (int i = 0; i < o->item_count; i++) {
        result_t r = db_update_menu_stock(o->items[i].menu_item_id,
                                          -o->items[i].quantity, db);
        if (r != RESULT_OK) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            sqlite3_close(db);
            pthread_mutex_unlock(&g_inventory_mutex);
            LOG_ERR("Insufficient stock for item %d", o->items[i].menu_item_id);
            return RESULT_INSUFFICIENT_STOCK;
        }
    }

    /* Insert order */
    const char *order_sql =
        "INSERT INTO orders "
        "(order_id,user_id,customer_name,customer_email,customer_phone,"
        "delivery_address,total_amount,status,worker_thread_id,processing_time_ms)"
        " VALUES (?,?,?,?,?,?,?,0,?,?);";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;

    if (sqlite3_prepare_v2(db, order_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,o->order_id,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,2,o->user_id);
        sqlite3_bind_text(stmt,3,o->customer_name,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,4,o->customer_email,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,5,o->customer_phone,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,6,o->delivery_address,-1,SQLITE_STATIC);
        sqlite3_bind_double(stmt,7,o->total_amount);
        sqlite3_bind_int(stmt,8,o->worker_thread_id);
        sqlite3_bind_int(stmt,9,o->processing_time_ms);

        if (sqlite3_step(stmt) == SQLITE_DONE) res = RESULT_OK;
    }
    sqlite3_finalize(stmt);

    if (res != RESULT_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        pthread_mutex_unlock(&g_inventory_mutex);
        return res;
    }

    /* Insert order_items */
    const char *item_sql =
        "INSERT INTO order_items "
        "(order_id,menu_item_id,item_name,quantity,unit_price,subtotal)"
        " VALUES (?,?,?,?,?,?);";

    for (int i = 0; i < o->item_count; i++) {
        sqlite3_stmt *ist = NULL;
        if (sqlite3_prepare_v2(db, item_sql, -1, &ist, NULL) == SQLITE_OK) {
            sqlite3_bind_text(ist,1,o->order_id,-1,SQLITE_STATIC);
            sqlite3_bind_int(ist,2,o->items[i].menu_item_id);
            sqlite3_bind_text(ist,3,o->items[i].item_name,-1,SQLITE_STATIC);
            sqlite3_bind_int(ist,4,o->items[i].quantity);
            sqlite3_bind_double(ist,5,o->items[i].unit_price);
            sqlite3_bind_double(ist,6,o->items[i].subtotal);
            if (sqlite3_step(ist) != SQLITE_DONE) res = RESULT_ERR;
        }
        sqlite3_finalize(ist);
        if (res != RESULT_OK) break;
    }

    if (res != RESULT_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    } else {
        if (sqlite3_exec(db, "COMMIT;", NULL, NULL, &err) != SQLITE_OK) {
            sqlite3_free(err);
            res = RESULT_ERR;
        }
    }

    sqlite3_close(db);
    pthread_mutex_unlock(&g_inventory_mutex);
    return res;
}

result_t db_get_orders(db_order_t *orders, int *count, int user_id)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;

    const char *sql =
        "SELECT id,order_id,user_id,customer_name,customer_email,customer_phone,"
        "delivery_address,total_amount,status,worker_thread_id,processing_time_ms,"
        "created_at,updated_at FROM orders"
        " WHERE (? = -1 OR user_id = ?) ORDER BY created_at DESC;";
    sqlite3_stmt *stmt = NULL;
    *count = 0;
    result_t res = RESULT_ERR;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,user_id);
        sqlite3_bind_int(stmt,2,user_id);
        while (sqlite3_step(stmt) == SQLITE_ROW && *count < MAX_ORDERS) {
            db_order_t *o = &orders[(*count)++];
            o->id                = sqlite3_column_int(stmt,0);
            SAFE_STRNCPY(o->order_id,         (const char*)sqlite3_column_text(stmt,1),ORDER_ID_LEN);
            o->user_id           = sqlite3_column_int(stmt,2);
            SAFE_STRNCPY(o->customer_name,    (const char*)sqlite3_column_text(stmt,3),MAX_NAME_LEN);
            SAFE_STRNCPY(o->customer_email,   (const char*)sqlite3_column_text(stmt,4),MAX_EMAIL_LEN);
            SAFE_STRNCPY(o->customer_phone,   (const char*)sqlite3_column_text(stmt,5),MAX_PHONE_LEN);
            SAFE_STRNCPY(o->delivery_address, (const char*)sqlite3_column_text(stmt,6),MAX_ADDRESS_LEN);
            o->total_amount      = sqlite3_column_double(stmt,7);
            o->status            = sqlite3_column_int(stmt,8);
            SAFE_STRNCPY(o->status_str, order_status_str((order_status_t)o->status), 64);
            o->worker_thread_id  = sqlite3_column_int(stmt,9);
            o->processing_time_ms = sqlite3_column_int(stmt,10);
            SAFE_STRNCPY(o->created_at, (const char*)sqlite3_column_text(stmt,11), 32);
            const char *ua = (const char*)sqlite3_column_text(stmt,12);
            SAFE_STRNCPY(o->updated_at, ua ? ua : "", 32);
            o->item_count = 0;
        }
        res = RESULT_OK;
    }
    sqlite3_finalize(stmt);

    /* Load items for each order */
    if (res == RESULT_OK) {
        const char *isql =
            "SELECT menu_item_id,item_name,quantity,unit_price,subtotal"
            " FROM order_items WHERE order_id=?;";
        for (int i = 0; i < *count; i++) {
            sqlite3_stmt *ist = NULL;
            if (sqlite3_prepare_v2(db, isql, -1, &ist, NULL) == SQLITE_OK) {
                sqlite3_bind_text(ist,1,orders[i].order_id,-1,SQLITE_STATIC);
                while (sqlite3_step(ist) == SQLITE_ROW &&
                       orders[i].item_count < MAX_ORDER_ITEMS) {
                    db_order_item_t *oi = &orders[i].items[orders[i].item_count++];
                    oi->menu_item_id = sqlite3_column_int(ist,0);
                    SAFE_STRNCPY(oi->item_name,(const char*)sqlite3_column_text(ist,1),MAX_NAME_LEN);
                    oi->quantity   = sqlite3_column_int(ist,2);
                    oi->unit_price = sqlite3_column_double(ist,3);
                    oi->subtotal   = sqlite3_column_double(ist,4);
                }
            }
            sqlite3_finalize(ist);
        }
    }

    sqlite3_close(db);
    return res;
}

result_t db_get_order_by_id(const char *order_id, db_order_t *out)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "SELECT id,order_id,user_id,customer_name,customer_email,customer_phone,"
        "delivery_address,total_amount,status,worker_thread_id,processing_time_ms,"
        "created_at,updated_at FROM orders WHERE order_id=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_NOT_FOUND;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,order_id,-1,SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out->id = sqlite3_column_int(stmt,0);
            SAFE_STRNCPY(out->order_id,         (const char*)sqlite3_column_text(stmt,1),ORDER_ID_LEN);
            out->user_id = sqlite3_column_int(stmt,2);
            SAFE_STRNCPY(out->customer_name,    (const char*)sqlite3_column_text(stmt,3),MAX_NAME_LEN);
            SAFE_STRNCPY(out->customer_email,   (const char*)sqlite3_column_text(stmt,4),MAX_EMAIL_LEN);
            SAFE_STRNCPY(out->customer_phone,   (const char*)sqlite3_column_text(stmt,5),MAX_PHONE_LEN);
            SAFE_STRNCPY(out->delivery_address, (const char*)sqlite3_column_text(stmt,6),MAX_ADDRESS_LEN);
            out->total_amount = sqlite3_column_double(stmt,7);
            out->status       = sqlite3_column_int(stmt,8);
            SAFE_STRNCPY(out->status_str, order_status_str((order_status_t)out->status), 64);
            out->worker_thread_id   = sqlite3_column_int(stmt,9);
            out->processing_time_ms = sqlite3_column_int(stmt,10);
            SAFE_STRNCPY(out->created_at, (const char*)sqlite3_column_text(stmt,11), 32);
            const char *ua = (const char*)sqlite3_column_text(stmt,12);
            SAFE_STRNCPY(out->updated_at, ua ? ua : "", 32);
            out->item_count = 0;
            res = RESULT_OK;
        }
    }
    sqlite3_finalize(stmt);

    if (res == RESULT_OK) {
        const char *isql =
            "SELECT menu_item_id,item_name,quantity,unit_price,subtotal"
            " FROM order_items WHERE order_id=?;";
        sqlite3_stmt *ist = NULL;
        if (sqlite3_prepare_v2(db, isql, -1, &ist, NULL) == SQLITE_OK) {
            sqlite3_bind_text(ist,1,order_id,-1,SQLITE_STATIC);
            while (sqlite3_step(ist) == SQLITE_ROW && out->item_count < MAX_ORDER_ITEMS) {
                db_order_item_t *oi = &out->items[out->item_count++];
                oi->menu_item_id = sqlite3_column_int(ist,0);
                SAFE_STRNCPY(oi->item_name,(const char*)sqlite3_column_text(ist,1),MAX_NAME_LEN);
                oi->quantity   = sqlite3_column_int(ist,2);
                oi->unit_price = sqlite3_column_double(ist,3);
                oi->subtotal   = sqlite3_column_double(ist,4);
            }
        }
        sqlite3_finalize(ist);
    }

    sqlite3_close(db);
    return res;
}

result_t db_update_order_status(const char *order_id, order_status_t status)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "UPDATE orders SET status=?, updated_at=datetime('now') WHERE order_id=?;";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,(int)status);
        sqlite3_bind_text(stmt,2,order_id,-1,SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE) res = RESULT_OK;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

/* ── Processing logs ────────────────────────────────────────────────────── */

result_t db_log_processing(const char *order_id, int thread_id,
                           const char *action, const char *result_str, int dur_ms)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "INSERT INTO processing_logs (order_id,thread_id,action,result,duration_ms)"
        " VALUES (?,?,?,?,?);";
    sqlite3_stmt *stmt = NULL;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,order_id,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,2,thread_id);
        sqlite3_bind_text(stmt,3,action,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,4,result_str,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,5,dur_ms);
        if (sqlite3_step(stmt) == SQLITE_DONE) res = RESULT_OK;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

result_t db_get_proc_logs(db_proc_log_t *logs, int *count)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;
    const char *sql =
        "SELECT id,order_id,thread_id,action,result,duration_ms,timestamp"
        " FROM processing_logs ORDER BY id DESC LIMIT 200;";
    sqlite3_stmt *stmt = NULL;
    *count = 0;
    result_t res = RESULT_ERR;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            db_proc_log_t *l = &logs[(*count)++];
            l->id = sqlite3_column_int(stmt,0);
            SAFE_STRNCPY(l->order_id, (const char*)sqlite3_column_text(stmt,1), ORDER_ID_LEN);
            l->thread_id  = sqlite3_column_int(stmt,2);
            SAFE_STRNCPY(l->action, (const char*)sqlite3_column_text(stmt,3), 256);
            SAFE_STRNCPY(l->result, (const char*)sqlite3_column_text(stmt,4), 64);
            l->duration_ms = sqlite3_column_int(stmt,5);
            SAFE_STRNCPY(l->timestamp, (const char*)sqlite3_column_text(stmt,6), 32);
        }
        res = RESULT_OK;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return res;
}

/* ── Analytics ──────────────────────────────────────────────────────────── */

result_t db_get_analytics(db_analytics_t *out)
{
    sqlite3 *db = db_open_new();
    if (!db) return RESULT_ERR;

    memset(out, 0, sizeof(*out));
    sqlite3_stmt *stmt = NULL;
    const char *sql;

    sql = "SELECT COUNT(*) FROM orders;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->total_orders=sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt); stmt=NULL;

    sql = "SELECT COUNT(*) FROM orders WHERE status=0;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->pending_orders=sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt); stmt=NULL;

    sql = "SELECT COUNT(*) FROM orders WHERE status=4;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->delivered_orders=sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt); stmt=NULL;

    sql = "SELECT COALESCE(SUM(total_amount),0) FROM orders WHERE status != 5;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->total_revenue=sqlite3_column_double(stmt,0);
    sqlite3_finalize(stmt); stmt=NULL;

    sql = "SELECT COUNT(*) FROM menu_items;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->total_menu_items=sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt); stmt=NULL;

    sql = "SELECT COUNT(*) FROM menu_items WHERE stock < 5 AND is_available=1;";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK)
        if (sqlite3_step(stmt)==SQLITE_ROW) out->low_stock_items=sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    return RESULT_OK;
}
