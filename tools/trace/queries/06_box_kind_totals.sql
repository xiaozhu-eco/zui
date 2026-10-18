-- Same grouping as 04, but by the element the zone belongs to: which tag and class the layout
-- time follows. A hotspot that tracks one class is a page-level finding, not an engine one.
SELECT MAX(CASE WHEN a.key = 'debug.tag'   THEN a.display_value END) AS tag,
       MAX(CASE WHEN a.key = 'debug.class' THEN a.display_value END) AS class,
       COUNT(*)                                                    AS zones,
       SUM(s.dur) / 1e6                                            AS total_ms
FROM slice s
JOIN args a ON a.arg_set_id = s.arg_set_id
WHERE s.dur IS NOT NULL
  AND a.key IN ('debug.tag', 'debug.class')
GROUP BY s.id
ORDER BY total_ms DESC
LIMIT 25;
