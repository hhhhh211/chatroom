# C++ 高并发聊天室

## 当前项目架构

本项目按前后端分离方式组织，当前处于第 6 阶段：高并发优化。

```text
chatroom/
├── backend/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── config/
│   │   ├── message/
│   │   ├── server/
│   │   ├── websocket/
│   │   ├── room/
│   │   └── user/
│   └── include/
├── frontend/
│   ├── package.json
│   ├── index.html
│   ├── vite.config.ts
│   └── src/
│       ├── api/
│       ├── components/
│       ├── types/
│       ├── App.vue
│       ├── main.ts
│       └── styles.css
├── deploy/
├── docs/
│   └── 开发文档.md
└── README.md
```

## 如何启动项目

第 4 阶段使用 MySQL 保存消息历史。先创建数据库和用户：

```sql
CREATE DATABASE chatroom CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'chatroom'@'localhost' IDENTIFIED BY 'chatroom';
GRANT ALL PRIVILEGES ON chatroom.* TO 'chatroom'@'localhost';
FLUSH PRIVILEGES;
```

安装 MySQL 客户端开发包后重新配置 CMake。Ubuntu/Debian 常用包名：

```bash
sudo apt install default-libmysqlclient-dev libhiredis-dev redis-server
```

第 5 阶段使用 Redis Pub/Sub 支持多个后端实例之间共享实时消息。默认连接：

```text
redis://127.0.0.1:6379
```

启动后端 WebSocket 服务：

```bash
cmake -S backend -B backend/build
cmake --build backend/build
./backend/build/bin/chatroom_backend
```

可以通过环境变量指定 MySQL 连接信息：

```bash
CHATROOM_MYSQL_HOST=127.0.0.1 \
CHATROOM_MYSQL_PORT=3306 \
CHATROOM_MYSQL_USER=chatroom \
CHATROOM_MYSQL_PASSWORD=chatroom \
CHATROOM_MYSQL_DATABASE=chatroom \
./backend/build/bin/chatroom_backend
```

也可以通过环境变量指定 Redis 连接信息：

```bash
CHATROOM_REDIS_HOST=127.0.0.1 \
CHATROOM_REDIS_PORT=6379 \
CHATROOM_REDIS_CHANNEL=chatroom.messages \
./backend/build/bin/chatroom_backend
```

如果 Redis 设置了密码，再额外加：

```bash
CHATROOM_REDIS_PASSWORD=your_password
```

默认监听地址：

```text
ws://127.0.0.1:8080/?room_id=lobby
```

再启动前端 Vue 页面：

```bash
cd frontend
npm install
npm run dev
```

浏览器页面通常是：

```text
http://127.0.0.1:5173/
```

也可以指定端口：

```bash
./backend/build/bin/chatroom_backend 9000
```

后端使用 Boost.Asio + Boost.Beast 实现 HTTP 和 WebSocket 通信；正式聊天页面放在 `frontend/`，后端根路径只保留启动提示页。

如果 CMake 输出 `MySQL client headers/library not found`，说明当前机器缺少 MySQL 客户端开发头文件或库，后端仍可编译和聊天，但不会启用消息持久化。

如果 CMake 输出 `hiredis headers/library not found`，说明当前机器缺少 hiredis 开发头文件或库，后端仍可编译和单实例聊天，但不会启用 Redis 跨实例广播。

## 如何验证网页聊天

启动后端：

```bash
./backend/build/bin/chatroom_backend
```

启动前端：

```bash
cd frontend
npm run dev
```

然后打开两个浏览器标签页访问 `http://127.0.0.1:5173/`，输入昵称和相同房间号，任意一个页面发送消息，两个页面都会收到同一条消息。

验证历史消息：

1. 在某个房间发送几条消息。
2. 刷新页面或新开一个同房间标签页。
3. 连接成功后会先看到该房间最近的历史消息。

验证房间隔离：

```text
http://127.0.0.1:5173/?room_id=room-a
http://127.0.0.1:5173/?room_id=room-b
```

分别打开两个不同房间，房间 A 和房间 B 的消息不会互相收到。

验证多实例广播：

```bash
CHATROOM_REDIS_HOST=127.0.0.1 ./backend/build/bin/chatroom_backend 8080
CHATROOM_REDIS_HOST=127.0.0.1 ./backend/build/bin/chatroom_backend 8081
```

再打开两个前端页面，分别填写不同后端地址：

```text
127.0.0.1:8080
127.0.0.1:8081
```

两个页面使用相同房间号。任意一边发送消息，另一边也能收到，说明 Redis Pub/Sub 跨实例广播已经生效。

验证第 6 阶段高并发保护：

1. 打开同一房间的多个页面，左侧会显示当前后端实例在线人数和本房间在线人数。
2. 快速连续发送超过 8 条消息，后端会返回“发送太快了，请稍后再试。”提示。
3. 发送超过 1024 字节的消息会被拒绝，不会写入 MySQL，也不会广播给其他人。
4. 后端使用 WebSocket 心跳检测空闲连接，并给每个连接维护独立发送队列，慢连接不会阻塞整个房间广播。

## 当前已实现功能

- 已创建项目基础目录。
- 已创建后端 CMake 工程。
- 已实现最小 WebSocket 服务。
- 支持多个浏览器客户端连接。
- 支持文本消息广播给同房间在线连接。
- 支持通过 `room_id` 创建和进入不同房间。
- 已创建 Vue 3 + Vite + TypeScript 前端页面。
- 前端支持输入昵称、房间号和后端地址。
- 前端支持消息列表、发送消息和断开提示。
- 后端支持 MySQL 自动建表：`rooms`、`messages`。
- 后端发送消息时写入 MySQL。
- 新连接进入房间时会加载最近历史消息。
- 后端支持 Redis Pub/Sub 多实例消息广播。
- 每个后端实例只负责转发自己本机的 WebSocket 连接。
- 后端支持 WebSocket 心跳检测。
- 后端支持每个连接独立发送队列，广播时只入队，不直接阻塞写 socket。
- 后端限制单条聊天消息最大 1024 字节。
- 后端限制单连接 10 秒内最多发送 8 条消息。
- 前端显示当前后端实例在线人数和本房间在线人数。
- 已创建开发文档。

## 当前进度

第 6 阶段：高并发优化。

说明：第 6 阶段已经加入心跳、独立发送队列、消息长度限制、发送频率限制和本实例在线统计；在线人数统计的是当前连接的后端实例，不是 Redis 多实例全局总人数。当前 CMake 会自动探测 MySQL 客户端开发库和 hiredis，探测成功后启用真实持久化和跨实例广播。
