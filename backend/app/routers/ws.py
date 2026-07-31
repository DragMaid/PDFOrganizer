"""Live change notifications, pushed to every member of a group.

A client cannot poll for everything it needs to stay current — a note someone
else writes, a PDF they add, a tag they rename — so the server pushes a small
event whenever shared state moves.

Events carry no content. They name *what* changed and *where*, and the client
re-fetches the part it happens to be showing::

    {"type": "note_created", "group_id": 4, "file_id": 12, "actor_id": 3}

That keeps the socket cheap, and it means a client that was disconnected while
things happened recovers by reloading rather than by replaying a log it missed.

``actor_id`` is whoever caused the change. The event still goes to that user's
own sessions on purpose: a second machine signed into the same account has just
as much to learn from it as anybody else's.
"""

from __future__ import annotations

import asyncio
import json
import logging

from fastapi import APIRouter, BackgroundTasks, WebSocket, WebSocketDisconnect
from sqlalchemy import select

from ..db import SessionLocal
from ..models import GroupMember, User
from ..security import decode_token

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/ws", tags=["ws"])


class WSManager:
    def __init__(self) -> None:
        # user_id -> every socket that user currently has open
        self.connections: dict[int, set[WebSocket]] = {}

    async def connect(self, user_id: int, ws: WebSocket) -> None:
        self.connections.setdefault(user_id, set()).add(ws)

    def disconnect(self, user_id: int, ws: WebSocket) -> None:
        sockets = self.connections.get(user_id)
        if sockets is None:
            return
        sockets.discard(ws)
        if not sockets:
            del self.connections[user_id]

    @staticmethod
    def _member_ids(group_id: int) -> list[int]:
        # A session of our own, deliberately: broadcasting happens in a
        # background task, by which time the request that scheduled it has
        # already closed the session it was holding.
        db = SessionLocal()
        try:
            rows = db.execute(
                select(GroupMember.user_id).where(GroupMember.group_id == group_id)
            ).all()
            return [row[0] for row in rows]
        finally:
            db.close()

    async def broadcast_to_group(
        self, group_id: int, event: dict[str, object]
    ) -> None:
        payload = json.dumps(event)
        # Reading the roster hits the database, which blocks; off the event loop
        # it goes, so one broadcast never stalls every other connection.
        member_ids = await asyncio.to_thread(self._member_ids, group_id)

        for user_id in member_ids:
            for ws in list(self.connections.get(user_id, ())):
                try:
                    await ws.send_text(payload)
                except Exception:  # noqa: BLE001 - one dead socket is not fatal
                    logger.warning(
                        "Dropping a socket that would not take %s", event.get("type")
                    )
                    self.disconnect(user_id, ws)


manager = WSManager()


def notify(
    background_tasks: BackgroundTasks,
    group_id: int,
    event_type: str,
    **fields: object,
) -> None:
    """Queue one event for a group, to be sent once the response is on its way.

    Every endpoint that changes shared state calls this, so the single place to
    look for "what does the client hear about?" is the set of call sites.
    """
    background_tasks.add_task(
        manager.broadcast_to_group,
        group_id,
        {"type": event_type, "group_id": group_id, **fields},
    )


@router.websocket("")
async def websocket_endpoint(websocket: WebSocket) -> None:
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
    except WebSocketDisconnect:
        return

    user_id = decode_token(token, "access")
    if not user_id:
        await websocket.close(code=1008)
        return

    db = SessionLocal()
    try:
        user = db.get(User, user_id)
    finally:
        db.close()
    if not user:
        await websocket.close(code=1008)
        return

    await manager.connect(user_id, websocket)
    try:
        # Nothing a client sends after authenticating means anything yet; the
        # read is what keeps the connection alive and notices it closing.
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        manager.disconnect(user_id, websocket)
