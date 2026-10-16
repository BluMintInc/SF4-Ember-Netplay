-- When the bridge last swept for lapsed claims, and the last span it heard
-- no claims in: from the sweep before it stopped to when it started again
-- (routes::blumint::liveness). A claim live when it stopped is not counted
-- lapsed for that span.
CREATE TABLE bridge_liveness (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    swept_at INTEGER,
    down_since INTEGER NOT NULL DEFAULT 0,
    resumed_at INTEGER NOT NULL DEFAULT 0
);
INSERT INTO bridge_liveness (id) VALUES (1);

-- The open matches, which the sweep reads every pass, whatever the number
-- of matches that have ended.
CREATE INDEX matches_open ON matches (id)
    WHERE state NOT IN ('completed', 'cancelled', 'failed', 'expired');
