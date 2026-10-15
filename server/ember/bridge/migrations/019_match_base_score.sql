-- Games each slot has already won when a match starts, P1 first. A
-- platform that asks for a set again after its match was cut short sends
-- the score so far (BluMint's startingWins), and the match carries on from
-- it: the ledger's score is these plus its own accepted games. 0 for every
-- other match.
ALTER TABLE matches ADD COLUMN base_p1 INTEGER NOT NULL DEFAULT 0;
ALTER TABLE matches ADD COLUMN base_p2 INTEGER NOT NULL DEFAULT 0;
