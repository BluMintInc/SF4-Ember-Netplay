-- Whether the fighter had pressed Ready at the match's table, as their
-- latest claim said (play::Claim::ready). A claim from a helper that does
-- not say reads not ready.
ALTER TABLE match_claims ADD COLUMN ready INTEGER NOT NULL DEFAULT 0;
