-- The counters of the last frame as one row, for comparing two runs: run this against a
-- baseline trace and a candidate trace and diff the columns.
-- The last value of each counter track: the counters of the final frame, as one row. Run it
-- against a baseline trace and a candidate trace and diff the two result sets.
SELECT t.name,
       c.value
FROM counter c
JOIN counter_track t ON t.id = c.track_id
WHERE t.name LIKE 'frame.%'
  AND c.ts = (SELECT MAX(c2.ts) FROM counter c2 WHERE c2.track_id = c.track_id)
ORDER BY t.name;
