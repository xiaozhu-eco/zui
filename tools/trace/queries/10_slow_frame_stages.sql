-- Slow frames, split by the zones that are their direct children.
--
-- 02_stage_breakdown.sql sums each stage over the whole session; this one keeps the frames apart,
-- so a session with a handful of multi-second frames shows which stage each one went to. Three
-- cascade-dominated frames and one that also carried a tree sync look very different in the two.
--
-- The threshold is milliseconds; raise it for a long session, lower it for a fast one.
WITH slow AS (
  SELECT id, ts, dur
  FROM slice
  WHERE name = 'frame' AND dur > 200e6
)
SELECT ROUND(slow.ts / 1e6, 1) AS frame_start_ms,
       ROUND(slow.dur / 1e6, 1) AS frame_ms,
       ROUND(c.dur / 1e6, 1)    AS stage_ms,
       c.name                   AS stage
FROM slow
JOIN slice c ON c.parent_id = slow.id
ORDER BY slow.ts, c.ts;
