#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <sqlite3.h>
#include <errno.h>
#include <signal.h>

#define DATABASE_NAME "server_database.db"

// 定义 UserData 结构体，用于传递 D-Bus 和数据库指针
typedef struct {
    sqlite3 *db;
    sd_bus *bus;
} UserData;

// 初始化数据库
static int init_database(sqlite3 **db) {
    char *err_msg = 0;

    int rc = sqlite3_open(DATABASE_NAME, db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        return rc;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS users ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "name TEXT NOT NULL,"
                      "age INTEGER NOT NULL,"
                      "email TEXT NOT NULL);";

    rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db);
        return rc;
    }

    return SQLITE_OK;
}

// 插入数据到数据库
static int database_insert(sqlite3 *db, const char *name, int age, const char *email) {
    char *err_msg = 0;
    int rc;

    const char *sql = "INSERT INTO users (name, age, email) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, age);
    sqlite3_bind_text(stmt, 3, email, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL step error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

// 查询数据库
static int database_select(sqlite3 *db, const char *name, sqlite3_stmt **stmt) {
    int rc;

    const char *sql = "SELECT name, age, email FROM users WHERE name = ?;";
    rc = sqlite3_prepare_v2(db, sql, -1, stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(*stmt, 1, name, -1, SQLITE_STATIC);
    return SQLITE_OK;
}

// D-Bus 方法：插入数据
static int method_database_insert(sd_bus_message *message, void *userdata, sd_bus_error *error) {
    const char *name;
    int age;
    const char *email;
    UserData *data = (UserData *)userdata;
    sqlite3 *db = data->db;

    printf("Entering method_database_insert\n");

    int r = sd_bus_message_read(message, "sis", &name, &age, &email);
    if (r < 0) {
        fprintf(stderr, "Failed to read message: %s\n", strerror(-r));
        return r;
    }

    printf("Received insert request: name=%s, age=%d, email=%s\n", name, age, email);

    int rc = database_insert(db, name, age, email);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert data\n");
        return -EINVAL;
    }

    printf("Data inserted successfully\n");

    return sd_bus_reply_method_return(message, "s", "Data inserted successfully");
}

// D-Bus 方法：查询数据
static int method_database_select(sd_bus_message *message, void *userdata, sd_bus_error *error) {
    const char *condition;
    UserData *data = (UserData *)userdata;
    sqlite3 *db = data->db;
    sqlite3_stmt *stmt = NULL;
    int rc;

    printf("Entering method_database_select\n");

    int r = sd_bus_message_read(message, "s", &condition);
    if (r < 0) {
        fprintf(stderr, "Failed to read message: %s\n", strerror(-r));
        return r;
    }

    printf("Received select request with condition: %s\n", condition);

    // 构造 SQL 查询语句
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT name, age, email FROM users WHERE %s;", condition);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
        return -EINVAL;
    }

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(message, &reply);
    if (r < 0) {
        fprintf(stderr, "Failed to create reply message: %s\n", strerror(-r));
        sqlite3_finalize(stmt);
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "(sis)");
    if (r < 0) {
        fprintf(stderr, "Failed to open container: %s\n", strerror(-r));
        sqlite3_finalize(stmt);
        sd_bus_message_unref(reply);
        return r;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        int age = sqlite3_column_int(stmt, 1);
        const unsigned char *email = sqlite3_column_text(stmt, 2);

        if (name && email) {
            r = sd_bus_message_append(reply, "(sis)", (const char *)name, age, (const char *)email);
            if (r < 0) {
                fprintf(stderr, "Failed to append result: %s\n", strerror(-r));
                sqlite3_finalize(stmt);
                sd_bus_message_unref(reply);
                return r;
            }
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        fprintf(stderr, "Failed to close container: %s\n", strerror(-r));
        sqlite3_finalize(stmt);
        sd_bus_message_unref(reply);
        return r;
    }

    sqlite3_finalize(stmt);

    r = sd_bus_send(data->bus, reply, NULL);
    if (r < 0) {
        fprintf(stderr, "Failed to send reply: %s\n", strerror(-r));
        sd_bus_message_unref(reply);
        return r;
    }

    sd_bus_message_unref(reply);
    printf("Reply sent successfully\n");

    return 0;
}

// D-Bus 接口定义
static const sd_bus_vtable calculator_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("DatabaseInsert", "sis", "s", method_database_insert, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DatabaseSelect", "s", "a(sis)", method_database_select, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

// 主函数
int main() {
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    sqlite3 *db = NULL;
    UserData *user_data = NULL;
    int r;

    printf("Starting database server...\n");

    // 打开 D-Bus 连接
    r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-r));
        goto finish;
    }

    printf("Connected to D-Bus user bus\n");

    // 初始化数据库
    r = init_database(&db);
    if (r != SQLITE_OK) {
        fprintf(stderr, "Failed to initialize database\n");
        goto finish;
    }

    printf("Database initialized successfully\n");

    // 分配 UserData 结构体
    user_data = malloc(sizeof(UserData));
    if (!user_data) {
        fprintf(stderr, "Failed to allocate memory for user data\n");
        goto finish;
    }
    user_data->db = db;
    user_data->bus = bus;

    // 注册 D-Bus 接口
    r = sd_bus_add_object_vtable(bus, &slot, "/net/poettering/Calculator", "net.poettering.Calculator", calculator_vtable, user_data);
    if (r < 0) {
        fprintf(stderr, "Failed to add object vtable: %s\n", strerror(-r));
        goto finish;
    }

    printf("Object vtable added successfully\n");

    // 请求服务名称
    r = sd_bus_request_name(bus, "net.poettering.Calculator", 0);
    if (r < 0) {
        fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-r));
        goto finish;
    }

    printf("Service name acquired successfully\n");

    printf("Database server is running...\n");

    // 主事件循环
    for (;;) {
        sd_bus_process(bus, NULL);
        sd_bus_wait(bus, (uint64_t)-1);
    }

finish:
    // 清理资源
    if (db) {
        sqlite3_close(db);
        printf("Database closed\n");
    }
    if (slot) {
        sd_bus_slot_unref(slot);
        printf("Bus slot unreferenced\n");
    }
    if (bus) {
        sd_bus_unref(bus);
        printf("Bus unreferenced\n");
    }
    if (user_data) {
        free(user_data);
        printf("User data freed\n");
    }

    return 0;
}