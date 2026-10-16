-- A fighter whose claim was refused because their build is not the one the
-- other fighter claimed with. They opened the match, so they are present,
-- though they cannot play it on that build.
CREATE TABLE match_claim_refusals (
    match_id TEXT NOT NULL REFERENCES matches (id),
    ember_id TEXT NOT NULL,
    refused_at INTEGER NOT NULL,
    PRIMARY KEY (match_id, ember_id)
);
