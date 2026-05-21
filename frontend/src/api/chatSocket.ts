export type ChatSocketStatus = "idle" | "connecting" | "connected" | "disconnected" | "error";

export interface ChatSocketOptions {
    backendHost: string;
    roomId: string;
    onOpen: () => void;
    onMessage: (message: string) => void;
    onClose: () => void;
    onError: () => void;
}

export function createChatSocket(options: ChatSocketOptions): WebSocket {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const room = encodeURIComponent(options.roomId.trim() || "lobby");
    const socket = new WebSocket(`${protocol}//${options.backendHost}/?room_id=${room}`);

    socket.addEventListener("open", options.onOpen);
    socket.addEventListener("message", (event) => {
        options.onMessage(String(event.data));
    });
    socket.addEventListener("close", options.onClose);
    socket.addEventListener("error", options.onError);

    return socket;
}
