/*
 * FoodRush – Smart Online Food Ordering System
 * json_utils.c – Lightweight JSON builder and tiny parser
 */

#include "json_utils.h"
#include <ctype.h>

/* ── JSON string escaping ────────────────────────────────────────────────── */
void json_escape(const char *src, char *dst, int dst_len)
{
    int j = 0;
    if (!src) { dst[0]='\0'; return; }
    for (int i = 0; src[i] && j < dst_len-2; i++) {
        unsigned char c = (unsigned char)src[i];
        if      (c == '"')  { dst[j++]='\\'; dst[j++]='"'; }
        else if (c == '\\') { dst[j++]='\\'; dst[j++]='\\'; }
        else if (c == '\n') { dst[j++]='\\'; dst[j++]='n'; }
        else if (c == '\r') { dst[j++]='\\'; dst[j++]='r'; }
        else if (c == '\t') { dst[j++]='\\'; dst[j++]='t'; }
        else if (c < 0x20)  { /* skip control chars */ }
        else                { dst[j++] = (char)c; }
    }
    dst[j] = '\0';
}

/* ── Menu item ───────────────────────────────────────────────────────────── */
int json_menu_item(const db_menu_item_t *m, char *buf, int buf_len)
{
    char name[MAX_NAME_LEN*2], desc[MAX_DESC_LEN*2],
         cat[128], img[1024];
    json_escape(m->name,        name, sizeof(name));
    json_escape(m->description, desc, sizeof(desc));
    json_escape(m->category,    cat,  sizeof(cat));
    json_escape(m->image_url,   img,  sizeof(img));
    return snprintf(buf, buf_len,
        "{"
        "\"id\":%d,"
        "\"name\":\"%s\","
        "\"description\":\"%s\","
        "\"category\":\"%s\","
        "\"price\":%.2f,"
        "\"stock\":%d,"
        "\"rating\":%.1f,"
        "\"rating_count\":%d,"
        "\"image_url\":\"%s\","
        "\"is_available\":%s,"
        "\"created_at\":\"%s\""
        "}",
        m->id, name, desc, cat, m->price, m->stock, m->rating,
        m->rating_count, img, m->is_available ? "true" : "false",
        m->created_at);
}

int json_menu_items(const db_menu_item_t *items, int count, char *buf, int buf_len)
{
    int pos = 0;
    pos += snprintf(buf+pos, buf_len-pos, "[");
    for (int i = 0; i < count; i++) {
        if (i) pos += snprintf(buf+pos, buf_len-pos, ",");
        char tmp[4096];
        json_menu_item(&items[i], tmp, sizeof(tmp));
        pos += snprintf(buf+pos, buf_len-pos, "%s", tmp);
        if (pos >= buf_len-200) break;
    }
    pos += snprintf(buf+pos, buf_len-pos, "]");
    return pos;
}

/* ── Order ───────────────────────────────────────────────────────────────── */
int json_order(const db_order_t *o, char *buf, int buf_len)
{
    char cn[MAX_NAME_LEN*2], ce[MAX_EMAIL_LEN*2],
         cp[64], da[MAX_ADDRESS_LEN*2], st[128];
    json_escape(o->customer_name,    cn, sizeof(cn));
    json_escape(o->customer_email,   ce, sizeof(ce));
    json_escape(o->customer_phone,   cp, sizeof(cp));
    json_escape(o->delivery_address, da, sizeof(da));
    json_escape(o->status_str,       st, sizeof(st));

    int pos = snprintf(buf, buf_len,
        "{"
        "\"id\":%d,"
        "\"order_id\":\"%s\","
        "\"user_id\":%d,"
        "\"customer_name\":\"%s\","
        "\"customer_email\":\"%s\","
        "\"customer_phone\":\"%s\","
        "\"delivery_address\":\"%s\","
        "\"total_amount\":%.2f,"
        "\"status\":%d,"
        "\"status_str\":\"%s\","
        "\"worker_thread_id\":%d,"
        "\"processing_time_ms\":%d,"
        "\"created_at\":\"%s\","
        "\"updated_at\":\"%s\","
        "\"items\":[",
        o->id, o->order_id, o->user_id,
        cn, ce, cp, da,
        o->total_amount, o->status, st,
        o->worker_thread_id, o->processing_time_ms,
        o->created_at, o->updated_at);

    for (int i = 0; i < o->item_count; i++) {
        char iname[MAX_NAME_LEN*2];
        json_escape(o->items[i].item_name, iname, sizeof(iname));
        if (i) pos += snprintf(buf+pos, buf_len-pos, ",");
        pos += snprintf(buf+pos, buf_len-pos,
            "{\"menu_item_id\":%d,\"item_name\":\"%s\","
            "\"quantity\":%d,\"unit_price\":%.2f,\"subtotal\":%.2f}",
            o->items[i].menu_item_id, iname,
            o->items[i].quantity, o->items[i].unit_price, o->items[i].subtotal);
    }
    pos += snprintf(buf+pos, buf_len-pos, "]}");
    return pos;
}

int json_orders(const db_order_t *orders, int count, char *buf, int buf_len)
{
    int pos = 0;
    pos += snprintf(buf+pos, buf_len-pos, "[");
    for (int i = 0; i < count; i++) {
        if (i) pos += snprintf(buf+pos, buf_len-pos, ",");
        char tmp[8192];
        json_order(&orders[i], tmp, sizeof(tmp));
        pos += snprintf(buf+pos, buf_len-pos, "%s", tmp);
        if (pos >= buf_len-500) break;
    }
    pos += snprintf(buf+pos, buf_len-pos, "]");
    return pos;
}

/* ── User ────────────────────────────────────────────────────────────────── */
int json_user(const db_user_t *u, char *buf, int buf_len)
{
    char un[MAX_USERNAME_LEN*2], em[MAX_EMAIL_LEN*2],
         fn[MAX_NAME_LEN*2], ph[64], ad[MAX_ADDRESS_LEN*2];
    json_escape(u->username,  un, sizeof(un));
    json_escape(u->email,     em, sizeof(em));
    json_escape(u->full_name, fn, sizeof(fn));
    json_escape(u->phone,     ph, sizeof(ph));
    json_escape(u->address,   ad, sizeof(ad));
    return snprintf(buf, buf_len,
        "{"
        "\"id\":%d,"
        "\"username\":\"%s\","
        "\"email\":\"%s\","
        "\"full_name\":\"%s\","
        "\"phone\":\"%s\","
        "\"address\":\"%s\","
        "\"role\":%d,"
        "\"created_at\":\"%s\""
        "}",
        u->id, un, em, fn, ph, ad, u->role, u->created_at);
}

/* ── Analytics ───────────────────────────────────────────────────────────── */
int json_analytics(const db_analytics_t *a, int active, int queued,
                   int completed, int failed, char *buf, int buf_len)
{
    return snprintf(buf, buf_len,
        "{"
        "\"total_orders\":%d,"
        "\"pending_orders\":%d,"
        "\"delivered_orders\":%d,"
        "\"total_revenue\":%.2f,"
        "\"total_menu_items\":%d,"
        "\"low_stock_items\":%d,"
        "\"active_workers\":%d,"
        "\"queued_tasks\":%d,"
        "\"completed_tasks\":%d,"
        "\"failed_tasks\":%d"
        "}",
        a->total_orders, a->pending_orders, a->delivered_orders,
        a->total_revenue, a->total_menu_items, a->low_stock_items,
        active, queued, completed, failed);
}

/* ── Processing logs ─────────────────────────────────────────────────────── */
int json_proc_logs(const db_proc_log_t *logs, int count, char *buf, int buf_len)
{
    int pos = 0;
    pos += snprintf(buf+pos, buf_len-pos, "[");
    for (int i = 0; i < count; i++) {
        char act[512], res[128];
        json_escape(logs[i].action, act, sizeof(act));
        json_escape(logs[i].result, res, sizeof(res));
        if (i) pos += snprintf(buf+pos, buf_len-pos, ",");
        pos += snprintf(buf+pos, buf_len-pos,
            "{\"id\":%d,\"order_id\":\"%s\",\"thread_id\":%d,"
            "\"action\":\"%s\",\"result\":\"%s\","
            "\"duration_ms\":%d,\"timestamp\":\"%s\"}",
            logs[i].id, logs[i].order_id, logs[i].thread_id,
            act, res, logs[i].duration_ms, logs[i].timestamp);
        if (pos >= buf_len-300) break;
    }
    pos += snprintf(buf+pos, buf_len-pos, "]");
    return pos;
}

/* ── Response wrappers ───────────────────────────────────────────────────── */
int json_ok(const char *data_json, char *buf, int buf_len)
{
    return snprintf(buf, buf_len,
        "{\"status\":\"ok\",\"data\":%s}",
        data_json ? data_json : "null");
}

int json_err(const char *message, char *buf, int buf_len)
{
    char msg[512];
    json_escape(message, msg, sizeof(msg));
    return snprintf(buf, buf_len,
        "{\"status\":\"error\",\"message\":\"%s\"}", msg);
}

/* ── Tiny parser ─────────────────────────────────────────────────────────── */

static const char *find_key(const char *json, const char *key)
{
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

int json_get_string(const char *json, const char *key, char *val, int val_len)
{
    const char *p = find_key(json, key);
    if (!p || *p != '"') return 0;
    p++; /* skip opening quote */
    int j = 0;
    while (*p && *p != '"' && j < val_len-1) {
        if (*p == '\\') {
            p++;
            if      (*p == '"')  val[j++] = '"';
            else if (*p == '\\') val[j++] = '\\';
            else if (*p == 'n')  val[j++] = '\n';
            else if (*p == 't')  val[j++] = '\t';
            else                 val[j++] = *p;
        } else {
            val[j++] = *p;
        }
        p++;
    }
    val[j] = '\0';
    return 1;
}

int json_get_int(const char *json, const char *key, int *val)
{
    const char *p = find_key(json, key);
    if (!p) return 0;
    *val = (int)strtol(p, NULL, 10);
    return 1;
}

int json_get_double(const char *json, const char *key, double *val)
{
    const char *p = find_key(json, key);
    if (!p) return 0;
    *val = strtod(p, NULL);
    return 1;
}
