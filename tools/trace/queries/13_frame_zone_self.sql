-- Self time of every zone inside a frame, ranked, for the frames over a threshold.
--
-- `03_hotspots.sql` ranks by self time over the whole trace, which is the right first question but
-- mixes every frame together: a startup frame's 500ms of chapter parsing buries the hitch the
-- measurement is about. This query keeps the frame boundary, so a single interaction frame can be
-- read on its own - `12_tour_frame_stages.sql` gives the stage totals, this gives what is inside
-- them (and what the stages do not cover).
--
-- Self time is the zone's duration minus its direct children's, so nesting does not double count
-- and the rows of one frame sum to roughly that frame's duration. Adjust the threshold, or the
-- ordering, to point it at the frame under study.
WITH slow AS (
  SELECT id, ts, dur
  FROM slice
  WHERE name = 'frame' AND dur > 100e6
),
sub AS (
  SELECT slow.id AS frame_id, slow.ts AS frame_ts, slow.dur AS frame_dur,
         s.id AS sid, s.name AS name, s.category AS cat, s.ts AS ts, s.dur AS dur
  FROM slow
  JOIN descendant_slice(slow.id) s
)
SELECT ROUND(frame_ts / 1e6, 1) AS frame_start_ms,
       ROUND(frame_dur / 1e6, 1) AS frame_ms,
       name,
       cat,
       COUNT(*)  AS zones,
       ROUND(SUM(dur - COALESCE((
           SELECT SUM(c.dur) FROM slice c
           WHERE c.parent_id = sid AND c.ts >= ts AND c.ts + c.dur <= ts + dur), 0)) / 1e6, 2) AS self_ms
FROM sub
GROUP BY frame_id, frame_ts, frame_dur, name, cat
ORDER BY frame_ts, self_ms DESC;
