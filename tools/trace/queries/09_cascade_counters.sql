-- Cascade internals: nodes visited and share-cache effectiveness.
--
-- `cascade.nodes` is the window total, mirrored from FrameWork at publish.
-- `cascade.cache_*` / `has_calls` are still one point per css::Engine::cascade round.
-- High miss rates with many similar siblings (e.g. .para) usually mean allow_cache was
-- forced off (:has) or ShareKey is over-splitting (unique ids on every row).
SELECT name, value
FROM counter
JOIN counter_track ON counter.track_id = counter_track.id
WHERE name LIKE 'cascade.%'
ORDER BY ts DESC, name
LIMIT 40;
