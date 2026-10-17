-- When the bridge last swept for lapsed claims (routes::blumint::liveness),
-- so each pass reads only the claims that lapsed since the one before, and a
-- bridge that starts again knows when it last ran.
CREATE TABLE bridge_liveness (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    swept_at INTEGER
);
INSERT INTO bridge_liveness (id) VALUES (1);

-- The open matches, which the sweep reads every pass, whatever the number
-- of matches that have ended.
CREATE INDEX matches_open ON matches (id)
    WHERE state NOT IN ('completed', 'cancelled', 'failed', 'expired');
