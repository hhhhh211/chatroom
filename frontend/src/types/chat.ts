export interface ChatMessage {
    id: number;
    type: "system" | "chat";
    author?: string;
    text: string;
    time: string;
    mine?: boolean;
}
