#include <stdio.h>
#include <sqlite3.h>
#include <string.h>

#define DB_NAME "mydatabase.db"
#define TABLE_NAME "users"
#define INPUT_BUFFER_SIZE 100

// 回调函数，用于处理查询结果
static int callback(void *data, int num_columns, char **column_values, char **column_names) {
    int i;
    for (i = 0; i < num_columns; i++) {
        printf("%s: %s\n", column_names[i], column_values[i] ? column_values[i] : "NULL");
    }
    printf("\n");
    return 0;
}

// 插入数据到数据库的函数
int database_insert(sqlite3 *db, const char *table_name, const char *columns[], const char *values[]) {
    // 构造INSERT语句
    char sql[1024];
    int i = 0;

    // 检查参数
    if (!table_name || !columns || !values) {
        fprintf(stderr, "Invalid parameters\n");
        return -1;
    }

    // 构造INSERT语句
    sprintf(sql, "INSERT INTO %s (", table_name);
    for (i = 0; columns[i] != NULL; i++) {
        if (i > 0) {
            strcat(sql, ", ");
        }
        strcat(sql, columns[i]);
    }
    strcat(sql, ") VALUES (");
    for (i = 0; values[i] != NULL; i++) {
        if (i > 0) {
            strcat(sql, ", ");
        }
        strcat(sql, "'");
        strcat(sql, values[i]);
        strcat(sql, "'");
    }
    strcat(sql, ");");

    char *zErrMsg = 0;
    int ret = sqlite3_exec(db, sql, NULL, NULL, &zErrMsg);

    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return -1;
    }

    printf("Data inserted successfully\n");
    return 0;
}

// 查询数据的函数
int database_select(sqlite3 *db, const char *table_name, const char *condition) {
    char sql[1024];
    sprintf(sql, "SELECT * FROM %s WHERE %s;", table_name, condition);

    char *zErrMsg = 0;
    int ret = sqlite3_exec(db, sql, callback, NULL, &zErrMsg);

    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return -1;
    }

    return 0;
}

int main(int argc, char const *argv[]) {
    sqlite3 *db;
    int ret = 0;
    int choice;

    // 打开数据库
    if ((ret = sqlite3_open(DB_NAME, &db)) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // 创建表
    const char *create_table_sql = "CREATE TABLE IF NOT EXISTS users ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "name TEXT NOT NULL,"
                                   "age INTEGER NOT NULL,"
                                   "email TEXT NOT NULL);";

    char *zErrMsg = 0;
    ret = sqlite3_exec(db, create_table_sql, NULL, NULL, &zErrMsg);

    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        sqlite3_close(db);
        return -1;
    }

    // 显示菜单并获取用户选择
    printf("Welcome to the Database Operation Program\n");
    printf("1. Insert Data\n");
    printf("2. Select Data\n");
    printf("Enter your choice (1 or 2): ");
    scanf("%d", &choice);
    getchar(); // 清除输入缓冲区

    if (choice == 1) {
        // 插入数据
        char name[INPUT_BUFFER_SIZE];
        char age[INPUT_BUFFER_SIZE];
        char email[INPUT_BUFFER_SIZE];

        printf("Enter name: ");
        fgets(name, INPUT_BUFFER_SIZE, stdin);
        name[strcspn(name, "\n")] = '\0'; // 去除换行符

        printf("Enter age: ");
        fgets(age, INPUT_BUFFER_SIZE, stdin);
        age[strcspn(age, "\n")] = '\0'; // 去除换行符

        printf("Enter email: ");
        fgets(email, INPUT_BUFFER_SIZE, stdin);
        email[strcspn(email, "\n")] = '\0'; // 去除换行符

        const char *columns[] = {"name", "age", "email", NULL};
        const char *values[] = {name, age, email, NULL};

        if (database_insert(db, TABLE_NAME, columns, values) != 0) {
            fprintf(stderr, "Failed to insert data\n");
        }
    } else if (choice == 2) {
        // 查询数据
        char condition[INPUT_BUFFER_SIZE];
        printf("Enter query condition (e.g., 'age > 25'): ");
        fgets(condition, INPUT_BUFFER_SIZE, stdin);
        condition[strcspn(condition, "\n")] = '\0'; // 去除换行符

        if (database_select(db, TABLE_NAME, condition) != 0) {
            fprintf(stderr, "Failed to select data\n");
        }
    } else {
        printf("Invalid choice\n");
    }

    // 关闭数据库
    sqlite3_close(db);
    return 0;
}