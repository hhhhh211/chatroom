<script setup lang="ts">
defineProps<{
    modelValue: string;
    disabled: boolean;
}>();

defineEmits<{
    submit: [];
    "update:modelValue": [value: string];
}>();
</script>

<template>
    <form class="composer" @submit.prevent="$emit('submit')">
        <label class="sr-only" for="chat-message">消息内容</label>
        <input
            id="chat-message"
            :value="modelValue"
            :disabled="disabled"
            autocomplete="off"
            maxlength="1024"
            placeholder="输入消息"
            @input="$emit('update:modelValue', ($event.target as HTMLInputElement).value)"
        />
        <button type="submit" :disabled="disabled || !modelValue.trim()">发送</button>
    </form>
</template>
