-- One row per frame the driver laid out, in time order.
--
-- The frame zone is opened by the driver (`App::run` or `tests/dump.cpp`), so this is the
-- boundary every other query is measured against. A trace with one row is a cold page with no
-- steady state; the interesting traces have a handful of warm frames after the cold one.
SELECT row_number() OVER (ORDER BY ts) AS frame,
       ts,
       dur / 1e6                    AS ms
FROM slice
WHERE name = 'frame'
ORDER BY ts;
