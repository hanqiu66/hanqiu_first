#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<pthread.h>
#include <sqlite3.h>
#define MAXCONN 2
#define ERRORCODE -1
#define BUFFSIZE 1024
#define DB_NAME "mydatabase.db"
#define TABLE_NAME "users"
#define INPUT_BUFFER_SIZE 100
int count_connect = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;
struct pthread_socket
{
	int socket_d;
	pthread_t thrd;
};

void trim(char *str) {
    if (str == NULL) return;

    // 去除开头的空格
    while (isspace((unsigned char)*str)) {
        str++;
    }

    // 去除末尾的空格和换行符
    size_t len = strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '\n')) {
        len--;
    }
    str[len] = '\0';
}

static int callback(void *data, int num_columns, char **column_values, char **column_names) {
    int i;
	char buf2[BUFFSIZE];
	int sd = *((int *)data);
    for (i = 0; i < num_columns; i++) {
        
		memset(buf2, 0, sizeof(buf2));
		strcpy(buf2, column_names[i]);
        strcat(buf2, ":");
        strcat(buf2, column_values[i]); 
		strcat(buf2, "\n");
		send(sd, buf2, strlen(buf2), 0);

    }
    
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
int database_select(sqlite3 *db, const char *table_name, const char *condition,int sd) {
    char sql[1024];
    sprintf(sql, "SELECT * FROM %s WHERE %s;", table_name, condition);

    char *zErrMsg = 0;
    int ret = sqlite3_exec(db, sql, callback, &sd, &zErrMsg);

    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return -1;
    }

    return 0;
}

static void *thread_send(void *arg)
{
	char buf[BUFFSIZE];
	int sd = *(int *) arg;
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "Welcome to the Database Operation Program\n 1. Insert Data 2. Select Data\n Enter your choice (1 or 2):\n");
	if (send(sd, buf, strlen(buf), 0) == -1)
	{
		printf("send error:%s \n", strerror(errno));
		return NULL;
	}
	while (1)
	{
		memset(buf, 0, sizeof(buf));
		read(STDIN_FILENO, buf, sizeof(buf));
		if (send(sd, buf, strlen(buf), 0) == -1)
		{
			printf("send error:%s \n", strerror(errno));
			break;
		}
	}
	return NULL;
}
static void* thread_recv(void *arg)
{
	char buf[BUFFSIZE];
    char buf1[BUFFSIZE];
	struct pthread_socket *pt = (struct pthread_socket *) arg;
	int sd = pt->socket_d;
	pthread_t thrd = pt->thrd;
	while (1)
	{
		memset(buf, 0, sizeof(buf));
		int rv = recv(sd, buf, sizeof(buf), 0); //是阻塞的
		if (rv < 0){
			printf("recv error:%s \n", strerror(errno));
			break;
            }
        	if (rv == 0) // 这种情况说明client已经关闭socket连接
        	{
            		break;
        	}
			buf[rv] = '\0';
			trim(buf);
        	printf("%s", buf); //输出接受到内容
            if (strcmp(buf, "1") == 0){
                //插入数据
                char name[INPUT_BUFFER_SIZE] = {0};
                char age[INPUT_BUFFER_SIZE] = {0};
                char email[INPUT_BUFFER_SIZE] = {0};
                memset(buf1, 0, sizeof(buf1));
                strcpy(buf1,"Enter name:\n");
				printf("Sending message: %s\n", buf1);
                if (send(sd, buf1, strlen(buf1), 0) == -1){
                    printf("send error:%s \n", strerror(errno));
                    return NULL;
                }
                int is = recv(sd, name, INPUT_BUFFER_SIZE, 0);
                if (is <= 0){
                    break;
                }
                name[is] = '\0';
                memset(buf1, 0, sizeof(buf1));
                strcpy(buf1,"Enter age:\n");
				printf("Sending message: %s\n", buf1);
                if (send(sd, buf1, strlen(buf1), 0) == -1){
                    printf("send error:%s \n", strerror(errno));
                    return NULL;
                }
                is = recv(sd, age, INPUT_BUFFER_SIZE, 0);
                if (is <= 0){
                    break;
                }
                age[is] = '\0';
                memset(buf1, 0, sizeof(buf1));
                strcpy(buf1,"Enter email:\n");
				printf("Sending message: %s\n", buf1);
                if (send(sd, buf1, strlen(buf1), 0) == -1){
                    printf("send error:%s \n", strerror(errno));
                    return NULL;
                }
                is = recv(sd, email, INPUT_BUFFER_SIZE, 0);
                if (is <= 0){
                    break;
                }
                email[is] = '\0';
                const char *columns[] = {"name", "age", "email", NULL};
                const char *values[] = {name, age, email, NULL};
                if (database_insert(db, TABLE_NAME, columns, values) != 0) {
					memset(buf1, 0, sizeof(buf1));
                    strcpy(buf1,"Failed to insert data\n");
                    send(sd, buf1, strlen(buf1), 0);
                }
	
            }
			else if (strcmp(buf, "2") == 0){
				//查询数据
				char condition[INPUT_BUFFER_SIZE];
				memset(buf1, 0, sizeof(buf1));
                strcpy(buf1,"Enter query condition (e.g., 'age > 25'):\n");
                if (send(sd, buf1, strlen(buf1), 0) == -1){
                    printf("send error:%s \n", strerror(errno));
                    return NULL;
                }
				int sc = recv(sd, condition, INPUT_BUFFER_SIZE, 0);
                if (sc <= 0){
                    break;
                }
                condition[sc] = '\0';
				if (database_select(db, TABLE_NAME, condition,sd) != 0) {
					memset(buf1, 0, sizeof(buf1));
                    strcpy(buf1,"Failed to select data\n");
                    send(sd, buf1, strlen(buf1), 0);
					
				}
			}
			else{
				printf("input error:\n");
				memset(buf1, 0, sizeof(buf1));
                strcpy(buf1, "Invalid input\n");
                send(sd, buf1, strlen(buf1), 0);
			}
			memset(buf1, 0, sizeof(buf1));
            strcpy(buf1, "1. Insert Data 2. Select Data\n Enter your choice (1 or 2):\n");
            send(sd, buf1, strlen(buf1), 0);
            
    	}
    	pthread_cancel(thrd);
    	pthread_mutex_lock(&mutex);
    	count_connect--;
    	pthread_mutex_unlock(&mutex);
    	close(sd);
    	return NULL;
}
 
static int create_listen(int port)
{
 
    	int listen_st;
    	struct sockaddr_in sockaddr; //定义IP地址结构
    	int on = 1;
    	listen_st = socket(AF_INET, SOCK_STREAM, 0); //初始化socket
    	if (listen_st == -1)
    	{
        	printf("socket create error:%s \n", strerror(errno));
        	return ERRORCODE;
    	}
    	if (setsockopt(listen_st, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) //设置ip地址可重用
    	{
        	printf("setsockopt error:%s \n", strerror(errno));
        	return ERRORCODE;
    	}
    	sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    	sockaddr.sin_family = AF_INET;    //设置结构类型为TCP/IP
    	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);    //服务端是等待别人来连，不需要找谁的ip
    	//这里写一个长量INADDR_ANY表示server上所有ip，这个一个server可能有多个ip地址，因为可能有多块网卡
    	if (bind(listen_st, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) == -1)
    	{
       		printf("bind error:%s \n", strerror(errno));
        	return ERRORCODE;
    	}
 
    	if (listen(listen_st, 5) == -1) //     服务端开始监听
    	{
        	printf("listen error:%s \n", strerror(errno));
        	return ERRORCODE;
    	}
    	return listen_st;
}
 
int accept_socket(int listen_st)
{
    	int accept_st;
    	struct sockaddr_in accept_sockaddr; //定义accept IP地址结构
    	socklen_t addrlen = sizeof(accept_sockaddr);
    	memset(&accept_sockaddr, 0, addrlen);
    	accept_st = accept(listen_st, (struct sockaddr*) &accept_sockaddr,&addrlen);
    	//accept 会阻塞直到客户端连接连过来 服务端这个socket只负责listen 是不是有客服端连接过来了
    	//是通过accept返回socket通信的
    	if (accept_st == -1)
    	{
        	printf("accept error:%s \n", strerror(errno));
        	return ERRORCODE;
    	}
   	printf("accpet ip:%s \n", inet_ntoa(accept_sockaddr.sin_addr));
    	return accept_st;
}
int run_server(int port)
{
    	int listen_st = create_listen(port);    //创建监听socket
    	pthread_t send_thrd, recv_thrd;
    	struct pthread_socket ps;
    	int accept_st;
		int ret = 0;
    	if (listen_st == -1)
    	{
        	return ERRORCODE;
    	}
    	printf("server start \n");
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
    	while (1)
    	{
        	accept_st = accept_socket(listen_st); //获取连接的的socket
        	if (accept_st == -1)
        	{
            		return ERRORCODE;
        	}
        	if (count_connect >= MAXCONN)
        	{
            		printf("connect have already be full! \n");
            		close(accept_st);
            		continue;
        	}
        	pthread_mutex_lock(&mutex);
        	count_connect++;
        	pthread_mutex_unlock(&mutex);
        	if (pthread_create(&send_thrd, NULL, thread_send, &accept_st) != 0) //创建发送信息线程
        	{
            	    printf("create thread error:%s \n", strerror(errno));
            	    break;
 
        	}
        	pthread_detach(send_thrd);        //设置线程可分离性，这样的话主线程就不用join
        	ps.socket_d = accept_st;
        	ps.thrd = send_thrd;
        	if (pthread_create(&recv_thrd, NULL, thread_recv, &ps) != 0)//创建接收信息线程
        	{
            		printf("create thread error:%s \n", strerror(errno));
            		break;
        	}
        	pthread_detach(recv_thrd); //设置线程为可分离，这样的话，就不用pthread_join
    	}
    close(accept_st);
    close(listen_st);
	sqlite3_close(db);
    return 0;
}
//server main
int main(int argc, char *argv[])
{
    	if (argc < 2)
    	{
        	printf("Usage:port,example:8080 \n");
        	return -1;
    	}
    	int port = atoi(argv[1]);
    	if (port == 0)
    	{
        	printf("port error! \n");
    	} 
	else
    	{
        	run_server(port);
    	}
    return 0;
}