-- Where `relower` / `sync_children` sit in the tree: their parent chain, biggest first.
--
-- Both hang off the app's `tree_sync`, which hangs off `frame` (docs/TRACING.md section 3). Used
-- to tell "this frame's seconds are the box-tree rebuild" from "this frame's seconds are the
-- cascade, and the rebuild is a smaller part of it" - the zones are named, but only the parent
-- chain says which pass asked for them.
SELECT ROUND(s.ts / 1e6, 1)   AS t_ms,
       ROUND(s.dur / 1e6, 2)  AS ms,
       s.name,
       p.name                 AS parent,
       gp.name                AS grandparent
FROM slice s
LEFT JOIN slice p  ON s.parent_id = p.id
LEFT JOIN slice gp ON p.parent_id = gp.id
WHERE s.name IN ('relower', 'sync_children')
ORDER BY s.dur DESC
LIMIT 20;
