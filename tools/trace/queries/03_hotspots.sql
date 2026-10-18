-- The hotspot ranking: self time by zone name.
--
-- Self time is a zone's duration minus the duration of its direct children, so nesting does not
-- double count and the total of this column is the time actually spent in the leaf work. This is
-- the query to run first when you do not know where the time is.
WITH self_time AS (
  SELECT s.id,
         s.name,
         s.cat,
         s.dur - COALESCE((
           SELECT SUM(c.dur)
           FROM slice c
           WHERE c.parent_id = s.id
             AND c.dur IS NOT NULL
             AND c.ts >= s.ts
             AND c.ts + c.dur <= s.ts + s.dur
         ), 0) AS self_dur
  FROM slice s
  WHERE s.dur IS NOT NULL
    AND s.name NOT IN ('process_name', 'thread_name', 'zui_session')
)
SELECT name,
       cat,
       COUNT(*)                AS zones,
       SUM(self_dur) / 1e6     AS self_ms,
       AVG(self_dur) / 1e6     AS avg_self_ms,
       MAX(self_dur) / 1e6     AS max_self_ms
FROM self_time
GROUP BY name, cat
ORDER BY self_ms DESC
LIMIT 25;
