-- Whether the room's fighters each play at their own input delay (1), instead
-- of both at the higher of the two (0). Set from the creator's request when the
-- room is reserved, and from the room host's own report once it gives one
-- (`details.own_delay`; a report without it is a host that plays the shared
-- delay). Rooms from before the column are the shared delay.
ALTER TABLE rooms ADD COLUMN own_delay INTEGER NOT NULL DEFAULT 0;
