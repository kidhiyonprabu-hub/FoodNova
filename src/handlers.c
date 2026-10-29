/*
 * FoodRush – Smart Online Food Ordering System
 * handlers.c – All HTTP route handlers
 */

#include "handlers.h"
#include "database.h"
#include "json_utils.h"
#include "router.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms)*1000)
#endif

thread_pool_t *g_pool = NULL;

/* ── Helper: get session from request ──────────────────────────────────── */
static session_t *get_session(http_request_t *req)
{
    /* Token from Authorization: Bearer <token> */
    const char *hdr = req->auth_token;
    if (!hdr || !*hdr) return NULL;
    const char *tok = hdr;
    if (strncmp(tok, "Bearer ", 7) == 0) tok += 7;
    return session_lookup(tok);
}

/* ── Generate unique order ID ───────────────────────────────────────────── */
static void gen_order_id(char *buf)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", tm);
    int rnd = rand() % 99999;
    snprintf(buf, ORDER_ID_LEN, "FR%s%05d", ts, rnd);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AUTH HANDLERS
 * ══════════════════════════════════════════════════════════════════════════ */

void handle_post_register(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    char username[MAX_USERNAME_LEN]="", email[MAX_EMAIL_LEN]="",
         password[MAX_PASSWORD_LEN]="", full_name[MAX_NAME_LEN]="",
         phone[MAX_PHONE_LEN]="", address[MAX_ADDRESS_LEN]="";

    json_get_string(req->body, "username", username, sizeof(username));
    json_get_string(req->body, "email",    email,    sizeof(email));
    json_get_string(req->body, "password", password, sizeof(password));
    json_get_string(req->body, "full_name",full_name,sizeof(full_name));
    json_get_string(req->body, "phone",    phone,    sizeof(phone));
    json_get_string(req->body, "address",  address,  sizeof(address));

    if (!*username || !*email || !*password) {
        char err[256]; json_err("username, email and password are required", err, sizeof(err));
        respond_json(res, 400, err); return;
    }

    int new_id = 0;
    result_t r = db_create_user(username, email, password, full_name, phone, address, 0, &new_id);
    if (r == RESULT_CONFLICT) {
        char err[256]; json_err("Username or email already exists", err, sizeof(err));
        respond_json(res, 409, err); return;
    }
    if (r != RESULT_OK) {
        char err[256]; json_err("Registration failed", err, sizeof(err));
        respond_json(res, 500, err); return;
    }

    char *token = session_create(new_id, ROLE_CUSTOMER);
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"status\":\"ok\",\"token\":\"%s\",\"user_id\":%d,\"role\":0}",
        token ? token : "", new_id);
    respond_json(res, 201, body);
}

void handle_post_login(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    char username[MAX_USERNAME_LEN]="", password[MAX_PASSWORD_LEN]="";
    json_get_string(req->body, "username", username, sizeof(username));
    json_get_string(req->body, "password", password, sizeof(password));

    db_user_t user; memset(&user,0,sizeof(user));
    result_t r = db_authenticate_user(username, password, &user);

    if (r == RESULT_NOT_FOUND || r == RESULT_UNAUTHORIZED) {
        char err[256]; json_err("Invalid username or password", err, sizeof(err));
        respond_json(res, 401, err); return;
    }
    if (r != RESULT_OK) {
        char err[256]; json_err("Login failed", err, sizeof(err));
        respond_json(res, 500, err); return;
    }

    char *token = session_create(user.id, user.role);
    char udata[2048]; json_user(&user, udata, sizeof(udata));
    char body[4096];
    snprintf(body, sizeof(body),
        "{\"status\":\"ok\",\"token\":\"%s\",\"user\":%s}",
        token ? token : "", udata);
    respond_json(res, 200, body);
    LOG_INFO("User '%s' logged in (role=%d)", username, user.role);
}

void handle_post_logout(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    const char *tok = req->auth_token;
    if (tok && strncmp(tok,"Bearer ",7)==0) tok+=7;
    session_destroy(tok);
    char body[64]; json_ok("null", body, sizeof(body));
    respond_json(res, 200, body);
}

void handle_get_me(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    session_t *s = get_session(req);
    if (!s) {
        char err[256]; json_err("Unauthorised", err, sizeof(err));
        respond_json(res, 401, err); return;
    }
    db_user_t user; memset(&user,0,sizeof(user));
    if (db_get_user_by_id(s->user_id, &user) != RESULT_OK) {
        char err[256]; json_err("User not found", err, sizeof(err));
        respond_json(res, 404, err); return;
    }
    char udata[2048]; json_user(&user, udata, sizeof(udata));
    char body[3072]; json_ok(udata, body, sizeof(body));
    respond_json(res, 200, body);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MENU HANDLERS
 * ══════════════════════════════════════════════════════════════════════════ */

void handle_get_menu(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)req; (void)pp;
    static db_menu_item_t items[MAX_MENU_ITEMS];
    int count = 0;
    if (db_get_menu_items(items, &count) != RESULT_OK) {
        char err[256]; json_err("Failed to fetch menu", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    static char data[MAX_JSON_LEN];
    json_menu_items(items, count, data, sizeof(data));
    char body[MAX_JSON_LEN + 32];
    snprintf(body, sizeof(body), "{\"status\":\"ok\",\"data\":%s}", data);
    respond_json(res, 200, body);
}

void handle_get_menu_item(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)req;
    int id = pp ? atoi(pp) : 0;
    db_menu_item_t m; memset(&m,0,sizeof(m));
    result_t r = db_get_menu_item_by_id(id, &m);
    if (r == RESULT_NOT_FOUND) {
        char err[256]; json_err("Menu item not found", err, sizeof(err));
        respond_json(res, 404, err); return;
    }
    char data[4096]; json_menu_item(&m, data, sizeof(data));
    char body[5120]; json_ok(data, body, sizeof(body));
    respond_json(res, 200, body);
}

void handle_post_menu(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    session_t *s = get_session(req);
    if (!s || s->role != ROLE_ADMIN) {
        char err[256]; json_err("Admin access required", err, sizeof(err));
        respond_json(res, 403, err); return;
    }
    db_menu_item_t m; memset(&m,0,sizeof(m));
    json_get_string(req->body, "name",        m.name,        sizeof(m.name));
    json_get_string(req->body, "description", m.description, sizeof(m.description));
    json_get_string(req->body, "category",    m.category,    sizeof(m.category));
    json_get_string(req->body, "image_url",   m.image_url,   sizeof(m.image_url));
    json_get_double(req->body, "price",       &m.price);
    json_get_int(req->body,    "stock",       &m.stock);
    m.rating = 4.0; m.is_available = 1;

    if (!*m.name || m.price <= 0) {
        char err[256]; json_err("name and price are required", err, sizeof(err));
        respond_json(res, 400, err); return;
    }
    int new_id = 0;
    if (db_create_menu_item(&m, &new_id) != RESULT_OK) {
        char err[256]; json_err("Failed to create menu item", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    m.id = new_id;
    char data[4096]; json_menu_item(&m, data, sizeof(data));
    char body[5120]; json_ok(data, body, sizeof(body));
    respond_json(res, 201, body);
}

void handle_put_menu_item(http_request_t *req, http_response_t *res, const char *pp)
{
    session_t *s = get_session(req);
    if (!s || s->role != ROLE_ADMIN) {
        char err[256]; json_err("Admin access required", err, sizeof(err));
        respond_json(res, 403, err); return;
    }
    int id = pp ? atoi(pp) : 0;
    db_menu_item_t m; memset(&m,0,sizeof(m));
    if (db_get_menu_item_by_id(id, &m) != RESULT_OK) {
        char err[256]; json_err("Menu item not found", err, sizeof(err));
        respond_json(res, 404, err); return;
    }
    /* Patch only provided fields */
    char tmp_name[MAX_NAME_LEN]="", tmp_desc[MAX_DESC_LEN]="",
         tmp_cat[64]="", tmp_img[512]="";
    double tmp_price = -1; int tmp_stock = -1, tmp_avail = -1;

    if (json_get_string(req->body, "name",        tmp_name, sizeof(tmp_name))  && *tmp_name)
        SAFE_STRNCPY(m.name, tmp_name, sizeof(m.name));
    if (json_get_string(req->body, "description", tmp_desc, sizeof(tmp_desc))  && *tmp_desc)
        SAFE_STRNCPY(m.description, tmp_desc, sizeof(m.description));
    if (json_get_string(req->body, "category",    tmp_cat,  sizeof(tmp_cat))   && *tmp_cat)
        SAFE_STRNCPY(m.category, tmp_cat, sizeof(m.category));
    if (json_get_string(req->body, "image_url",   tmp_img,  sizeof(tmp_img))   && *tmp_img)
        SAFE_STRNCPY(m.image_url, tmp_img, sizeof(m.image_url));
    if (json_get_double(req->body, "price",  &tmp_price)  && tmp_price > 0) m.price = tmp_price;
    if (json_get_int(req->body,    "stock",  &tmp_stock)  && tmp_stock >= 0) m.stock = tmp_stock;
    if (json_get_int(req->body,    "is_available", &tmp_avail) && tmp_avail >= 0) m.is_available = tmp_avail;

    if (db_update_menu_item(&m) != RESULT_OK) {
        char err[256]; json_err("Update failed", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    char data[4096]; json_menu_item(&m, data, sizeof(data));
    char body[5120]; json_ok(data, body, sizeof(body));
    respond_json(res, 200, body);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ORDER PROCESSING (threaded)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    db_order_t order;
    int        result_status;   /* -1=pending, 0=ok, 1=fail */
} order_task_t;

static void process_order_task(void *arg)
{
    order_task_t *t = (order_task_t *)arg;
    db_order_t   *o = &t->order;

    long tid = (long)(unsigned long)pthread_self();
    o->worker_thread_id = (int)(tid & 0x7fffffff);

    LOG_INFO("Processing order %s on TID=%lu", o->order_id, (unsigned long)pthread_self());
    db_log_processing(o->order_id, o->worker_thread_id,
                      "START processing", "in-progress", 0);

    /* Simulate realistic processing time */
    int processing_delay = 200 + rand() % 300; /* 200-500 ms */
    SLEEP_MS(processing_delay);

    clock_t start = clock();
    result_t r = db_create_order(o);
    int dur = (int)(((double)(clock()-start)/CLOCKS_PER_SEC)*1000) + processing_delay;

    if (r == RESULT_OK) {
        o->processing_time_ms = dur;
        db_update_order_status(o->order_id, ORDER_CONFIRMED);
        db_log_processing(o->order_id, o->worker_thread_id,
                          "Order created and confirmed", "SUCCESS", dur);
        t->result_status = 0;
        LOG_INFO("Order %s SUCCESS (%d ms)", o->order_id, dur);
    } else if (r == RESULT_INSUFFICIENT_STOCK) {
        db_log_processing(o->order_id, o->worker_thread_id,
                          "Insufficient stock", "FAIL_STOCK", dur);
        t->result_status = 1;
        LOG_ERR("Order %s FAILED – insufficient stock", o->order_id);
    } else {
        db_log_processing(o->order_id, o->worker_thread_id,
                          "Database error", "FAIL_DB", dur);
        t->result_status = 1;
        LOG_ERR("Order %s FAILED – db error", o->order_id);
    }
    free(t);
}

void handle_post_order(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;

    /* Parse order JSON */
    char cname[MAX_NAME_LEN]="", cemail[MAX_EMAIL_LEN]="",
         cphone[MAX_PHONE_LEN]="", caddr[MAX_ADDRESS_LEN]="";

    if (!json_get_string(req->body, "customer_name",    cname,  sizeof(cname)) || !*cname) {
        json_get_string(req->body, "name", cname, sizeof(cname));
    }
    if (!json_get_string(req->body, "customer_email",   cemail, sizeof(cemail)) || !*cemail) {
        json_get_string(req->body, "email", cemail, sizeof(cemail));
    }
    if (!json_get_string(req->body, "phone",            cphone, sizeof(cphone)) || !*cphone) {
        json_get_string(req->body, "customer_phone",   cphone, sizeof(cphone));
    }
    if (!json_get_string(req->body, "delivery_address", caddr,  sizeof(caddr)) || !*caddr) {
        json_get_string(req->body, "address", caddr, sizeof(caddr));
    }

    if (!*cname || !*cemail || !*caddr) {
        char err[256]; json_err("customer_name, customer_email and delivery_address are required", err, sizeof(err));
        respond_json(res, 400, err); return;
    }

    /* Parse items array – we do this manually for simplicity */
    order_task_t *task = (order_task_t *)calloc(1, sizeof(order_task_t));
    if (!task) {
        char err[256]; json_err("Out of memory", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    db_order_t *o = &task->order;
    gen_order_id(o->order_id);
    SAFE_STRNCPY(o->customer_name,    cname,  sizeof(o->customer_name));
    SAFE_STRNCPY(o->customer_email,   cemail, sizeof(o->customer_email));
    SAFE_STRNCPY(o->customer_phone,   cphone, sizeof(o->customer_phone));
    SAFE_STRNCPY(o->delivery_address, caddr,  sizeof(o->delivery_address));

    /* session optional */
    session_t *s = get_session(req);
    o->user_id = s ? s->user_id : 0;

    /* Parse items array: [{"menu_item_id":1,"quantity":2},...] */
    const char *items_start = strstr(req->body, "\"items\"");
    if (!items_start) items_start = strstr(req->body, "\"cart\"");
    if (!items_start) {
        char err[256]; json_err("Order items are required", err, sizeof(err));
        free(task); respond_json(res, 400, err); return;
    }
    items_start = strchr(items_start, '[');
    if (!items_start) {
        char err[256]; json_err("Invalid items array", err, sizeof(err));
        free(task); respond_json(res, 400, err); return;
    }

    const char *p = items_start + 1;
    double total = 0.0;
    while (*p && *p != ']' && o->item_count < MAX_ORDER_ITEMS) {
        if (*p == '{') {
            /* find this object's end */
            int depth=1;
            const char *obj_start = p;
            const char *q = p+1;
            while (*q && depth>0) {
                if (*q=='{') depth++;
                else if (*q=='}') depth--;
                q++;
            }
            char obj[512];
            int obj_len = (int)(q - obj_start);
            if (obj_len >= (int)sizeof(obj)) obj_len = sizeof(obj)-1;
            strncpy(obj, obj_start, obj_len);
            obj[obj_len] = '\0';

            int mid=0, qty=0;
            if (!json_get_int(obj, "menu_item_id", &mid) || mid <= 0) {
                json_get_int(obj, "item_id", &mid);
            }
            if (!json_get_int(obj, "quantity", &qty) || qty <= 0) {
                json_get_int(obj, "qty", &qty);
            }

            if (mid > 0 && qty > 0) {
                db_menu_item_t m; memset(&m,0,sizeof(m));
                if (db_get_menu_item_by_id(mid, &m) == RESULT_OK && m.is_available) {
                    db_order_item_t *oi = &o->items[o->item_count++];
                    oi->menu_item_id = mid;
                    oi->quantity     = qty;
                    oi->unit_price   = m.price;
                    oi->subtotal     = m.price * qty;
                    SAFE_STRNCPY(oi->item_name, m.name, sizeof(oi->item_name));
                    total += oi->subtotal;
                }
            }
            p = q;
        } else {
            p++;
        }
    }

    if (o->item_count == 0) {
        char err[256]; json_err("No valid items in order", err, sizeof(err));
        free(task); respond_json(res, 400, err); return;
    }
    o->total_amount = total;
    task->result_status = -1;

    /* Submit to thread pool */
    if (pool_submit(g_pool, process_order_task, task) != 0) {
        /* Queue full – process synchronously */
        process_order_task(task);
        /* task was freed inside process_order_task */
        char err[256]; json_err("Server busy, please retry", err, sizeof(err));
        respond_json(res, 503, err); return;
    }

    /* Return immediately with order ID (processing is async) */
    char body[512];
    snprintf(body, sizeof(body),
        "{\"status\":\"ok\",\"order_id\":\"%s\","
        "\"message\":\"Order received and queued for processing\","
        "\"total_amount\":%.2f}",
        o->order_id, o->total_amount);
    respond_json(res, 202, body);
    LOG_INFO("Order %s queued (%.2f INR, %d items)", o->order_id, total, o->item_count);
}

void handle_get_orders(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    session_t *s = get_session(req);
    int uid = -1; /* -1 = all (admin) */
    if (s && s->role != ROLE_ADMIN) uid = s->user_id;

    static db_order_t orders[MAX_ORDERS];
    int count = 0;
    if (db_get_orders(orders, &count, uid) != RESULT_OK) {
        char err[256]; json_err("Failed to fetch orders", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    static char data[MAX_JSON_LEN];
    json_orders(orders, count, data, sizeof(data));
    char body[MAX_JSON_LEN + 32];
    snprintf(body, sizeof(body), "{\"status\":\"ok\",\"data\":%s}", data);
    respond_json(res, 200, body);
}

void handle_get_order(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)req;
    if (!pp || !*pp) {
        char err[256]; json_err("Order ID required", err, sizeof(err));
        respond_json(res, 400, err); return;
    }
    db_order_t o; memset(&o,0,sizeof(o));
    result_t r = db_get_order_by_id(pp, &o);
    if (r == RESULT_NOT_FOUND) {
        char err[256]; json_err("Order not found", err, sizeof(err));
        respond_json(res, 404, err); return;
    }
    char data[8192]; json_order(&o, data, sizeof(data));
    char body[9216]; json_ok(data, body, sizeof(body));
    respond_json(res, 200, body);
}

void handle_put_order_status(http_request_t *req, http_response_t *res, const char *pp)
{
    session_t *s = get_session(req);
    if (!s || s->role != ROLE_ADMIN) {
        char err[256]; json_err("Admin access required", err, sizeof(err));
        respond_json(res, 403, err); return;
    }
    if (!pp || !*pp) {
        char err[256]; json_err("Order ID required", err, sizeof(err));
        respond_json(res, 400, err); return;
    }
    int status_int = 0;
    json_get_int(req->body, "status", &status_int);
    result_t r = db_update_order_status(pp, (order_status_t)status_int);
    if (r != RESULT_OK) {
        char err[256]; json_err("Update failed", err, sizeof(err));
        respond_json(res, 500, err); return;
    }
    char body[128]; json_ok("null", body, sizeof(body));
    respond_json(res, 200, body);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ANALYTICS
 * ══════════════════════════════════════════════════════════════════════════ */

void handle_get_analytics(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)req; (void)pp;
    db_analytics_t a; memset(&a,0,sizeof(a));
    db_get_analytics(&a);

    pool_stats_t ps; memset(&ps,0,sizeof(ps));
    if (g_pool) pool_stats(g_pool, &ps);

    char data[2048];
    json_analytics(&a, ps.active_workers, ps.queued_tasks,
                   ps.completed_tasks, ps.failed_tasks, data, sizeof(data));
    char body[3072]; json_ok(data, body, sizeof(body));
    respond_json(res, 200, body);
}

void handle_get_proc_logs(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)req; (void)pp;
    static db_proc_log_t logs[200];
    int count = 0;
    db_get_proc_logs(logs, &count);
    static char data[MAX_JSON_LEN];
    json_proc_logs(logs, count, data, sizeof(data));
    char body[MAX_JSON_LEN + 32];
    snprintf(body, sizeof(body), "{\"status\":\"ok\",\"data\":%s}", data);
    respond_json(res, 200, body);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CONCURRENCY SIMULATOR
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char order_id[ORDER_ID_LEN];
    int  sim_item_id;
    int  qty;
    char cname[64];
    char caddr[128];
    char cemail[64];
} sim_task_arg_t;

static void sim_order_task(void *arg)
{
    sim_task_arg_t *a = (sim_task_arg_t *)arg;

    /* Build a minimal order */
    order_task_t *t = (order_task_t *)calloc(1, sizeof(order_task_t));
    if (!t) { free(a); return; }
    db_order_t *o = &t->order;

    SAFE_STRNCPY(o->order_id,         a->order_id, sizeof(o->order_id));
    SAFE_STRNCPY(o->customer_name,    a->cname,    sizeof(o->customer_name));
    SAFE_STRNCPY(o->delivery_address, a->caddr,    sizeof(o->delivery_address));
    SAFE_STRNCPY(o->customer_email,   a->cemail,   sizeof(o->customer_email));
    SAFE_STRNCPY(o->customer_phone,   "+91-0000000000", sizeof(o->customer_phone));
    o->user_id = 0;

    db_menu_item_t m; memset(&m,0,sizeof(m));
    if (db_get_menu_item_by_id(a->sim_item_id, &m) == RESULT_OK) {
        o->items[0].menu_item_id = m.id;
        o->items[0].quantity     = a->qty;
        o->items[0].unit_price   = m.price;
        o->items[0].subtotal     = m.price * a->qty;
        SAFE_STRNCPY(o->items[0].item_name, m.name, sizeof(o->items[0].item_name));
        o->item_count    = 1;
        o->total_amount  = o->items[0].subtotal;
    } else {
        free(t); free(a); return;
    }

    free(a);
    process_order_task(t); /* frees t */
}

void handle_post_simulate(http_request_t *req, http_response_t *res, const char *pp)
{
    (void)pp;
    session_t *s = get_session(req);
    if (!s || s->role != ROLE_ADMIN) {
        char err[256]; json_err("Admin access required", err, sizeof(err));
        respond_json(res, 403, err); return;
    }

    int count = 10;
    json_get_int(req->body, "count", &count);
    if (count < 1)  count = 1;
    if (count > 50) count = 50;

    /* Get first available menu item with enough stock */
    db_menu_item_t items[MAX_MENU_ITEMS]; int item_count=0;
    db_get_menu_items(items, &item_count);
    if (item_count == 0) {
        char err[256]; json_err("No menu items", err, sizeof(err));
        respond_json(res, 400, err); return;
    }

    int submitted = 0, rejected = 0;
    for (int i = 0; i < count; i++) {
        sim_task_arg_t *a = (sim_task_arg_t *)calloc(1, sizeof(sim_task_arg_t));
        if (!a) break;

        /* Round-robin through menu items */
        int idx = i % item_count;
        a->sim_item_id = items[idx].id;
        a->qty         = 1;
        gen_order_id(a->order_id);
        snprintf(a->cname,  sizeof(a->cname),  "SimUser%d", i+1);
        snprintf(a->caddr,  sizeof(a->caddr),  "Sim Address #%d", i+1);
        snprintf(a->cemail, sizeof(a->cemail), "sim%d@test.com", i+1);

        if (pool_submit(g_pool, sim_order_task, a) == 0) {
            submitted++;
        } else {
            free(a); rejected++;
        }
    }

    char body[512];
    snprintf(body, sizeof(body),
        "{\"status\":\"ok\","
        "\"submitted\":%d,"
        "\"rejected\":%d,"
        "\"message\":\"Simulation running – check /api/analytics and /api/logs\"}",
        submitted, rejected);
    respond_json(res, 200, body);
    LOG_INFO("Simulation: %d orders submitted, %d rejected", submitted, rejected);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ROUTE REGISTRATION
 * ══════════════════════════════════════════════════════════════════════════ */

void handlers_register_all(thread_pool_t *pool)
{
    g_pool = pool;

    /* Auth */
    route_register("POST",   "/api/auth/register", handle_post_register, 0);
    route_register("POST",   "/api/auth/login",    handle_post_login,    0);
    route_register("POST",   "/api/auth/logout",   handle_post_logout,   0);
    route_register("GET",    "/api/auth/me",        handle_get_me,        1);

    /* Menu */
    route_register("GET",    "/api/menu",           handle_get_menu,      0);
    route_register("GET",    "/api/menu/",          handle_get_menu_item, 0);
    route_register("POST",   "/api/menu",           handle_post_menu,     2);
    route_register("PUT",    "/api/menu/",          handle_put_menu_item, 2);

    /* Orders */
    route_register("POST",   "/api/orders",         handle_post_order,    0);
    route_register("GET",    "/api/orders",         handle_get_orders,    0);
    route_register("GET",    "/api/orders/",        handle_get_order,     0);
    route_register("PUT",    "/api/orders/",        handle_put_order_status, 2);

    /* Analytics & concurrency */
    route_register("GET",    "/api/analytics",      handle_get_analytics, 0);
    route_register("GET",    "/api/logs",           handle_get_proc_logs, 0);
    route_register("POST",   "/api/simulate",       handle_post_simulate, 2);
}
