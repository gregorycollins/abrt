#include <iostream>
#include <dbus/dbus.h>
#include "abrtlib.h"
#include "ABRTException.h"
#include "CrashWatcher.h"
#include "Settings.h"
#include "Daemon.h"
#include "CommLayerServerDBus.h"


static DBusConnection* s_pConn;


/*
 * Helpers for building DBus messages
 */

//static void store_bool(DBusMessageIter* iter, bool val);
static void store_int32(DBusMessageIter* iter, int32_t val);
static void store_uint32(DBusMessageIter* iter, uint32_t val);
static void store_int64(DBusMessageIter* iter, int64_t val);
static void store_uint64(DBusMessageIter* iter, uint64_t val);
static void store_string(DBusMessageIter* iter, const char* val);

//static inline void store_val(DBusMessageIter* iter, bool val)               { store_bool(iter, val); }
static inline void store_val(DBusMessageIter* iter, int32_t val)            { store_int32(iter, val); }
static inline void store_val(DBusMessageIter* iter, uint32_t val)           { store_uint32(iter, val); }
static inline void store_val(DBusMessageIter* iter, int64_t val)            { store_int64(iter, val); }
static inline void store_val(DBusMessageIter* iter, uint64_t val)           { store_uint64(iter, val); }
static inline void store_val(DBusMessageIter* iter, const char* val)        { store_string(iter, val); }
static inline void store_val(DBusMessageIter* iter, const std::string& val) { store_string(iter, val.c_str()); }

//static void store_bool(DBusMessageIter* iter, bool val)
//{
//    dbus_bool_t db = val;
//    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &db))
//        die_out_of_memory();
//}
static void store_int32(DBusMessageIter* iter, int32_t val)
{
    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &val))
        die_out_of_memory();
}
static void store_uint32(DBusMessageIter* iter, uint32_t val)
{
    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &val))
        die_out_of_memory();
}
static void store_int64(DBusMessageIter* iter, int64_t val)
{
    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_INT64, &val))
        die_out_of_memory();
}
static void store_uint64(DBusMessageIter* iter, uint64_t val)
{
    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT64, &val))
        die_out_of_memory();
}
static void store_string(DBusMessageIter* iter, const char* val)
{
    if (!dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &val))
        die_out_of_memory();
}

/* Templates for vector and map */
template <typename T> struct type {};
//template <> struct type<bool>        { static const char* csig() { return "b"; } };
template <> struct type<int32_t>     { static const char* csig() { return "i"; } static std::string sig(); };
template <> struct type<uint32_t>    { static const char* csig() { return "u"; } static std::string sig(); };
template <> struct type<int64_t>     { static const char* csig() { return "x"; } static std::string sig(); };
template <> struct type<uint64_t>    { static const char* csig() { return "t"; } static std::string sig(); };
template <> struct type<std::string> { static const char* csig() { return "s"; } static std::string sig(); };
#define SIG(T) (type<T>::csig() ? type<T>::csig() : type<T>::sig().c_str())
template <typename E>
struct type< std::vector<E> > {
    static const char* csig() { return NULL; }
    static std::string sig() { return ssprintf("a%s", SIG(E)); }
};
template <typename K, typename V>
struct type< std::map<K,V> > {
    static const char* csig() { return NULL; }
    static std::string sig() { return ssprintf("a{%s%s}", SIG(K), SIG(V)); }
};

template<typename E>
static void store_vector(DBusMessageIter* iter, const std::vector<E>& val)
{
    DBusMessageIter sub_iter;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, SIG(E), &sub_iter))
        die_out_of_memory();

    typename std::vector<E>::const_iterator vit = val.begin();
    for (; vit != val.end(); ++vit)
    {
        store_val(&sub_iter, *vit);
    }

    if (!dbus_message_iter_close_container(iter, &sub_iter))
        die_out_of_memory();
}
/*
template<>
static void store_vector(DBus::MessageIter &iter, const std::vector<uint8_t>& val)
{
    if we use such vector, MUST add specialized code here (see in dbus-c++ source)
}
*/
template<typename K, typename V>
static void store_map(DBusMessageIter* iter, const std::map<K,V>& val)
{
    DBusMessageIter sub_iter;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
                ssprintf("{%s%s}", SIG(K), SIG(V)).c_str(),
                &sub_iter))
        die_out_of_memory();

    typename std::map<K,V>::const_iterator mit = val.begin();
    for (; mit != val.end(); ++mit)
    {
        DBusMessageIter sub_sub_iter;
        if (!dbus_message_iter_open_container(&sub_iter, DBUS_TYPE_DICT_ENTRY, NULL, &sub_sub_iter))
            die_out_of_memory();
        store_val(&sub_sub_iter, mit->first);
        store_val(&sub_sub_iter, mit->second);
        if (!dbus_message_iter_close_container(&sub_iter, &sub_sub_iter))
            die_out_of_memory();
    }

    if (!dbus_message_iter_close_container(iter, &sub_iter))
        die_out_of_memory();
}

template<typename E>
static inline void store_val(DBusMessageIter* iter, const std::vector<E>& val) { store_vector(iter, val); }
template<typename K, typename V>
static inline void store_val(DBusMessageIter* iter, const std::map<K,V>& val)  { store_map(iter, val); }


/*
 * Helpers for parsing DBus messages
 */

enum {
    ERROR = -1,
    LAST_FIELD = 0,
    MORE_FIELDS = 1,
};
/* Checks type, loads data, advances to the next arg.
 * Returns TRUE if next arg exists.
 */
//static int load_bool(DBusMessageIter* iter, bool& val);
static int load_int32(DBusMessageIter* iter, int32_t &val);
static int load_uint32(DBusMessageIter* iter, uint32_t &val);
static int load_int64(DBusMessageIter* iter, int64_t &val);
static int load_uint64(DBusMessageIter* iter, uint64_t &val);
static int load_charp(DBusMessageIter* iter, const char*& val);
//static inline int load_val(DBusMessageIter* iter, bool &val)        { return load_bool(iter, val); }
static inline int load_val(DBusMessageIter* iter, int32_t &val)     { return load_int32(iter, val); }
static inline int load_val(DBusMessageIter* iter, uint32_t &val)    { return load_uint32(iter, val); }
static inline int load_val(DBusMessageIter* iter, int64_t &val)     { return load_int64(iter, val); }
static inline int load_val(DBusMessageIter* iter, uint64_t &val)    { return load_uint64(iter, val); }
static inline int load_val(DBusMessageIter* iter, const char*& val) { return load_charp(iter, val); }
static inline int load_val(DBusMessageIter* iter, std::string& val)
{
    const char* str;
    int r = load_charp(iter, str);
    val = str;
    return r;
}

//static int load_bool(DBusMessageIter* iter, bool& val)
//{
//    int type = dbus_message_iter_get_arg_type(iter);
//    if (type != DBUS_TYPE_BOOLEAN)
//        error_msg_and_die("%s expected in dbus message, but not found ('%c')", "bool", type);
//    dbus_bool_t db;
//    dbus_message_iter_get_basic(iter, &db);
//    val = db;
//    return dbus_message_iter_next(iter);
//}
static int load_int32(DBusMessageIter* iter, int32_t& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_INT32)
    {
        error_msg("%s expected in dbus message, but not found ('%c')", "int32", type);
        return -1;
    }
    dbus_message_iter_get_basic(iter, &val);
    return dbus_message_iter_next(iter);
}
static int load_uint32(DBusMessageIter* iter, uint32_t& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_UINT32)
    {
        error_msg("%s expected in dbus message, but not found ('%c')", "uint32", type);
        return -1;
    }
    dbus_message_iter_get_basic(iter, &val);
    return dbus_message_iter_next(iter);
}
static int load_int64(DBusMessageIter* iter, int64_t& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_INT64)
    {
        error_msg("%s expected in dbus message, but not found ('%c')", "int64", type);
        return -1;
    }
    dbus_message_iter_get_basic(iter, &val);
    return dbus_message_iter_next(iter);
}
static int load_uint64(DBusMessageIter* iter, uint64_t& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_UINT64)
    {
        error_msg("%s expected in dbus message, but not found ('%c')", "uint64", type);
        return -1;
    }
    dbus_message_iter_get_basic(iter, &val);
    return dbus_message_iter_next(iter);
}
static int load_charp(DBusMessageIter* iter, const char*& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_STRING)
    {
        error_msg("%s expected in dbus message, but not found ('%c')", "string", type);
        return -1;
    }
    dbus_message_iter_get_basic(iter, &val);
//log("load_charp:'%s'", val);
    return dbus_message_iter_next(iter);
}

/* Templates for vector and map */
template<typename E>
static int load_vector(DBusMessageIter* iter, std::vector<E>& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_ARRAY)
    {
        error_msg("array expected in dbus message, but not found ('%c')", type);
        return -1;
    }

    DBusMessageIter sub_iter;
    dbus_message_iter_recurse(iter, &sub_iter);

    int r;
//int cnt = 0;
    //type = dbus_message_iter_get_arg_type(&sub_iter);
    /* here "type" is the element's type, and it will be checked by load_val */
    // if (type != DBUS_TYPE_INVALID) - not needed?
    do {
        E elem;
//cnt++;
        r = load_val(&sub_iter, elem);
        if (r < 0)
            return r;
        val.push_back(elem);
    } while (r == MORE_FIELDS);
//log("%s: %d elems", __func__, cnt);

    return dbus_message_iter_next(iter);
}
/*
template<>
static int load_vector(DBusMessageIter* iter, std::vector<uint8_t>& val)
{
    if we use such vector, MUST add specialized code here (see in dbus-c++ source)
}
*/
template<typename K, typename V>
static int load_map(DBusMessageIter* iter, std::map<K,V>& val)
{
    int type = dbus_message_iter_get_arg_type(iter);
    if (type != DBUS_TYPE_ARRAY)
    {
        error_msg("array expected in dbus message, but not found ('%c')", type);
        return -1;
    }

    DBusMessageIter sub_iter;
    dbus_message_iter_recurse(iter, &sub_iter);

    bool next_exists;
    int r;
//int cnt = 0;
    do {
        type = dbus_message_iter_get_arg_type(&sub_iter);
        if (type != DBUS_TYPE_DICT_ENTRY)
        {
            error_msg("sub_iter type is not DBUS_TYPE_DICT_ENTRY (%c)!", type);
            return -1;
        }

        DBusMessageIter sub_sub_iter;
        dbus_message_iter_recurse(&sub_iter, &sub_sub_iter);

        K key;
        r = load_val(&sub_sub_iter, key);
        if (r != MORE_FIELDS)
        {
            if (r == LAST_FIELD)
                error_msg("malformed map element in dbus message");
            return -1;
        }
        V value;
        r = load_val(&sub_sub_iter, value);
        if (r != LAST_FIELD)
        {
            if (r == MORE_FIELDS)
                error_msg("malformed map element in dbus message");
            return -1;
        }
        val[key] = value;
//cnt++;
        next_exists = dbus_message_iter_next(&sub_iter);
    } while (next_exists);
//log("%s: %d elems", __func__, cnt);

    return dbus_message_iter_next(iter);
}

template<typename E>
static inline int load_val(DBusMessageIter* iter, std::vector<E>& val) { return load_vector(iter, val); }
template<typename K, typename V>
static inline int load_val(DBusMessageIter* iter, std::map<K,V>& val)  { return load_map(iter, val); }


/*
 * DBus signal emitters
 */

/* helpers */
static DBusMessage* new_signal_msg(const char* member)
{
    /* path, interface, member name */
    DBusMessage* msg = dbus_message_new_signal(CC_DBUS_PATH, CC_DBUS_IFACE, member);
    if (!msg)
        die_out_of_memory();
    return msg;
}
static void send_flush_and_unref(DBusMessage* msg)
{
    if (!dbus_connection_send(s_pConn, msg, NULL /* &serial */))
        error_msg_and_die("Error sending DBus message");
    dbus_connection_flush(s_pConn);
    VERB3 log("DBus message sent");
    dbus_message_unref(msg);
}

/* Notify the clients (UI) about a new crash */
void CCommLayerServerDBus::Crash(const std::string& progname, const std::string& uid)
{
    DBusMessage* msg = new_signal_msg("Crash");
    const char* c_progname = progname.c_str();
    const char* c_uid = uid.c_str();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &c_progname,
            DBUS_TYPE_STRING, &c_uid,
            DBUS_TYPE_INVALID);
    VERB2 log("Sending signal Crash('%s','%s')", c_progname, c_uid);
    send_flush_and_unref(msg);
}

void CCommLayerServerDBus::JobStarted(const char* pDest)
{
    DBusMessage* msg = new_signal_msg("JobStarted");
    /* send unicast dbus signal */
    if (!dbus_message_set_destination(msg, pDest))
        die_out_of_memory();
    uint64_t nJobID = uint64_t(pthread_self());
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &pDest, /* TODO: redundant parameter, remove from API */
            DBUS_TYPE_UINT64, &nJobID, /* TODO: redundant parameter, remove from API */
            DBUS_TYPE_INVALID);
    VERB2 log("Sending signal JobStarted('%s',%llx)", pDest, (unsigned long long)nJobID);
    send_flush_and_unref(msg);
}

void CCommLayerServerDBus::JobDone(const char* pDest, const char* pUUID)
{
    DBusMessage* msg = new_signal_msg("JobDone");
    /* send unicast dbus signal */
    if (!dbus_message_set_destination(msg, pDest))
        die_out_of_memory();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &pDest, /* TODO: redundant parameter, remove from API */
            DBUS_TYPE_STRING, &pUUID, /* TODO: redundant parameter, remove from API */
            DBUS_TYPE_INVALID);
    VERB2 log("Sending signal JobDone('%s','%s')", pDest, pUUID);
    send_flush_and_unref(msg);
}

void CCommLayerServerDBus::Error(const std::string& arg1)
{
    DBusMessage* msg = new_signal_msg("Error");
    const char* c_arg1 = arg1.c_str();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &c_arg1,
            DBUS_TYPE_INVALID);
    send_flush_and_unref(msg);
}

void CCommLayerServerDBus::Update(const std::string& pMessage, uint64_t job_id)
{
    DBusMessage* msg = new_signal_msg("Update");
    const char* c_message = pMessage.c_str();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &c_message,
            DBUS_TYPE_UINT64, &job_id,
            DBUS_TYPE_INVALID);
    send_flush_and_unref(msg);
}

/* TODO: one Warning()? */
void CCommLayerServerDBus::Warning(const std::string& pMessage)
{
    DBusMessage* msg = new_signal_msg("Warning");
    const char* c_message = pMessage.c_str();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &c_message,
            DBUS_TYPE_INVALID);
    send_flush_and_unref(msg);
}

void CCommLayerServerDBus::Warning(const std::string& pMessage, uint64_t job_id)
{
    DBusMessage* msg = new_signal_msg("Warning");
    const char* c_message = pMessage.c_str();
    dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &c_message,
            DBUS_TYPE_UINT64, &job_id,
            DBUS_TYPE_INVALID);
    send_flush_and_unref(msg);
}


/*
 * DBus call handlers
 */

static long get_remote_uid(DBusMessage* call, const char** ppSender = NULL)
{
    DBusError err;
    dbus_error_init(&err);
    const char* sender = dbus_message_get_sender(call);
    if (ppSender)
        *ppSender = sender;
    long uid = dbus_bus_get_unix_user(s_pConn, sender, &err);
    if (dbus_error_is_set(&err))
    {
        dbus_error_free(&err);
        error_msg("can't determine remore uid, assuming 0");
        return 0;
    }
    return uid;
}

static int handle_GetCrashInfos(DBusMessage* call, DBusMessage* reply)
{
    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s() call from uid %ld", "GetCrashInfos", unix_uid);
    vector_crash_infos_t argout1 = GetCrashInfos(to_string(unix_uid));

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    store_val(&iter, argout1);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_CreateReport(DBusMessage* call, DBusMessage* reply)
{
    const char* pUUID;
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "CreateReport");
        return -1;
    }
    int r = load_val(&in_iter, pUUID);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "CreateReport");
        return -1;
    }

    const char* sender;
    long unix_uid = get_remote_uid(call, &sender);
    VERB1 log("got %s('%s') call from sender '%s' uid %ld", "CreateReport", pUUID, sender, unix_uid);
    if (CreateReportThread(pUUID, to_string(unix_uid).c_str(), sender) != 0)
        return -1; /* can't create thread (err msg is already logged) */

    dbus_message_append_args(reply,
                DBUS_TYPE_STRING, &pUUID, /* redundant, eliminate from API */
                DBUS_TYPE_INVALID);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_GetJobResult(DBusMessage* call, DBusMessage* reply)
{
    const char* pUUID;
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "GetJobResult");
        return -1;
    }
    int r = load_val(&in_iter, pUUID);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "GetJobResult");
        return -1;
    }

    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s('%s') call from uid %ld", "GetJobResult", pUUID, unix_uid);
    map_crash_report_t report = GetJobResult(pUUID, to_string(unix_uid).c_str());

    DBusMessageIter out_iter;
    dbus_message_iter_init_append(reply, &out_iter);
    store_val(&out_iter, report);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_Report(DBusMessage* call, DBusMessage* reply)
{
    map_crash_report_t argin1;
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "Report");
        return -1;
    }
    int r = load_val(&in_iter, argin1);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "Report");
        return -1;
    }

    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s(...) call from uid %ld", "Report", unix_uid);
    report_status_t argout1;
    try
    {
        argout1 = Report(argin1, to_string(unix_uid));
    }
    catch (CABRTException &e)
    {
        dbus_message_unref(reply);
        reply = dbus_message_new_error(call, DBUS_ERROR_FAILED, e.what().c_str());
        if (!reply)
            die_out_of_memory();
        send_flush_and_unref(reply);
        return 0;
    }

    DBusMessageIter out_iter;
    dbus_message_iter_init_append(reply, &out_iter);
    store_val(&out_iter, argout1);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_DeleteDebugDump(DBusMessage* call, DBusMessage* reply)
{
    const char* argin1;
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "DeleteDebugDump");
        return -1;
    }
    int r = load_val(&in_iter, argin1);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "DeleteDebugDump");
        return -1;
    }

    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s('%s') call from uid %ld", "DeleteDebugDump", argin1, unix_uid);
    bool argout1 = DeleteDebugDump(argin1, to_string(unix_uid));

    dbus_message_append_args(reply,
                DBUS_TYPE_BOOLEAN, &argout1,
                DBUS_TYPE_INVALID);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_GetPluginsInfo(DBusMessage* call, DBusMessage* reply)
{
    vector_map_string_t plugins_info = g_pPluginManager->GetPluginsInfo();

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    store_val(&iter, plugins_info);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_GetPluginSettings(DBusMessage* call, DBusMessage* reply)
{
    const char* PluginName;
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "GetPluginSettings");
        return -1;
    }
    int r = load_val(&in_iter, PluginName);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "GetPluginSettings");
        return -1;
    }

    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s('%s') call from uid %ld", "GetPluginSettings", PluginName, unix_uid);
    map_plugin_settings_t plugin_settings = g_pPluginManager->GetPluginSettings(PluginName, to_string(unix_uid));

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    store_val(&iter, plugin_settings);
    send_flush_and_unref(reply);
    return 0;
}

static int handle_SetPluginSettings(DBusMessage* call, DBusMessage* reply)
{
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "SetPluginSettings");
        return -1;
    }
    std::string PluginName;
    int r = load_val(&in_iter, PluginName);
    if (r != MORE_FIELDS)
    {
        if (r == LAST_FIELD)
            error_msg("dbus call %s: too few parameters", "SetPluginSettings");
        return -1;
    }
    map_plugin_settings_t plugin_settings;
    r = load_val(&in_iter, plugin_settings);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "SetPluginSettings");
        return -1;
    }

    long unix_uid = get_remote_uid(call);
    VERB1 log("got %s('%s',...) call from uid %ld", "SetPluginSettings", PluginName.c_str(), unix_uid);
    g_pPluginManager->SetPluginSettings(PluginName, to_string(unix_uid), plugin_settings);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_RegisterPlugin(DBusMessage* call, DBusMessage* reply)
{
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "RegisterPlugin");
        return -1;
    }
    const char* PluginName;
    int r = load_val(&in_iter, PluginName);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "RegisterPlugin");
        return -1;
    }

    g_pPluginManager->RegisterPlugin(PluginName);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_UnRegisterPlugin(DBusMessage* call, DBusMessage* reply)
{
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "UnRegisterPlugin");
        return -1;
    }
    const char* PluginName;
    int r = load_val(&in_iter, PluginName);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "UnRegisterPlugin");
        return -1;
    }

    g_pPluginManager->UnRegisterPlugin(PluginName);

    send_flush_and_unref(reply);
    return 0;
}

static int handle_GetSettings(DBusMessage* call, DBusMessage* reply)
{
    map_abrt_settings_t result = GetSettings();

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    store_val(&iter, result);
    send_flush_and_unref(reply);
    return 0;
}

static int handle_SetSettings(DBusMessage* call, DBusMessage* reply)
{
    DBusMessageIter in_iter;
    if (!dbus_message_iter_init(call, &in_iter))
    {
        error_msg("dbus call %s: no parameters", "SetSettings");
        return -1;
    }
    map_abrt_settings_t param1;
    int r = load_val(&in_iter, param1);
    if (r != LAST_FIELD)
    {
        if (r == MORE_FIELDS)
            error_msg("dbus call %s: extra parameters", "SetSettings");
        return -1;
    }

    SetSettings(param1);

    send_flush_and_unref(reply);
    return 0;
}


/*
 * Glib integration machinery
 */

/* Callback: "glib says dbus fd is active" */
static gboolean handle_dbus(GIOChannel *gio, GIOCondition condition, gpointer data)
{
    DBusWatch *watch = (DBusWatch*)data;

    VERB3 log("%s(gio, condition:%x [bits:IN/PRI/OUT/ERR/HUP...], data)", __func__, int(condition));

    /* Notify the D-Bus library when a previously-added watch
     * is ready for reading or writing, or has an exception such as a hangup.
     */
    int glib_flags = int(condition);
    int dbus_flags = 0;
    if (glib_flags & G_IO_IN)  dbus_flags |= DBUS_WATCH_READABLE;
    if (glib_flags & G_IO_OUT) dbus_flags |= DBUS_WATCH_WRITABLE;
    if (glib_flags & G_IO_ERR) dbus_flags |= DBUS_WATCH_ERROR;
    if (glib_flags & G_IO_HUP) dbus_flags |= DBUS_WATCH_HANGUP;
    /*
     * TODO:
     * If dbus_watch_handle returns FALSE, then the file descriptor
     * may still be ready for reading or writing, but more memory
     * is needed in order to do the reading or writing. If you ignore
     * the FALSE return, your application may spin in a busy loop
     * on the file descriptor until memory becomes available,
     * but nothing more catastrophic should happen.
     */
    dbus_watch_handle(watch, dbus_flags);

    while (dbus_connection_dispatch(s_pConn) == DBUS_DISPATCH_DATA_REMAINS)
        VERB3 log("%s: more data to process, looping", __func__);
    return TRUE; /* "glib, do not remove this even source!" */
}
struct watch_app_info_t
{
    GIOChannel *channel;
    guint event_source_id;
    bool watch_enabled;
};
/* Callback: "dbus_watch_get_enabled() may return a different value than it did before" */
static void toggled_watch(DBusWatch *watch, void* data)
{
    VERB3 log("%s(watch:%p, data)", __func__, watch);

    watch_app_info_t* app_info = (watch_app_info_t*)dbus_watch_get_data(watch);
    if (dbus_watch_get_enabled(watch))
    {
        if (!app_info->watch_enabled)
        {
            app_info->watch_enabled = true;
            int dbus_flags = dbus_watch_get_flags(watch);
            int glib_flags = 0;
            if (dbus_flags & DBUS_WATCH_READABLE)
                glib_flags |= G_IO_IN;
            if (dbus_flags & DBUS_WATCH_WRITABLE)
                glib_flags |= G_IO_OUT;
            VERB3 log(" adding watch to glib main loop. dbus_flags:%x glib_flags:%x", dbus_flags, glib_flags);
            app_info->event_source_id = g_io_add_watch(app_info->channel, GIOCondition(glib_flags), handle_dbus, watch);
        }
        /* else: it was already enabled */
    } else {
        if (app_info->watch_enabled)
        {
            app_info->watch_enabled = false;
            /* does it free the hidden GSource too? */
            VERB3 log(" removing watch from glib main loop");
            g_source_remove(app_info->event_source_id);
        }
        /* else: it was already disabled */
    }
}
/* Callback: "libdbus needs a new watch to be monitored by the main loop" */
static dbus_bool_t add_watch(DBusWatch *watch, void* data)
{
    VERB3 log("%s(watch:%p, data)", __func__, watch);

    watch_app_info_t* app_info = (watch_app_info_t*)xzalloc(sizeof(*app_info));
    dbus_watch_set_data(watch, app_info, free);

    int fd = dbus_watch_get_unix_fd(watch);
    VERB3 log(" dbus_watch_get_unix_fd():%d", fd);
    app_info->channel = g_io_channel_unix_new(fd);
    /* _unconditionally_ adding it to event loop would be an error */
    toggled_watch(watch, data);
    return TRUE;
}
/* Callback: "libdbus no longer needs a watch to be monitored by the main loop" */
static void remove_watch(DBusWatch *watch, void* data)
{
    VERB3 log("%s()", __func__);
    watch_app_info_t* app_info = (watch_app_info_t*)dbus_watch_get_data(watch);
    if (app_info->watch_enabled)
    {
        app_info->watch_enabled = false;
        g_source_remove(app_info->event_source_id);
    }
    g_io_channel_unref(app_info->channel);
}

/* Callback: "libdbus needs a new timeout to be monitored by the main loop" */
static dbus_bool_t add_timeout(DBusTimeout *timeout, void* data)
{
    VERB3 log("%s()", __func__);
    return TRUE;
}
/* Callback: "libdbus no longer needs a timeout to be monitored by the main loop" */
static void remove_timeout(DBusTimeout *timeout, void* data)
{
    VERB3 log("%s()", __func__);
}
/* Callback: "dbus_timeout_get_enabled() may return a different value than it did before" */
static void timeout_toggled(DBusTimeout *timeout, void* data)
{
//seems to be never called, let's make it noisy
    log("%s()", __func__);
}

/* Callback: "a message is received to a registered object path" */
static DBusHandlerResult message_received(DBusConnection *conn, DBusMessage *msg, void* data)
{
    const char* member = dbus_message_get_member(msg);
    log("%s(method:'%s')", __func__, member);

    DBusMessage* reply = dbus_message_new_method_return(msg);
    int r = -1;
    if (strcmp(member, "GetCrashInfos") == 0)
        r = handle_GetCrashInfos(msg, reply);
    else if (strcmp(member, "CreateReport") == 0)
        r = handle_CreateReport(msg, reply);
    else if (strcmp(member, "Report") == 0)
        r = handle_Report(msg, reply);
    else if (strcmp(member, "DeleteDebugDump") == 0)
        r = handle_DeleteDebugDump(msg, reply);
    else if (strcmp(member, "GetJobResult") == 0)
        r = handle_GetJobResult(msg, reply);
    else if (strcmp(member, "GetPluginsInfo") == 0)
        r = handle_GetPluginsInfo(msg, reply);
    else if (strcmp(member, "GetPluginSettings") == 0)
        r = handle_GetPluginSettings(msg, reply);
    else if (strcmp(member, "SetPluginSettings") == 0)
        r = handle_SetPluginSettings(msg, reply);
    else if (strcmp(member, "RegisterPlugin") == 0)
        r = handle_RegisterPlugin(msg, reply);
    else if (strcmp(member, "UnRegisterPlugin") == 0)
        r = handle_UnRegisterPlugin(msg, reply);
    else if (strcmp(member, "GetSettings") == 0)
        r = handle_GetSettings(msg, reply);
    else if (strcmp(member, "SetSettings") == 0)
        r = handle_SetSettings(msg, reply);
// NB: C++ binding also handles "Introspect" method, which returns a string.
// It was sending "dummy" introspection answer whick looks like this:
// "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
// "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
// "<node>\n"
// "</node>\n"
// Apart from a warning from abrt-gui, just sending error back works as well.
// NB2: we may want to handle "Disconnected" here too.
    if (r < 0) /* error */
    {
        dbus_message_unref(reply);
        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL)
        {
            reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, "not supported");
            if (!reply)
                die_out_of_memory();
            send_flush_and_unref(reply);
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}
/* Callback: "DBusObjectPathVTable is unregistered (or its connection is freed)" */
static void unregister_vtable(DBusConnection *conn, void* data)
{
    VERB3 log("%s()", __func__);
}
/* Table */
static const DBusObjectPathVTable vtable = {
    /* .unregister_function = */ unregister_vtable,
    /* .message_function    = */ message_received,
};

static void handle_dbus_err(bool error_flag, DBusError *err)
{
    if (dbus_error_is_set(err))
    {
        error_msg("dbus error: %s", err->message);
        /* dbus_error_free(&err); */
        error_flag = true;
    }
    if (!error_flag)
        return;
    error_msg_and_die(
            "error requesting DBus name %s, possible reasons: "
            "abrt run by non-root; dbus config is incorrect",
            CC_DBUS_NAME);
}

/*
 * Initialization works as follows:
 *
 * we call dbus_bus_get
 * we call dbus_connection_set_watch_functions
 *  libdbus calls back add_watch(watch:0x2341090, data), this watch is for writing
 *   we call toggled_watch, but it finds that watch is not to be enabled yet
 *  libdbus calls back add_watch(watch:0x23410e0, data), this watch is for reading
 *   we call toggled_watch, it adds watch's fd to glib main loop with POLLIN
 *  (note: these watches are different objects, but they have the same fd)
 * we call dbus_connection_set_timeout_functions
 * we call dbus_connection_register_object_path
 * we call dbus_bus_request_name
 *  libdbus calls back add_timeout()
 *  libdbus calls back remove_timeout()
 * (therefore there is no code yet in timeout_toggled (see above), it's not used)
 */
CCommLayerServerDBus::CCommLayerServerDBus()
{
    DBusConnection* conn;
    DBusError err;

    dbus_error_init(&err);
    VERB3 log("dbus_bus_get");
    s_pConn = conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    handle_dbus_err(conn == NULL, &err);

//do we need this? why?
//log("dbus_connection_set_dispatch_status_function");
//    dbus_connection_set_dispatch_status_function(conn,
//                dispatch, /* void dispatch(DBusConnection *conn, DBusDispatchStatus new_status, void* data) */
//                NULL, /* data */
//                NULL /* free_data_function */
//    )
    VERB3 log("dbus_connection_set_watch_functions");
    if (!dbus_connection_set_watch_functions(conn,
                add_watch,
                remove_watch,
                toggled_watch,
                NULL, /* data */
                NULL /* free_data_function */
                )
    ) {
        die_out_of_memory();
    }
    VERB3 log("dbus_connection_set_timeout_functions");
    if (!dbus_connection_set_timeout_functions(conn,
                add_timeout,
                remove_timeout,
                timeout_toggled,
                NULL, /* data */
                NULL /* free_data_function */
                )
    ) {
        die_out_of_memory();
    }
    VERB3 log("dbus_connection_register_object_path");
    if (!dbus_connection_register_object_path(conn,
                "/com/redhat/abrt",
                &vtable,
                NULL /* data */
                )
    ) {
        die_out_of_memory();
    }
    VERB3 log("dbus_bus_request_name");
    int rc = dbus_bus_request_name(conn, CC_DBUS_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    handle_dbus_err(rc < 0, &err);
    VERB3 log("dbus init done");
}

CCommLayerServerDBus::~CCommLayerServerDBus()
{
// do we need to do something here?
}
