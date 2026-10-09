# Selected X4 Files storage composition

Files 1.5.8 must be built with `--storage-capability storage.volume
--storage-instance 9 --file-handlers` alongside the existing native-time,
alarm-v2, paper-motion and stage-log flags. Installed-files is a different Runtime
service at instance 0 and does not provide writable SD storage.

`file_browser_admission.validate` runs for the selected sparse/native cohort
before ELF compaction. It binds source, original ELF digest/size, manifest,
compiled capability/instance defines, native receipt, build-record grants, and
the generated boot grants. It rejects duplicate/overridden selectors, the old
installed-files@9 build, missing or wrong grants, and an unselected second volume.

Cohort integration must update the Files source pin and version to 1.5.8 in
`minimal/apps/sources.json`, `minimal/apps/catalog.json`, and
`minimal/scripts/native_time_cohort.py`, and select the clean rebuilt artifact
directory in its inputs. These metadata/version changes and final BIN are owned
by the cohort integrator; this change supplies the guard independently.

Validate the guard with:

```sh
python minimal/test/file_browser_admission_test.py
RISCRTE_RUNTIME_ROOT=/path/to/Runtime RISCRTE_READER_ROOT=/path/to/Reader \
  bash minimal/test/run_sd_test.sh
```

The System Apps `scripts/test_file_browser_runtime.py` checks actual controller
acquisition against Runtime c546dae32e2e75f7e7f4867dc788b6be53ded64c using the
compiled storage selectors from the verified target build record. Its fixture
declares a text receiver to qualify file.open handoff. That receiver is test-only;
the selected product currently declares no file-type handlers. Open must continue
reporting no declared handler while text/hex preview and volume operations work.

The production SD transport/shared-FatFs `files-paths` scenario verifies root
listing, `/Books/read.txt` creation, exact read bytes, rename, move, copy, delete,
owner checks and cleanup through the exported volume table. Paths are relative
to the volume root: there is no implicit `/sd` directory. The real-Runtime Files
controller fixture separately verifies nested preview uses `/Books/read.txt`
while broker dispatch uses `/sd/Books/read.txt`. The Reader dependency is the
product-pinned aac8c06d3221139084acd0cfc64f7b0ba194a97a.

No new reader/viewer, USB volume, hardware qualification or publication is added.
