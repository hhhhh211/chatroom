<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from "vue";
import { createChatSocket, type ChatSocketStatus } from "./api/chatSocket";
import ConnectionPanel from "./components/ConnectionPanel.vue";
import MessageComposer from "./components/MessageComposer.vue";
import MessageList from "./components/MessageList.vue";
import type { ChatMessage } from "./types/chat";

const urlParams = new URLSearchParams(window.location.search);
const backendHost = ref(urlParams.get("backend") || "127.0.0.1:8080");
const nickname = ref(window.localStorage.getItem("chatroom.nickname") || "");
const roomId = ref(urlParams.get("room_id") || "lobby");
const draft = ref("");
const messages = ref<ChatMessage[]>([]);
const status = ref<ChatSocketStatus>("idle");
const socket = ref<WebSocket | null>(null);
const onlineCount = ref(0);
const roomOnlineCount = ref(0);
let messageId = 0;
let connectionId = 0;
const controlPrefix = "__chatroom_control__";

const connected = computed(() => status.value === "connected" && socket.value?.readyState === WebSocket.OPEN);
const canLeave = computed(() => status.value === "connected" || status.value === "connecting");
const statusLine = computed(() => {
    if (status.value === "connected") {
        return `正在 ${roomId.value.trim() || "lobby"} 房间`;
    }
    if (status.value === "connecting") {
        return "正在建立 WebSocket 连接";
    }
    if (status.value === "error") {
        return "连接异常，请确认后端服务已启动";
    }
    if (status.value === "disconnected") {
        return "WebSocket 已断开";
    }
    return "填写昵称和房间号后连接";
});

function nowTime() {
    return new Intl.DateTimeFormat("zh-CN", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit"
    }).format(new Date());
}

function addMessage(message: Omit<ChatMessage, "id" | "time">) {
    messages.value.push({
        id: ++messageId,
        time: nowTime(),
        ...message
    });
}

function handleControlMessage(message: string) {
    if (!message.startsWith(controlPrefix)) {
        return false;
    }

    try {
        const payload = JSON.parse(message.slice(controlPrefix.length)) as {
            type?: string;
            text?: string;
            onlineCount?: number;
            roomOnlineCount?: number;
        };

        if (payload.type === "stats") {
            onlineCount.value = Number(payload.onlineCount || 0);
            roomOnlineCount.value = Number(payload.roomOnlineCount || 0);
            return true;
        }

        if (payload.type === "notice" && payload.text) {
            addMessage({ type: "system", text: payload.text });
            return true;
        }
    } catch {
        addMessage({ type: "system", text: "收到无法解析的服务端控制消息" });
    }

    return true;
}

function normalizedNickname() {
    const value = nickname.value.trim();
    return value || "匿名";
}

function normalizedRoomId() {
    const value = roomId.value.trim();
    return value || "lobby";
}

function closeSocket() {
    if (socket.value) {
        socket.value.close();
        socket.value = null;
    }
}

function connect() {
    closeSocket();

    const nextRoomId = normalizedRoomId();
    const nextNickname = normalizedNickname();
    roomId.value = nextRoomId;
    nickname.value = nextNickname;
    window.localStorage.setItem("chatroom.nickname", nextNickname);

    const nextUrl = new URL(window.location.href);
    nextUrl.searchParams.set("room_id", nextRoomId);
    nextUrl.searchParams.set("backend", backendHost.value.trim() || "127.0.0.1:8080");
    window.history.replaceState({}, "", nextUrl);

    messages.value = [];
    onlineCount.value = 0;
    roomOnlineCount.value = 0;
    status.value = "connecting";
    addMessage({ type: "system", text: `正在进入房间：${nextRoomId}` });

    const currentConnectionId = ++connectionId;
    socket.value = createChatSocket({
        backendHost: backendHost.value.trim() || "127.0.0.1:8080",
        roomId: nextRoomId,
        onOpen: () => {
            if (currentConnectionId !== connectionId) {
                return;
            }
            status.value = "connected";
            addMessage({ type: "system", text: `${nextNickname} 已连接` });
        },
        onMessage: (message) => {
            if (currentConnectionId !== connectionId) {
                return;
            }
            if (handleControlMessage(message)) {
                return;
            }

            const mine = message.startsWith(`${nextNickname}: `);
            const divider = message.indexOf(": ");
            addMessage({
                type: "chat",
                author: divider > 0 ? message.slice(0, divider) : "成员",
                text: divider > 0 ? message.slice(divider + 2) : message,
                mine
            });
        },
        onClose: () => {
            if (currentConnectionId !== connectionId) {
                return;
            }
            status.value = "disconnected";
            socket.value = null;
            addMessage({ type: "system", text: "连接已断开" });
        },
        onError: () => {
            if (currentConnectionId !== connectionId) {
                return;
            }
            status.value = "error";
            addMessage({ type: "system", text: "连接异常，请检查后端服务和端口" });
        }
    });
}

function leave() {
    ++connectionId;
    closeSocket();
    status.value = "disconnected";
    onlineCount.value = 0;
    roomOnlineCount.value = 0;
    addMessage({ type: "system", text: "你已离开房间" });
}

function sendMessage() {
    const text = draft.value.trim();
    if (!text || !connected.value || !socket.value) {
        return;
    }

    socket.value.send(`${normalizedNickname()}: ${text}`);
    draft.value = "";
}

onBeforeUnmount(() => {
    closeSocket();
});

onMounted(() => {
    connect();
});
</script>

<template>
    <div class="app-shell">
        <a class="skip-link" href="#chat-main">跳到聊天区</a>

        <header class="topbar">
            <div>
                <p class="eyebrow">C++ / Boost.Beast / WebSocket</p>
                <h1>C++ 高并发聊天室</h1>
            </div>
            <div :class="['signal', `signal-${status}`]" role="status" aria-live="polite">
                <span aria-hidden="true"></span>
                {{ statusLine }}
            </div>
        </header>

        <main id="chat-main" class="workspace">
            <ConnectionPanel
                v-model:backend-host="backendHost"
                v-model:nickname="nickname"
                v-model:room-id="roomId"
                :status="status"
                :message-count="messages.length"
                :online-count="onlineCount"
                :room-online-count="roomOnlineCount"
                :can-leave="canLeave"
                @connect="connect"
                @leave="leave"
            />

            <section class="chat-panel" aria-labelledby="room-title">
                <div class="chat-header">
                    <div>
                        <p class="panel-kicker">ROOM</p>
                        <h2 id="room-title">{{ normalizedRoomId() }}</h2>
                    </div>
                    <span class="room-chip">{{ normalizedNickname() }}</span>
                </div>

                <MessageList :messages="messages" />
                <MessageComposer v-model="draft" :disabled="!connected" @submit="sendMessage" />
            </section>
        </main>
    </div>
</template>
