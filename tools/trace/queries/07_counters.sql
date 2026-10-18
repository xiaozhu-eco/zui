-- The frame counters as a time series. Phase times and cache quality still come from
-- `emit_frame_counters`; `frame.laid` is the store-count alias mirrored from FrameWork
-- at publish (`laid.boxes` is the distinct-box count — see 14_work_units.sql).
-- A counter's name lives on its track, not on the value row.
SELECT c.ts,
       t.name,
       c.value
FROM counter c
JOIN counter_track t ON t.id = c.track_id
WHERE t.name LIKE 'frame.%'
ORDER BY c.ts, t.name;
