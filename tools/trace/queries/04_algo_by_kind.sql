-- Which formatter the time went to. `kind` is the algorithm the box was routed to, so this
-- separates normal flow from flex, grid, table and multicol.
SELECT a.display_value            AS kind,
       COUNT(*)                   AS zones,
       SUM(s.dur) / 1e6           AS total_ms,
       AVG(s.dur) / 1e6           AS avg_ms
FROM slice s
JOIN args a ON a.arg_set_id = s.arg_set_id
WHERE s.cat = 'layout.algo'
  -- Track event debug annotations read back with a `debug.` prefix, so the key the writer emitted
  -- as `kind` is `debug.kind` here. `SELECT * FROM args LIMIT 0` is the check that keeps this
  -- honest.
  AND a.key = 'debug.kind'
GROUP BY kind
ORDER BY total_ms DESC;
