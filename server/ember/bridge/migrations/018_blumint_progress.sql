-- What the bridge tells BluMint about a set while it is played
-- (delivery::progress): a progress report after each game that raised the
-- score without ending the set, keyed by the games won so far, and one
-- status report, games_played 0, when the set's first game starts. Each is
-- sent at least once, as a result is, and says what the ledger holds when it
-- is sent. A report whose match has ended is not_required: the result or
-- abort carries the final score.
CREATE TABLE blumint_progress (
    match_id TEXT NOT NULL REFERENCES matches (id),
    games_played INTEGER NOT NULL,
    state TEXT NOT NULL
        CHECK (state IN ('queued', 'delivering', 'retrying', 'delivered', 'failed', 'not_required')),
    next_at INTEGER NOT NULL DEFAULT 0,
    attempts INTEGER NOT NULL DEFAULT 0,
    first_at INTEGER,
    PRIMARY KEY (match_id, games_played)
);
CREATE INDEX blumint_progress_due ON blumint_progress (state, next_at);
