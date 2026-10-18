-- The reader tour's frames, split by the zones that are their direct children.
--
-- `10_slow_frame_stages.sql` keeps only frames over 200ms, which is the right sieve for a trace
-- whose problem is multi-second frames. Release reader work is not that: the four interactions the
-- layout plan measures are one hitch each, so the threshold here is 20ms and every hitch the tour
-- produces shows up with its breakdown. Trace it with `tools/trace/repro_reader_tour.ps1`.
WITH slow AS (
  SELECT id, ts, dur
  FROM slice
  WHERE name = 'frame' AND dur > 20e6
)
SELECT ROUND(slow.ts / 1e6, 1) AS frame_start_ms,
       ROUND(slow.dur / 1e6, 1) AS frame_ms,
       ROUND(c.dur / 1e6, 1)    AS stage_ms,
       c.name                   AS stage
FROM slow
JOIN slice c ON c.parent_id = slow.id
ORDER BY slow.ts, c.ts;
