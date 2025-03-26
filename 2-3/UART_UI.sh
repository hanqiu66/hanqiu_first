#!/bin/bash

# 全局变量
IP="127.0.0.1"
PORT=8080
NAME=""
AGE=0
EMAIL=""
CONDITION=""
TMPFILE=$(mktemp)

# 退出时清理临时文件
trap 'rm -f "$TMPFILE"' EXIT

# 验证函数
validate_age() {
    [[ "$AGE" =~ ^[0-9]+$ ]] || {
        dialog --msgbox "年龄必须是数字！" 10 30
        return 1
    }
}

validate_email() {
    [[ "$EMAIL" =~ .+@.+\..+ ]] || {
        dialog --msgbox "邮箱格式无效！" 10 30
        return 1
    }
}

validate_port() {
    [[ "$PORT" =~ ^[0-9]+$ ]] && (( PORT >= 1 && PORT <= 65535 )) || {
        dialog --msgbox "端口必须是1-65535之间的数字！" 10 30
        return 1
    }
}

# 功能函数
input_data() {
    dialog --clear --title "INPUT DATA" \
        --form "请输入数据:" 10 40 3 \
        "姓名:" 1 1 "$NAME" 1 10 30 0 \
        "年龄:" 2 1 "$AGE" 2 10 30 0 \
        "邮箱:" 3 1 "$EMAIL" 3 10 30 0 2>"$TMPFILE"

    if [ $? -eq 0 ]; then
        NAME=$(sed -n '1p' "$TMPFILE")
        AGE=$(sed -n '2p' "$TMPFILE")
        EMAIL=$(sed -n '3p' "$TMPFILE")

        if validate_age && validate_email; then
            if ! echo "insert $NAME,$AGE,$EMAIL" | nc -w 3 "$IP" "$PORT" > "$TMPFILE"; then
                dialog --msgbox "无法连接到服务器！" 10 30
            else
                dialog --msgbox "$(cat "$TMPFILE")" 10 30
            fi
        fi
    fi
}

query_data() {
    dialog --clear --title "QUERY DATA" \
        --inputbox "请输入查询条件:" 10 30 "age>20" 2>"$TMPFILE"

    if [ $? -eq 0 ]; then
        CONDITION=$(cat "$TMPFILE")
        if ! echo "select $CONDITION" | nc -w 3 "$IP" "$PORT" > "$TMPFILE"; then
            dialog --msgbox "无法连接到服务器！" 10 30
        else
            dialog --textbox "$TMPFILE" 20 60
        fi
    fi
}

set_server() {
    dialog --clear --title "SET SERVER" \
        --form "请输入服务器地址和端口:" 10 40 2 \
        "IP地址:" 1 1 "$IP" 1 10 30 0 \
        "端口:" 2 1 "$PORT" 2 10 30 0 2>"$TMPFILE"

    if [ $? -eq 0 ]; then
        IP=$(sed -n '1p' "$TMPFILE")
        PORT=$(sed -n '2p' "$TMPFILE")
        if ! validate_port; then
            PORT=8080
        fi
    fi
}

# 主循环
while true; do
    dialog --clear --title "UART MENU" \
        --menu "请选择操作:" 15 50 4 \
        1 "插入数据到数据库" \
        2 "从数据库查询数据" \
        3 "设置服务器地址和端口" \
        4 "退出" 2>"$TMPFILE"

    menuitem=$(cat "$TMPFILE")

    case $menuitem in
        1) input_data ;;
        2) query_data ;;
        3) set_server ;;
        4) exit 0 ;;
    esac
done