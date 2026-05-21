<script setup lang="ts">
import { nextTick, ref, watch } from "vue";
import type { ChatMessage } from "../types/chat";

const props = defineProps<{
    messages: ChatMessage[];
}>();

const listRef = ref<HTMLElement | null>(null);

watch(
    () => props.messages.length,
    async () => {
        await nextTick();
        if (listRef.value) {
            listRef.value.scrollTop = listRef.value.scrollHeight;
        }
    }
);
</script>

<template>
    <section class="message-board" aria-label="聊天消息">
        <div ref="listRef" class="message-list" aria-live="polite">
            <article
                v-for="message in messages"
                :key="message.id"
                :class="['message-item', `message-${message.type}`, { mine: message.mine }]"
            >
                <div class="message-meta">
                    <span>{{ message.type === "system" ? "SYSTEM" : message.author || "匿名" }}</span>
                    <time>{{ message.time }}</time>
                </div>
                <p>{{ message.text }}</p>
            </article>
        </div>
    </section>
</template>
