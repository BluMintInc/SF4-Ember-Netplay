-- BluMint's status report on a match (delivery::status): whether its first
-- game has started and each player's presence, kept as it was last observed.
-- Each change to it moves `revision` and queues it to be sent again, so a
-- report that waited carries the latest status, and BluMint can drop one
-- that arrives after a later one. Once the match has ended it is
-- not_required: the result or abort says how it ended.
CREATE TABLE blumint_status (
    match_id TEXT PRIMARY KEY REFERENCES matches(id),
    revision INTEGER NOT NULL,
    body TEXT NOT NULL,
    observed_at INTEGER NOT NULL,
    state TEXT NOT NULL
        CHECK (state IN ('queued', 'delivering', 'retrying', 'delivered', 'failed', 'not_required')),
    next_at INTEGER NOT NULL DEFAULT 0,
    attempts INTEGER NOT NULL DEFAULT 0,
    first_at INTEGER
);
CREATE INDEX blumint_status_due ON blumint_status (state, next_at);

