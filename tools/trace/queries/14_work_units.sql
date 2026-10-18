-- Work units of the last accounting window (docs/PERF.md section 2.2 / 6.1).
-- `work.*` is the FrameWork dump at publish.
-- `laid.boxes` is distinct boxes (I5); `frame.laid` is store events.
SELECT t.name,
       c.value
FROM counter c
JOIN counter_track t ON t.id = c.track_id
WHERE (t.name LIKE 'work.%' OR t.name IN ('laid.boxes', 'frame.laid', 'cascade.nodes'))
  AND c.ts = (SELECT MAX(c2.ts) FROM counter c2 WHERE c2.track_id = c.track_id)
ORDER BY t.name;
