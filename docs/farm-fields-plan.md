# Field-First Catalog + Job Workflow (Agro V1, No Plan View in GUI)

## Summary
This revision replaces the prior Farm+Plan concept with a `Field -> Jobs` workflow:

- User creates/edits a Field polygon first.
- User creates one or more Spray Jobs under that field.
- Plan view is removed from user navigation; only Fly + Fields remain primary.
- Jobs list (inside each field page) provides Edit, Remove, Start, Resume.
- Start/Resume upload full job mission and then switch to Fly on successful upload completion.
- Field polygon edits mark linked jobs as stale (`needsRegeneration`), and stale jobs cannot Start/Resume until regenerated and saved.

## Public APIs / Interfaces / Types

### 1) New C++ QML types in `QGroundControl.Controls`

#### `FieldJobCatalogManager`
Properties:
- `fields` (`QmlObjectListModel*`)
- `activeFieldId` (`QString`)
- `activeJobId` (`QString`)
- `fieldContextActive` (`bool`)
- `jobContextActive` (`bool`)
- `uploadInProgress` (`bool`)
- `lastError` (`QString`)

Methods:
- `reload()`
- `beginCreateFieldDraft()`
- `beginEditField(fieldId)`
- `commitActiveField(name, polygonPath)`
- `deleteField(fieldId)` (hard-delete field folder + linked jobs files)
- `jobsForField(fieldId) -> QmlObjectListModel*`
- `beginCreateJob(fieldId)`
- `beginEditJob(jobId)`
- `activeJobPlanFilePath()`
- `activeJobThumbnailFilePath()`
- `commitActiveJob(name, notes)`
- `deleteJob(jobId)` (hard-delete)
- `startJob(jobId)`
- `resumeJob(jobId, missionIncomplete)`
- `clearContexts()`

Signals:
- `jobUploadSucceeded(jobId)`
- `jobUploadFailed(jobId, errorText)`
- `fieldsChanged()`
- `lastErrorChanged()`
- `uploadInProgressChanged()`

#### `FieldCatalogEntry`
QML-readable fields:
- `id`
- `name`
- `thumbnailFilePath`
- `polygonPath`
- `jobCount`
- `createdAtUtc`
- `updatedAtUtc`
- `schemaVersion`
- `revision`
- `syncPending`

#### `JobCatalogEntry`
QML-readable fields:
- `id`
- `fieldId`
- `name`
- `planFilePath`
- `thumbnailFilePath`
- `pesticideLitersPerDekar`
- `dropletSizeMicron`
- `nozzleValuePct`
- `boomWidthM`
- `notes`
- `needsRegeneration`
- `fieldRevisionRequired`
- `createdAtUtc`
- `updatedAtUtc`
- `schemaVersion`
- `revision`
- `syncPending`

### 2) QML interface changes
- MainWindow primary navigation:
  - `showFlyView()` and `showFieldsView()`.
  - Remove user-facing `showPlanView()` entry points.
- New pages/components:
  - `FarmFieldsView.qml` (primary fields list)
  - `FieldDetailView.qml` (field page with jobs list and actions)
  - `FieldPolygonEditorView.qml` (field polygon editor using current polygon tools)
  - `JobEditorView.qml` (reuses Plan internals, spray-only editing)
- Hide Plan settings page entry from app settings model:
  - `SettingsPagesModel.qml`

### 3) Persisted file schemas

Fields catalog:
- `fields_catalog.json`

Header:
- `fileType: "FieldCatalog"`
- `version: 1`
- `groundStation: "QGroundControl"`

Jobs catalog:
- `jobs_catalog.json`

Header:
- `fileType: "JobCatalog"`
- `version: 1`
- `groundStation: "QGroundControl"`

Assets:
- `field.png`
- `<jobId>.plan`
- `<jobId>.png`

## Implementation TODO (Decision-Complete)

- [x] 1.1 Add Field/Job catalog data layer  
  Location: `FieldJobCatalogManager.h`, `FieldJobCatalogManager.cc`, `FieldCatalogEntry.h`, `FieldCatalogEntry.cc`, `JobCatalogEntry.h`, `JobCatalogEntry.cc`, `CMakeLists.txt`  
  Implementation:
  - Add the three new types and expose them to QML.
  - Load/save both catalogs with atomic writes (`QGCFileHelper::atomicWrite`).
  - Keep in-memory models sorted by `updatedAtUtc` descending.
  Acceptance:
  - Missing catalogs load as empty without crash.
  - Corrupt JSON sets actionable `lastError` and falls back to empty models.

- [x] 1.2 Implement Field revision + Job stale propagation  
  Location: `FieldJobCatalogManager.cc`  
  Implementation:
  - On `commitActiveField`, increment field revision.
  - Update linked jobs with latest field polygon snapshot, set `needsRegeneration=true`, set `fieldRevisionRequired=field.revision`.
  - Do not auto-regenerate job mission files.
  Acceptance:
  - Editing a field marks all linked jobs stale.
  - Jobs remain listed but Start/Resume become blocked.

- [x] 1.3 Add hard-delete behavior for field/job  
  Location: `FieldJobCatalogManager.cc`  
  Implementation:
  - `deleteField(fieldId)` removes field thumbnail, job plan/png files, and field folder.
  - Remove field and linked jobs from catalogs (hard delete).
  - `deleteJob(jobId)` removes job files and catalog record.
  Acceptance:
  - No orphan files remain after delete.
  - Catalog reload reflects deletions exactly.

- [x] 2.1 Add Fields primary view and field detail page  
  Location: `FarmFieldsView.qml`, `FieldDetailView.qml`, `CMakeLists.txt`  
  Implementation:
  - Fields list page with Create Field action and field cards.
  - Field detail page with jobs list and buttons: Edit, Remove, Start, Resume, Create Job.
  - Show one field thumbnail and one thumbnail per job card.
  Acceptance:
  - User can create/open/delete fields and manage jobs from field page.
  - Field page has all required job actions.

- [x] 2.2 Remove Plan from primary GUI navigation  
  Location: `MainWindow.qml`, `SelectViewDropdown.qml`  
  Implementation:
  - Replace Fly/Plan selector with Fly/Fields.
  - Remove Plan button entries and stale references to `showPlanView`.
  - Keep internal mission infrastructure available for job editor only.
  Acceptance:
  - No user-visible path to generic Plan view.
  - Tool selector works from any primary screen.

- [x] 2.3 Hide Plan settings page in app settings  
  Location: `SettingsPagesModel.qml`  
  Implementation:
  - Remove/disable Plan View page row in settings model.
  Acceptance:
  - Plan settings page is not visible in Settings navigation.

- [x] 3.1 Add Field polygon editor using existing polygon tools  
  Location: `FieldPolygonEditorView.qml`, optional helpers in `FarmFieldsView.qml`  
  Implementation:
  - Use `FlightMap` + `QGCMapPolygon` + `QGCMapPolygonVisuals`.
  - Keep current polygon tool flow (Basic, Circular, Trace, Load KML/SHP).
  - Require field name and valid polygon (>=3 points) before save.
  Acceptance:
  - User can draw/import/edit polygon before creating jobs.
  - Field save blocked on invalid polygon/name.

- [x] 4.1 Add Spray-only Job editor (reuse planner internals)  
  Location: `JobEditorView.qml`, minor reuse from `PlanView.qml` patterns  
  Implementation:
  - Build job editor around `PlanMasterController` and mission map/editor internals.
  - Restrict to one Spray complex item only; hide generic mission/fence/rally tabs and generic pattern insertion.
  - On job open, load plan file and enforce spray item presence.
  - Apply field polygon snapshot to spray item, then regenerate/safe-save on commit.
  Acceptance:
  - Job editor cannot create arbitrary mission types.
  - Job save updates plan, thumbnail, and job record; clears `needsRegeneration`.

- [x] 4.2 Keep spray mission command/protocol mapping unchanged  
  Location: `SprayComplexItem.cc`, companion Lua expectations in `agriboard.lua`  
  Implementation:
  - Do not change script action IDs, command injection semantics, or Lua param expectations.
  Acceptance:
  - Existing bridge behavior (SCRIPT 20/21 and related spray flow) remains intact.

- [x] 5.1 Implement Start/Resume upload flow from field job list  
  Location: `FieldDetailView.qml`, `FieldJobCatalogManager.cc`, `MainWindow.qml`  
  Implementation:
  - Start uploads job plan to active vehicle.
  - Resume checks incomplete mission first, then uploads full mission if incomplete.
  - Mission incomplete check in QML uses Fly mission controller state consistent with guided logic (`resumeMissionIndex` and `missionItemCount`).
  - Block Start/Resume when `needsRegeneration=true`.
  - On upload success signal, switch to Fly view.
  Acceptance:
  - Start/Resume buttons exist and follow requested behavior.
  - Stale jobs cannot Start/Resume.
  - Successful upload transitions UI to Fly.

- [x] 5.2 Add upload status signals and error plumbing  
  Location: `FieldJobCatalogManager.h`, `FieldJobCatalogManager.cc`  
  Implementation:
  - Emit `jobUploadSucceeded` / `jobUploadFailed`.
  - Populate `lastError` for user-visible failures (no vehicle, missing files, invalid state, upload errors).
  Acceptance:
  - Field page can react deterministically to success/failure.

- [x] 6.1 Fix spray plan reload in mission JSON loader  
  Location: `MissionController.cc`  
  Implementation:
  - Add `complexItemType == "spray"` branch in `_loadJsonMissionFileV2`.
  - Instantiate/load `SprayComplexItem` like other complex items.
  Acceptance:
  - Spray-containing `.plan` files load without unsupported-type failure.

- [ ] 7.1 Add catalog tests (Field + Job)  
  Location: `FieldJobCatalogManagerTest.h`, `FieldJobCatalogManagerTest.cc`, `CMakeLists.txt`, `CMakeLists.txt`  
  Implementation:
  - Cover create/edit/delete field.
  - Cover create/edit/delete job.
  - Cover field-edit stale propagation.
  - Cover hard-delete removing files and records.
  Acceptance:
  - New tests pass and validate schema + file lifecycle.

- [ ] 7.2 Add spray reload regression test  
  Location: `MissionControllerTest.h`, `MissionControllerTest.cc`  
  Implementation:
  - Add regression asserting spray complex item can be loaded from `.plan`.
  Acceptance:
  - Test fails before fix and passes after fix.

- [ ] 7.3 Add Start/Resume gating tests  
  Location: `FieldJobCatalogManagerTest.cc` and QML behavior checks in runtime notes  
  Implementation:
  - Verify Start/Resume blocked for stale jobs.
  - Verify Resume path requires incomplete mission flag.
  Acceptance:
  - Behavior matches locked UX rules.

- [ ] 8.1 Build and test validation  
  Location: repo root  
  Implementation:
  - `cmake --preset windows-ihattys-debug`
  - `cmake --build --preset windows-debug`
  - Run targeted CTest for MissionManager and new catalog tests.
  Acceptance:
  - Build succeeds and changed tests pass.

- [ ] 8.2 Manual runtime scenarios  
  Location: app runtime  
  Implementation:
  - Create field polygon, save field, verify field thumbnail.
  - Create two jobs under same field, each with distinct spray settings/pattern.
  - Edit field polygon, verify both jobs become stale and Start/Resume disabled.
  - Edit one stale job, save/regenerate, verify Start enabled for that job only.
  - Press Start, verify upload completes and view switches to Fly.
  - Press Resume when mission not incomplete, verify informative message and no upload.
  - Press Resume when mission incomplete, verify full upload then Fly switch.
  - Remove job and remove field, verify hard-delete from UI and disk.
  Acceptance:
  - End-to-end matches requested agro workflow.

- [ ] 8.3 Docs/changelog update  
  Location: `farm-fields-plan.md`, `docs/en/qgc-user-guide/ agro flow page`, `CHANGELOG.md`  
  Implementation:
  - Replace old Farm+Plan workflow with Field->Jobs workflow.
  - Document Start/Resume semantics and stale-job rule.
  - Document separate catalogs and storage paths.
  Acceptance:
  - User-visible behavior is documented and consistent with implementation.

## Test Cases and Scenarios (Must Pass)

- Missing `fields_catalog.json` and `jobs_catalog.json` loads as empty without crash.
- Malformed catalog JSON reports user-visible error and recovers.
- Field creation blocked without valid polygon or field name.
- Job creation blocked without job name.
- Job editor enforces spray-only (single spray complex item).
- Field polygon edit marks all linked jobs `needsRegeneration=true`.
- Start/Resume disabled for stale jobs.
- Start uploads full job mission and transitions to Fly on success.
- Resume only proceeds when mission is considered incomplete, then uploads full mission and transitions to Fly.
- Deleting field hard-deletes linked job files and records.
- Spray `.plan` reload regression in MissionController passes.
- No behavior change to spray command/action protocol mapping.

## Assumptions and Defaults Locked

- Primary navigation is Fly + Fields only.
- Each field has its own detail page listing jobs.
- Job actions on field page are exactly: Edit, Remove, Start, Resume.
- Catalog persistence uses separate files (`fields_catalog.json`, `jobs_catalog.json`).
- Job type scope is Spray-only in V1.
- Field name and job name are required; notes are optional.
- Field edits mark jobs stale and Start/Resume are blocked until regeneration save.
- Field delete policy is hard-delete all linked files and records.
- Plan view is removed from GUI navigation and Plan settings page is hidden.
- Resume V1 behavior is incomplete-check + full mission upload (no partial resume flag yet).
- Spray mission protocol/action mapping remains unchanged and compatible with current AgriBoard Lua bridge.
