# Model Viewer and Hollow Trail

The canvas integration adds the actual Reader apps through the shared
RiscRTE-Utilities `CanvasApp` client. X4 selects Model Viewer 1.3.0 and Hollow
Trail 1.2.0. No legacy Reader firmware is embedded. The existing panel 0.1.16,
resident Home/Springboard, Lists and Contexts source selections are preserved.

`minimal/canvas-catalog.json` owns the installed entries. Run
`minimal/scripts/build_canvas_launchers.py` with the selected System Apps,
Runtime, Utilities and display SDK roots, then Utilities'
`build_canvas_apps.py --target x4`. `stage_canvas_cohort.py` adds exact grants
(display 3, raw touch 4, navigation 6; viewer volume 9, file.open 0 and private
AppData 64) and checks namespace ownership. `package_canvas_cohort.py` verifies
the exact built native BIN/ELF, every module, complete boot graph, sanitizer
admission, independent SPIFFS readbacks and paired image hashes.

Both apps appear in Springboard. Viewer also handles OBJ/STL selections from
File Browser. Hollow Trail runs landscape with touch arrows, Jump, Use, Pause
and Notes; Home exits. Viewer opens a built-in cube without an SD card and
supports rotation, pan, zoom and shaded/wire modes. See Utilities'
`lib/CanvasApp/README.md` for bounds and the preserved Reader source lineage.

The downloadable full image is an initial 16 MiB image at 0x0, overwriting
internal settings and AppData. It does not contain removable SD contents.
There is no hardware or preserving-update qualification claim.
