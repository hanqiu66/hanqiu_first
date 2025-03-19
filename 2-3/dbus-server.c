#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <sqlite3.h>
#include <errno.h>

// 定义数据库文件名
#define DATABASE_NAME "server_database.db"

// 初始化数据库
static int init_database(sqlite3 **db) {
    char *err_msg = 0;

    // 打开或创建数据库
    int rc = sqlite3_open(DATABASE_NAME, db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        return rc;
    }

    // 创建表
    const char *sql = "CREATE TABLE IF NOT EXISTS users ("\
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"\
                      "name TEXT NOT NULL,"\
                      "age INTEGER NOT NULL,"\
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

// 根据条件查询数据从数据库
static int database_select(sqlite3 *db, const char *condition, char ***results, int *count) {
    char *err_msg = 0;
    int rc;

    // 构建查询语句
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT name, age, email FROM users WHERE %s;", condition);

    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    *count = 0;
    *results = NULL;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        int age = sqlite3_column_int(stmt, 1);
        const unsigned char *email = sqlite3_column_text(stmt, 2);

        if (name && email) {
            // 动态分配内存以存储结果
            *results = (char **)realloc(*results, (*count + 1) * sizeof(char *));
            (*results)[*count] = (char *)malloc(3 * sizeof(char *));
            
            (*results)[*count][0] = strdup((const char *)name);
            (*results)[*count][1] = (char *)malloc(10 * sizeof(char));
            snprintf((*results)[*count][1], 10, "%d", age);
            (*results)[*count][2] = strdup((const char *)email);

            (*count)++;
        }
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

// 处理DatabaseInsert方法调用
static int method_database_insert(sd_bus_message *message, void *userdata, sd_bus_error *error) {
    const char *name;
    int age;
    const char *email;
    sqlite3 *db = userdata;

    int r = sd_bus_message_read(message, "sis", &name, &age, &email);
    if (r < 0) {
        return r;
    }

    int rc = database_insert(db, name, age, email);
    if (rc != SQLITE_OK) {
        return -EINVAL;
    }

    return sd_bus_reply_method_return(message, "s", "Data inserted successfully");
}

// 处理DatabaseSelect方法调用
static int method_database_select(sd_bus_message *message, void *userdata, sd_bus_error *error) {
    const char *condition;
    char ***results = NULL;
    int count = 0;
    sqlite3 *db = userdata;

    int r = sd_bus_message_read(message, "s", &condition);
    if (r < 0) {
        return r;
    }

    // 验证条件参数
    if (!condition || strlen(condition) == 0) {
        return -EINVAL;
    }

    int rc = database_select(db, condition, results, &count);
    if (rc != SQLITE_OK) {
        return -EINVAL;
    }

    // 构造回复消息
    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(message, &reply);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "(sis)");
    if (r < 0) {
        return r;
    }

    for (int i = 0; i < count; i++) {
        r = sd_bus_message_open_container(reply, 'r', "sis");
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "s", (*results)[i][0]);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "i", atoi((*results)[i][1]));
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "s", (*results)[i][2]);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_close_container(reply);
        if (r < 0) {
            return r;
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        return r;
    }

    // 发送回复消息
    sd_bus *bus = userdata;
    r = sd_bus_send(bus, reply, NULL);
    if (r < 0) {
        return r;
    }

    // 清理内存
    for (int i = 0; i < count; i++) {
        free((*results)[i][0]);
        free((*results)[i][1]);
        free((*results)[i][2]);
        free((*results)[i]);
    }
    free(*results);

    return 0;
}

// 定义DBus接口的元数据表
static const sd_bus_vtable calculator_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("DatabaseInsert", "sis", "s", method_database_insert, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DatabaseSelect", "s", "as", method_database_select, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int main() {
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    sqlite3 *db = NULL;
    int r;

    // 连接到用户总线
    r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-r));
        goto finish;
    }

    // 初始化数据库并保持连接打开
    r = init_database(&db);
    if (r != SQLITE_OK) {
        fprintf(stderr, "Failed to initialize database\n");
        goto finish;
    }

    // 注册对象路径和接口
    r = sd_bus_add_object_vtable(bus, &slot, "/net/poettering/Calculator", "net.poettering.Calculator", calculator_vtable, db);
    if (r < 0) {
        fprintf(stderr, "Failed to add object vtable: %s\n", strerror(-r));
        goto finish;
    }

    // 请求服务名称
    r = sd_bus_request_name(bus, "net.poettering.Calculator", 0);
    if (r < 0) {
        fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-r));
        goto finish;
    }

    printf("Database server is running...\n");

    // 主循环
    for (;;) {
        sd_bus_process(bus, NULL);
        sd_bus_wait(bus, (uint64_t) -1);
    }

finish:
    if (db) {
        sqlite3_close(db);
    }
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);

    return 0;
}