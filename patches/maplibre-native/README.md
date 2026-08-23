# MapLibre Native patches

`.mise/bin/sync-submodules` applies these to the `third_party/maplibre-native`
worktree after checking out the pinned commit. The submodule keeps tracking
upstream, so the pin stays honest and each patch is a change we are carrying
only until it lands there.

`0002-windows-local-file-urls.patch` removes the URI-only leading slash from
canonical Windows drive paths and opens UTF-8 filenames through wide filesystem
APIs. This lets the local file source load percent-encoded `file:///C:/...`
resources whose paths contain spaces or non-ASCII characters.

`0003-run-loop-process-gate.patch` adds an optional gate callback to
`RunLoop::process()`, consulted before each queued task is dequeued. The C API
uses it to bound one pump's drain; the budget logic stays on the C API side, and
an unset gate keeps upstream behavior.

`0005-tile-matrix-hook.patch` adds `Map::setTileMatrixHook()`: a `std::function`
member on `TransformState`, called at the end of `matrixFor()` with the default
tile matrix already computed, that may overwrite the matrix in place. The hook
propagates through the memberwise copies of `TransformState` that reach
placement, collision, and tile cover, so one injection point covers the GPU and
CPU consumers alike. The render-crs subsystem (`plans/render-crs/`) registers a
per-tile homography here; the hook itself carries no CRS knowledge, and an unset
hook keeps upstream behavior. This patch replaces the phase-1 instrumentation
patch `0004-experimental-tile-matrix-warp.patch`.

`0006-tile-cover-bounds-override.patch` adds
`Map::setTileCoverBoundsOverride()`: an optional `LatLngBounds` on
`TransformState` that tile-cover selection intersects tiles against instead of
the view frustum. Tile selection reads only the inverse projection matrix and so
does not follow a tile matrix hook; the override names the covered region
explicitly. An unset override keeps upstream behavior.

Drop a patch once the pin moves to a commit that carries it. The sync checks out
the pinned commit with `--force`, so it discards whatever the last sync applied
before applying the list again. A pin bump, an edit to a patch, and a dropped
patch all take effect on a worktree that still carries the old version. A patch
that no longer applies fails the sync rather than being skipped.

Local edits to the submodule worktree, including edits inside a nested vendor
submodule, are discarded by the same checkout, and a sync runs it whenever the
worktree carries a tracked change that no listed patch accounts for. The sync
prints those paths first. A forced checkout also removes an untracked file that
sits where a new pin adds a tracked one.
