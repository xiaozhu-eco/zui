-- Where a frame's time went, by the zones that are direct children of the frame: the cascade,
-- the tree sync, the layout engine, paint and present.
--
-- `layout` here is the whole `LayoutEngine::layout` call. Drill into it with 03 and 04.
SELECT s.name                       AS stage,
       COUNT(*)                     AS frames,
       SUM(s.dur) / 1e6             AS total_ms,
       AVG(s.dur) / 1e6             AS avg_ms,
       MAX(s.dur) / 1e6             AS max_ms
FROM slice s
LEFT JOIN slice p ON s.parent_id = p.id
WHERE p.name = 'frame'
GROUP BY s.name
ORDER BY total_ms DESC;
