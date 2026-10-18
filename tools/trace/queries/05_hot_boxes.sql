-- The slowest boxes, named by tag / id / class, so a hotspot can be pointed at a DOM element
-- instead of a pointer. One row per zone, longest first.
SELECT s.name                                                      AS zone,
       MAX(CASE WHEN a.key = 'debug.kind'  THEN a.display_value END) AS kind,
       MAX(CASE WHEN a.key = 'debug.tag'   THEN a.display_value END) AS tag,
       MAX(CASE WHEN a.key = 'debug.id'    THEN a.display_value END) AS id,
       MAX(CASE WHEN a.key = 'debug.class' THEN a.display_value END) AS class,
       s.dur / 1e6                                                 AS ms
FROM slice s
JOIN args a ON a.arg_set_id = s.arg_set_id
WHERE s.dur IS NOT NULL
  AND a.key IN ('debug.kind', 'debug.tag', 'debug.id', 'debug.class')
GROUP BY s.id, s.name, s.dur
ORDER BY s.dur DESC
LIMIT 25;
