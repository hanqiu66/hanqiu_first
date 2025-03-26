#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <systemd/sd-bus.h>

#define MAXCONN 2
#define ERRORCODE -1
#define BUFFSIZE 1024

int count_connect = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int create_listen(int port) {
    int listen_st;
    struct sockaddr_in sockaddr;
    int on = 1;
    listen_st = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_st == -1) {
        printf("socket create error:%s \n", strerror(errno));
        return ERRORCODE;
    }
    if (setsockopt(listen_st, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        printf("setsockopt error:%s \n", strerror(errno));
        return ERRORCODE;
    }
    sockaddr.sin_port = htons(port);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_st, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) == -1) {
        printf("bind error:%s \n", strerror(errno));
        return ERRORCODE;
    }
    if (listen(listen_st, 5) == -1) {
        printf("listen error:%s \n", strerror(errno));
        return ERRORCODE;
    }
    return listen_st;
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFSIZE];
    int bytes_received;

    while ((bytes_received = recv(client_socket, buffer, BUFFSIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("Received from client: %s\n", buffer);

        // 调用 D-Bus 服务端的方法
        sd_bus *bus = NULL;
        int r = sd_bus_open_user(&bus);
        if (r < 0) {
            fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-r));
            break;
        }

        // 解析客户端消息
        char *method = strtok(buffer, " ");
        if (!method) {
            send(client_socket, "Invalid request format\n", strlen("Invalid request format\n"), 0);
            sd_bus_unref(bus);
            continue;
        }

        if (strcmp(method, "insert") == 0) {
            const char *name = strtok(NULL, ",");
            int age = atoi(strtok(NULL, ","));
            const char *email = strtok(NULL, ",");

            if (!name || !email) {
                send(client_socket, "Invalid insert format\n", strlen("Invalid insert format\n"), 0);
                sd_bus_unref(bus);
                continue;
            }

            sd_bus_message *reply = NULL;
            r = sd_bus_call_method(bus,
                                   "net.poettering.Calculator", // 服务名称
                                   "/net/poettering/Calculator", // 对象路径
                                   "net.poettering.Calculator", // 接口名称
                                   "DatabaseInsert", // 方法名称
                                   NULL,
                                   &reply,
                                   "sis", // 输入参数类型
                                   name, age, email); // 输入参数值

            if (r < 0) {
                fprintf(stderr, "Failed to call method: %s\n", strerror(-r));
                send(client_socket, "Failed to insert data\n", strlen("Failed to insert data\n"), 0);
            } else {
                const char *result;
                r = sd_bus_message_read(reply, "s", &result);
                if (r < 0) {
                    fprintf(stderr, "Failed to read reply: %s\n", strerror(-r));
                    send(client_socket, "Failed to get result\n", strlen("Failed to get result\n"), 0);
                } else {
                    // 在结果字符串末尾添加换行符
                    size_t result_len = strlen(result);
                    char *result_with_newline = malloc(result_len + 2); // +2 用于换行符和字符串结束符
                    strcpy(result_with_newline, result);
                    strcat(result_with_newline, "\n");
                    send(client_socket, result_with_newline, strlen(result_with_newline), 0);
                    free(result_with_newline);
                    //send(client_socket, result, strlen(result), 0);
                }
                sd_bus_message_unref(reply);
            }
        } else if (strcmp(method, "select") == 0) {
            const char *condition = strtok(NULL, " \n");

            if (!condition) {
                send(client_socket, "Invalid select format\n", strlen("Invalid select format\n"), 0);
                sd_bus_unref(bus);
                continue;
            }

            sd_bus_message *reply = NULL;
            r = sd_bus_call_method(bus,
                                   "net.poettering.Calculator", // 服务名称
                                   "/net/poettering/Calculator", // 对象路径
                                   "net.poettering.Calculator", // 接口名称
                                   "DatabaseSelect", // 方法名称
                                   NULL,
                                   &reply,
                                   "s", // 输入参数类型
                                   condition); // 输入参数值

            if (r < 0) {
                fprintf(stderr, "Failed to call method: %s\n", strerror(-r));
                send(client_socket, "Failed to select data\n", strlen("Failed to select data\n"), 0);
            } else {
                // 处理 a(sis) 类型的回复
                char *reply_str = NULL;
                sd_bus_message_rewind(reply, 1);

                // 遍历数组中的每个结构体
                int array_index = 0;
                while (sd_bus_message_enter_container(reply, 'a', "(sis)") > 0) {
                    while (sd_bus_message_enter_container(reply, 'r', "sis") > 0) {
                        const char *name;
                        int age;
                        const char *email;

                        r = sd_bus_message_read(reply, "sis", &name, &age, &email);
                        if (r < 0) {
                            fprintf(stderr, "Failed to read struct: %s\n", strerror(-r));
                            break;
                        }

                        // 将每个结构体的内容格式化为字符串
                        char struct_str[256];
                        snprintf(struct_str, sizeof(struct_str), "name: %s, age: %d, email: %s\n", name, age, email);

                        // 将所有结构体的内容汇总到一个字符串中
                        if (reply_str == NULL) {
                            reply_str = strdup(struct_str);
                        } else {
                            char *temp = reply_str;
                            reply_str = malloc(strlen(temp) + strlen(struct_str) + 1);
                            strcpy(reply_str, temp);
                            strcat(reply_str, struct_str);
                            free(temp);
                        }

                        sd_bus_message_exit_container(reply);
                    }
                    sd_bus_message_exit_container(reply);
                }

                if (reply_str == NULL) {
                    reply_str = strdup("No data found\n");
                }

                send(client_socket, reply_str, strlen(reply_str), 0);
                free(reply_str);
                sd_bus_message_unref(reply);
            }
        } else {
            send(client_socket, "Unknown command\n", strlen("Unknown command\n"), 0);
        }

        sd_bus_unref(bus);
        memset(buffer, 0, BUFFSIZE);
    }

    if (bytes_received == 0) {
        printf("Client disconnected\n");
    } else if (bytes_received == -1) {
        printf("recv error:%s \n", strerror(errno));
    }

    close(client_socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return -1;
    }

    int port = atoi(argv[1]);
    if (port == 0) {
        printf("Invalid port number\n");
        return -1;
    }

    int listen_st = create_listen(port);
    if (listen_st == ERRORCODE) {
        return -1;
    }

    printf("Server started on port %d\n", port);

    while (1) {
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(listen_st, NULL, NULL);
        if (*client_socket == -1) {
            fprintf(stderr, "accept error:%s \n", strerror(errno));
            free(client_socket);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, client_socket) != 0) {
            fprintf(stderr, "Failed to create thread: %s\n", strerror(errno));
            close(*client_socket);
            free(client_socket);
            continue;
        }

        printf("New client connected\n");
    }

    close(listen_st);
    return 0;
}