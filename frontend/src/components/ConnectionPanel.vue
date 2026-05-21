<script setup lang="ts">
import type { ChatSocketStatus } from "../api/chatSocket";

defineProps<{
    backendHost: string;
    nickname: string;
    roomId: string;
    status: ChatSocketStatus;
    messageCount: number;
    onlineCount: number;
    roomOnlineCount: number;
    canLeave: boolean;
}>();

defineEmits<{
    connect: [];
    leave: [];
    "update:backendHost": [value: string];
    "update:nickname": [value: string];
    "update:roomId": [value: string];
}>();

const statusText: Record<ChatSocketStatus, string> = {
    idle: "待机",
    connecting: "连接中",
    connected: "在线",
    disconnected: "已断开",
    error: "异常"
};
</script>

<template>
    <aside class="connection-panel" aria-labelledby="connection-title">
        <div class="panel-kicker">CHANNEL</div>
        <h2 id="connection-title">进入房间</h2>

        <form class="control-stack" @submit.prevent="$emit('connect')">
            <label class="field">
                <span>昵称</span>
                <input
                    :value="nickname"
                    autocomplete="nickname"
                    maxlength="24"
                    placeholder="例如 hhhhh"
                    @input="$emit('update:nickname', ($event.target as HTMLInputElement).value)"
                />
            </label>

            <label class="field">
                <span>房间号</span>
                <input
                    :value="roomId"
                    autocomplete="off"
                    maxlength="64"
                    placeholder="lobby"
                    @input="$emit('update:roomId', ($event.target as HTMLInputElement).value)"
                />
            </label>

            <label class="field">
                <span>后端地址</span>
                <input
                    :value="backendHost"
                    autocomplete="off"
                    placeholder="127.0.0.1:8080"
                    @input="$emit('update:backendHost', ($event.target as HTMLInputElement).value)"
                />
            </label>

            <div class="action-row">
                <button class="primary-action" type="submit" :disabled="status === 'connecting'">
                    {{ status === "connected" ? "重新连接" : "连接" }}
                </button>
                <button class="secondary-action" type="button" :disabled="!canLeave" @click="$emit('leave')">
                    离开
                </button>
            </div>
        </form>

        <dl class="room-readout">
            <div>
                <dt>状态</dt>
                <dd :class="['status-pill', `status-${status}`]">{{ statusText[status] }}</dd>
            </div>
            <div>
                <dt>当前房间</dt>
                <dd>{{ roomId.trim() || "lobby" }}</dd>
            </div>
            <div>
                <dt>房间在线</dt>
                <dd>{{ roomOnlineCount }}</dd>
            </div>
            <div>
                <dt>实例在线</dt>
                <dd>{{ onlineCount }}</dd>
            </div>
            <div>
                <dt>本页消息</dt>
                <dd>{{ messageCount }}</dd>
            </div>
        </dl>
    </aside>
</template>
