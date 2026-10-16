-- Games each slot has already won when a match starts, P1 first. A
-- platform that asks for a set again after its match was cut short sends
-- the score so far (BluMint's startingWins), and the match carries on from
-- it: the ledger's score is these plus its own accepted games. 0 for every
-- other match.
ALTER TABLE matches ADD COLUMN base_p1 INTEGER NOT NULL DEFAULT 0;
ALTER TABLE matches ADD COLUMN base_p2 INTEGER NOT NULL DEFAULT 0;

-- BluMint matches still open from before their lifetime was capped end as a
-- new one would: at most a day after their creation, whatever games moved
-- their deadline. 86400 is BLUMINT_MATCH_LIFETIME_SECS.
UPDATE matches
   SET expires_at = MIN(expires_at, created_at + 86400),
       expiry_secs = MIN(expiry_secs, 86400)
 WHERE expires_at IS NOT NULL
   AND state NOT IN ('completed', 'cancelled', 'failed', 'expired')
   AND connection_id IN (SELECT id FROM provider_connections WHERE kind = 'blumint');
