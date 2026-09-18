const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8081 });

console.log('信令服务器启动在 ws://localhost:8081');

wss.on('connection', (ws) => {
    console.log('新客户端连接');
    ws.on('message', (message) => {
        // 转发消息给所有其他客户端
        wss.clients.forEach((client) => {
            if (client !== ws && client.readyState === WebSocket.OPEN) {
                client.send(message);
            }
        });
    });
    ws.on('close', () => console.log('客户端断开'));
});