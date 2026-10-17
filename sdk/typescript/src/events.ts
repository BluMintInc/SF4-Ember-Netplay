// Bridge events: CloudEvents 1.0 with an `emberseq` cursor (spec 20.2).
import { parseStrict, type Json } from "./canonical.ts";
import { counter, isBridgeOrigin, prefixedId } from "./signed.ts";

export const EVENT_TYPES = [
  "identity.link.pending",
  "identity.link.completed",
  "identity.link.removed",
  "match.created",
  "match.assignment.changed",
  "match.room.ready",
  "match.player.present",
  "match.player.ready",
  "match.started",
  "match.attempt.authorized",
  "match.game.reported",
  "match.game.confirmed",
  "match.score.changed",
  "match.needs_review",
  "match.completed",
  "match.cancelled",
  "match.failed",
  "match.expired",
  "match.corrected",
  "provider.delivery.succeeded",
  "provider.delivery.failed",
  "provider.delivery.ambiguous",
  "lobby.created",
  "lobby.queue.changed",
  "lobby.set.started",
  "lobby.set.completed",
  "lobby.closed",
  "tournament.created",
  "tournament.entrants.changed",
  "tournament.started",
  "tournament.match.started",
  "tournament.match.completed",
  "tournament.match.reopened",
  "tournament.completed",
  "tournament.cancelled",
  "room.created",
  "room.opened",
  "room.changed",
  "room.closed",
] as const;

export type EventName = (typeof EVENT_TYPES)[number];

/** `io.ember.tournament.<name>.v1`. */
export function eventType<Name extends EventName>(name: Name): `io.ember.tournament.${Name}.v1` {
  return `io.ember.tournament.${name}.v1`;
}

export interface BridgeEvent {
  specversion: "1.0";
  id: string;
  source: string;
  type: string;
  subject: string;
  time: string;
  datacontenttype: "application/json";
  dataschema: string;
  emberseq: string;
  data: { [key: string]: Json };
}

export class EventError extends Error {}

const FIELDS = ["specversion", "id", "source", "type", "subject", "time", "datacontenttype", "dataschema", "emberseq", "data"];

/**
 * Parses a verified webhook body or one event from the replay API. Unknown
 * event types parse too, so a receiver can skip ones it does not handle.
 */
export function parseEvent(payload: string | Uint8Array | Json): BridgeEvent {
  const value =
    typeof payload === "string" ? parseStrict(payload) : payload instanceof Uint8Array ? parseStrict(Buffer.from(payload).toString("utf8")) : payload;
  if (value === null || typeof value !== "object" || Array.isArray(value)) throw new EventError("an event is an object");
  const keys = Object.keys(value);
  if (keys.length !== FIELDS.length || !FIELDS.every((field) => keys.includes(field))) throw new EventError("unexpected event fields");
  const { specversion, id, source, type, subject, time, datacontenttype, dataschema, emberseq, data } = value;
  const sequence = counter(emberseq);
  if (specversion !== "1.0" || typeof id !== "string" || !prefixedId(id, "evt") ||
      typeof source !== "string" || !isBridgeOrigin(source) ||
      typeof type !== "string" || type.length > 160 || !/^io\.ember\.[a-z_]+(\.[a-z_]+)+\.v1$/.test(type) ||
      typeof subject !== "string" || subject.length === 0 || Buffer.byteLength(subject, "utf8") > 256 ||
      typeof time !== "string" || !/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$/.test(time) ||
      datacontenttype !== "application/json" || typeof dataschema !== "string" ||
      Buffer.byteLength(dataschema, "utf8") > 512 || !/^(https:\/\/|urn:)/.test(dataschema) ||
      typeof emberseq !== "string" || sequence === null || sequence === 0n ||
      data === null || typeof data !== "object" || Array.isArray(data)) throw new EventError("invalid event envelope");
  return { specversion, id, source, type, subject, time, datacontenttype, dataschema, emberseq, data };
}

/** One open room, as the room routes list it. */
export interface RoomSummary {
  room_id: string;
  name: string;
  build_id: string;
  members: number;
  capacity: number;
  tables_playing: number;
  /** The host's relay region code, such as `use1`. */
  region: string;
  created_at: number;
}

/** `waiting` until the creator is in, then `open`, then `closed`. */
export type RoomState = "waiting" | "open" | "closed";

/** A room as the connection that created it sees it. */
export interface ConnectionRoom {
  room: RoomSummary;
  state: RoomState;
  creator_ember_id: string;
  /** The page that opens the room in Ember, or offers to install Ember first. */
  join_url: string;
}

/** Why a room closed: your `closeRoom`, or it ended on its own (it emptied, its creator never came, or its host stopped). */
export type RoomCloseReason = "closed_by_connection" | "ended";

const ROOM_STATES: readonly string[] = ["waiting", "open", "closed"];
const ROOM_CLOSE_REASONS: readonly string[] = ["closed_by_connection", "ended"];

/** `room.created`, `room.opened` and `room.changed` carry the room as it stands; `room.closed` adds why. */
export type RoomEvent =
  | (BridgeEvent & { type: `io.ember.tournament.room.${"created" | "opened" | "changed"}.v1`; data: ConnectionRoom })
  | (BridgeEvent & { type: "io.ember.tournament.room.closed.v1"; data: ConnectionRoom & { reason: RoomCloseReason } });

function isRoomSummary(value: unknown): value is RoomSummary {
  const room = value as { [key: string]: unknown } | null;
  return (
    typeof room === "object" &&
    room !== null &&
    typeof room.room_id === "string" &&
    typeof room.name === "string" &&
    typeof room.build_id === "string" &&
    typeof room.members === "number" &&
    typeof room.capacity === "number" &&
    typeof room.tables_playing === "number" &&
    typeof room.region === "string" &&
    typeof room.created_at === "number"
  );
}

/**
 * Whether `event` is one of the room events (for rooms your connection
 * created) with the data its type promises, so its `data` can be read as a
 * `ConnectionRoom`. Other events, and room events with unexpected data, are
 * not.
 */
export function isRoomEvent(event: BridgeEvent): event is RoomEvent {
  const kind = event.type;
  const room = event.data;
  const known =
    kind === eventType("room.created") || kind === eventType("room.opened") || kind === eventType("room.changed") || kind === eventType("room.closed");
  return (
    known &&
    isRoomSummary(room.room) &&
    typeof room.state === "string" &&
    ROOM_STATES.includes(room.state) &&
    typeof room.creator_ember_id === "string" &&
    typeof room.join_url === "string" &&
    (kind !== eventType("room.closed") || (typeof room.reason === "string" && ROOM_CLOSE_REASONS.includes(room.reason)))
  );
}
