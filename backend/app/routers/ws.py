from __future__ import annotations

import json
import logging
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Depends
from sqlalchemy.orm import Session
from sqlalchemy import select

from ..db import SessionLocal, get_db
from ..models import User, GroupMember
from ..security import decode_token

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/ws", tags=["ws"])


class WSManager:
    def __init__(self):
        # user_id -> set of WebSockets
        self.connections: dict[int, set[WebSocket]] = {}

    async def connect(self, user_id: int, ws: WebSocket):
        if user_id not in self.connections:
            self.connections[user_id] = set()
        self.connections[user_id].add(ws)

    def disconnect(self, user_id: int, ws: WebSocket):
        if user_id in self.connections:
            self.connections[user_id].discard(ws)
            if not self.connections[user_id]:
                del self.connections[user_id]

    async def broadcast_to_group(self, group_id: int, message: str, db: Session):
        stmt = select(GroupMember.user_id).where(GroupMember.group_id == group_id)
        user_ids = [row[0] for row in db.execute(stmt).all()]
        for uid in user_ids:
            if uid in self.connections:
                for ws in list(self.connections[uid]):
                    try:
                        await ws.send_text(message)
                    except Exception as e:
                        logger.error(f"Exception group broadcast: {e}")
                        self.disconnect(uid, ws)


manager = WSManager()


@router.websocket("")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        first_message = await websocket.receive_text()
        payload = json.loads(first_message)
        if payload.get("type") != "auth":
            raise ValueError("First message must be of type 'auth'")
        token = payload["token"]
    except (json.JSONDecodeError, KeyError, ValueError):
        await websocket.close(code=4400, reason="Broken auth message.")
        return

    user_id = decode_token(token, "access")
    if not user_id:
        await websocket.close(code=1008)
        return

    db = SessionLocal()
    user = db.get(User, user_id)
    if not user:
        await websocket.close(code=1008)
        return

    await manager.connect(user_id, websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(user_id, websocket)
