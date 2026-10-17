-- The `Idempotency-Key` BluMint created each of its matches under
-- (routes::blumint::keys) is the match's external_match_id, as
-- 'blumint:<key>'. BluMint closes a match by that key for as long as the
-- match exists, while the answer stored under it is removed after a day. A
-- match made before this takes the key of the answer that names it, where
-- that answer is still kept and no close has replaced it.
UPDATE matches SET external_match_id = 'blumint:' || r.idempotency_key
  FROM idempotency_records r
 WHERE r.method = 'POST' AND r.path = '/v1/blumint/matches'
   AND r.actor = 'connection:' || matches.connection_id
   AND json_extract(CAST(r.response AS TEXT), '$.matchId') = matches.id;

-- A key whose creation the bridge refused, which stores no answer. Kept for
-- the day an answer is, so a close under it closes a key BluMint sent.
CREATE TABLE blumint_refused_keys (
    connection_id TEXT NOT NULL,
    idempotency_key TEXT NOT NULL,
    refused_at INTEGER NOT NULL,
    PRIMARY KEY (connection_id, idempotency_key)
);
