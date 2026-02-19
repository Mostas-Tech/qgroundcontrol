#include "FieldJobCatalogManager.h"

#include "AppSettings.h"
#include "FieldCatalogEntry.h"
#include "JobCatalogEntry.h"
#include "JsonHelper.h"
#include "JsonParsing.h"
#include "MissionManager.h"
#include "MultiVehicleManager.h"
#include "QGCFileHelper.h"
#include "QGCMapPolygon.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "Vehicle.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QJSValue>

#include <algorithm>

QGC_LOGGING_CATEGORY(FieldJobCatalogManagerLog, "QMLControls.FieldJobCatalogManager")

namespace {

constexpr const char* kFieldsCatalogFileName = "fields_catalog.json";
constexpr const char* kJobsCatalogFileName = "jobs_catalog.json";

constexpr const char* kFieldsCatalogFileType = "FieldCatalog";
constexpr const char* kJobsCatalogFileType = "JobCatalog";
constexpr int kCatalogVersion = 1;

constexpr const char* kFieldsArrayKey = "fields";
constexpr const char* kJobsArrayKey = "jobs";

constexpr const char* kIdKey = "id";
constexpr const char* kNameKey = "name";
constexpr const char* kFieldIdKey = "fieldId";
constexpr const char* kThumbnailFilePathKey = "thumbnailFilePath";
constexpr const char* kPlanFilePathKey = "planFilePath";
constexpr const char* kPolygonPathKey = "polygonPath";
constexpr const char* kExclusionPolygonPathsKey = "exclusionPolygonPaths";
constexpr const char* kFieldPolygonPathKey = "fieldPolygonPath";
constexpr const char* kFieldExclusionPolygonPathsKey = "fieldExclusionPolygonPaths";
constexpr const char* kJobCountKey = "jobCount";
constexpr const char* kCreatedAtUtcKey = "createdAtUtc";
constexpr const char* kUpdatedAtUtcKey = "updatedAtUtc";
constexpr const char* kSchemaVersionKey = "schemaVersion";
constexpr const char* kRevisionKey = "revision";
constexpr const char* kSyncPendingKey = "syncPending";
constexpr const char* kNotesKey = "notes";
constexpr const char* kNeedsRegenerationKey = "needsRegeneration";
constexpr const char* kFieldRevisionRequiredKey = "fieldRevisionRequired";
constexpr const char* kPesticideLitersPerDekarKey = "pesticideLitersPerDekar";
constexpr const char* kDropletSizeMicronKey = "dropletSizeMicron";
constexpr const char* kNozzleValuePctKey = "nozzleValuePct";
constexpr const char* kBoomWidthMKey = "boomWidthM";

const QString kCatalogDirectoryName = QStringLiteral("FarmFields");
const QString kFieldsDirectoryName = QStringLiteral("fields");
const QString kJobsDirectoryName = QStringLiteral("jobs");

QDateTime parseUtcTime(const QString& utcText)
{
    QDateTime parsed = QDateTime::fromString(utcText, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(utcText, Qt::ISODate);
    }
    if (parsed.isValid()) {
        parsed = parsed.toUTC();
    }
    return parsed;
}

QString newUuidString()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString variantSummary(const QVariant& variant)
{
    QVariant normalized = variant;
    if (normalized.metaType().id() == qMetaTypeId<QJSValue>()) {
        normalized = normalized.value<QJSValue>().toVariant();
    }

    if (normalized.canConvert<QGeoCoordinate>()) {
        const QGeoCoordinate coordinate = normalized.value<QGeoCoordinate>();
        if (coordinate.isValid()) {
            return QStringLiteral("QGeoCoordinate(%1,%2)")
                    .arg(coordinate.latitude(), 0, 'f', 6)
                    .arg(coordinate.longitude(), 0, 'f', 6);
        }
    }

    const QVariantList listValue = normalized.toList();
    if (!listValue.isEmpty()) {
        return QStringLiteral("QVariantList(size=%1)").arg(listValue.size());
    }

    const QVariantMap mapValue = normalized.toMap();
    if (!mapValue.isEmpty()) {
        return QStringLiteral("QVariantMap(keys=%1)").arg(mapValue.keys().join(QStringLiteral(",")));
    }

    return QStringLiteral("type=%1").arg(QString::fromUtf8(normalized.metaType().name()));
}

bool isValidPolygonPath(const QVariantList& polygonPath)
{
    if (polygonPath.size() < 3) {
        return false;
    }

    for (const QVariant& coordinateVar : polygonPath) {
        const QGeoCoordinate coordinate = coordinateVar.value<QGeoCoordinate>();
        if (!coordinate.isValid()) {
            return false;
        }
    }

    return true;
}

bool variantToGeoCoordinate(const QVariant& coordinateVar, QGeoCoordinate& coordinateOut)
{
    QVariant normalizedCoordinateVar = coordinateVar;
    if (normalizedCoordinateVar.metaType().id() == qMetaTypeId<QJSValue>()) {
        const QJSValue jsValue = normalizedCoordinateVar.value<QJSValue>();
        normalizedCoordinateVar = jsValue.toVariant();
    }

    const QGeoCoordinate directCoordinate = normalizedCoordinateVar.value<QGeoCoordinate>();
    if (directCoordinate.isValid()) {
        coordinateOut = directCoordinate;
        return true;
    }

    const QVariantMap coordinateMap = normalizedCoordinateVar.toMap();
    if (!coordinateMap.isEmpty()) {
        const QVariant latitudeVar = coordinateMap.value(QStringLiteral("latitude"), coordinateMap.value(QStringLiteral("lat")));
        QVariant longitudeVar = coordinateMap.value(QStringLiteral("longitude"), coordinateMap.value(QStringLiteral("lng")));
        if (!longitudeVar.isValid()) {
            longitudeVar = coordinateMap.value(QStringLiteral("lon"));
        }
        if (!longitudeVar.isValid()) {
            longitudeVar = coordinateMap.value(QStringLiteral("long"));
        }

        if (latitudeVar.isValid() && longitudeVar.isValid()) {
            QGeoCoordinate mappedCoordinate(latitudeVar.toDouble(), longitudeVar.toDouble());
            if (coordinateMap.contains(QStringLiteral("altitude"))) {
                mappedCoordinate.setAltitude(coordinateMap.value(QStringLiteral("altitude")).toDouble());
            } else if (coordinateMap.contains(QStringLiteral("alt"))) {
                mappedCoordinate.setAltitude(coordinateMap.value(QStringLiteral("alt")).toDouble());
            }

            if (mappedCoordinate.isValid()) {
                coordinateOut = mappedCoordinate;
                return true;
            }
        }
    }

    const QVariantList coordinateList = normalizedCoordinateVar.toList();
    if (coordinateList.size() >= 2) {
        bool latitudeOk = false;
        bool longitudeOk = false;
        const double latitude = coordinateList[0].toDouble(&latitudeOk);
        const double longitude = coordinateList[1].toDouble(&longitudeOk);
        if (!latitudeOk || !longitudeOk) {
            return false;
        }

        QGeoCoordinate listedCoordinate(latitude, longitude);
        if (coordinateList.size() >= 3) {
            bool altitudeOk = false;
            const double altitude = coordinateList[2].toDouble(&altitudeOk);
            if (altitudeOk) {
                listedCoordinate.setAltitude(altitude);
            }
        }

        if (listedCoordinate.isValid()) {
            coordinateOut = listedCoordinate;
            return true;
        }
    }

    const QObject* coordinateObject = normalizedCoordinateVar.value<QObject*>();
    if (coordinateObject) {
        const QGeoCoordinate objectCoordinate = coordinateObject->property("coordinate").value<QGeoCoordinate>();
        if (objectCoordinate.isValid()) {
            coordinateOut = objectCoordinate;
            return true;
        }

        const QVariant latitudeVar = coordinateObject->property("latitude");
        const QVariant longitudeVar = coordinateObject->property("longitude");
        if (latitudeVar.isValid() && longitudeVar.isValid()) {
            QGeoCoordinate objectMappedCoordinate(latitudeVar.toDouble(), longitudeVar.toDouble());
            const QVariant altitudeVar = coordinateObject->property("altitude");
            if (altitudeVar.isValid()) {
                objectMappedCoordinate.setAltitude(altitudeVar.toDouble());
            }

            if (objectMappedCoordinate.isValid()) {
                coordinateOut = objectMappedCoordinate;
                return true;
            }
        }
    }

    return false;
}

QVariantList normalizedPolygonPath(const QVariantList& polygonPath)
{
    QVariantList normalizedPath;
    normalizedPath.reserve(polygonPath.size());

    for (const QVariant& coordinateVar : polygonPath) {
        QGeoCoordinate coordinate;
        if (variantToGeoCoordinate(coordinateVar, coordinate)) {
            normalizedPath.append(QVariant::fromValue(coordinate));
        }
    }

    return normalizedPath;
}

QVariantList normalizedPolygonPaths(const QVariantList& polygonPaths)
{
    QVariantList normalizedPaths;
    qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths begin rawTopLevelCount:" << polygonPaths.size();
    if (polygonPaths.isEmpty()) {
        qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths early exit empty input";
        return normalizedPaths;
    }

    // QML marshalling may pass a single polygon path either as:
    //  - [ [coord...], [coord...] ] for multiple polygons
    //  - [ coord, coord, ... ] for a single polygon
    bool topLevelLooksLikeSinglePolygonPath = true;
    for (const QVariant& rawPolygonEntryVar : polygonPaths) {
        QVariant polygonEntryVar = rawPolygonEntryVar;
        if (polygonEntryVar.metaType().id() == qMetaTypeId<QJSValue>()) {
            polygonEntryVar = polygonEntryVar.value<QJSValue>().toVariant();
        }

        QGeoCoordinate coordinate;
        if (!variantToGeoCoordinate(polygonEntryVar, coordinate)) {
            topLevelLooksLikeSinglePolygonPath = false;
            break;
        }
    }
    qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths topLevelLooksLikeSinglePolygonPath:" << topLevelLooksLikeSinglePolygonPath;

    if (topLevelLooksLikeSinglePolygonPath) {
        const QVariantList normalizedPath = normalizedPolygonPath(polygonPaths);
        if (normalizedPath.size() >= 3) {
            normalizedPaths.append(QVariant::fromValue(normalizedPath));
            qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths accepted single polygon path with vertexCount:" << normalizedPath.size();
        } else {
            qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths dropped single polygon path with vertexCount:" << normalizedPath.size();
        }
        qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths end normalizedCount:" << normalizedPaths.size();
        return normalizedPaths;
    }

    for (int polygonIndex = 0; polygonIndex < polygonPaths.size(); ++polygonIndex) {
        const QVariant& rawPolygonEntryVar = polygonPaths[polygonIndex];
        QVariant polygonEntryVar = rawPolygonEntryVar;
        if (polygonEntryVar.metaType().id() == qMetaTypeId<QJSValue>()) {
            polygonEntryVar = polygonEntryVar.value<QJSValue>().toVariant();
        }

        const QVariantList polygonPath = polygonEntryVar.toList();
        if (polygonPath.isEmpty()) {
            qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths polygonIndex" << polygonIndex
                                                 << "empty toList conversion. raw summary:" << variantSummary(rawPolygonEntryVar);
            continue;
        }

        const QVariantList normalizedPath = normalizedPolygonPath(polygonPath);
        if (normalizedPath.size() >= 3) {
            normalizedPaths.append(QVariant::fromValue(normalizedPath));
            qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths accepted polygonIndex" << polygonIndex
                                                 << "vertexCount:" << normalizedPath.size();
        } else {
            qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths dropped polygonIndex" << polygonIndex
                                                 << "vertexCount:" << normalizedPath.size()
                                                 << "rawPathSize:" << polygonPath.size();
        }
    }

    qCWarning(FieldJobCatalogManagerLog) << "normalizedPolygonPaths end normalizedCount:" << normalizedPaths.size();
    return normalizedPaths;
}

} // namespace

FieldJobCatalogManager::FieldJobCatalogManager(QObject *parent)
    : QObject(parent)
    , _fields(new QmlObjectListModel(this))
{
    reload();
}

FieldJobCatalogManager::~FieldJobCatalogManager() = default;

void FieldJobCatalogManager::reload()
{
    _setUploadInProgress(false);

    _fieldRecords.clear();
    _jobRecords.clear();

    QStringList loadErrors;

    QString errorString;
    if (!_loadFieldsCatalog(errorString)) {
        loadErrors.append(errorString);
    }

    errorString.clear();
    if (!_loadJobsCatalog(errorString)) {
        loadErrors.append(errorString);
    }

    if (!loadErrors.isEmpty()) {
        _fieldRecords.clear();
        _jobRecords.clear();
    }

    _recomputeFieldJobCounts();
    _sortRecords();
    _refreshFieldsModel();
    _refreshJobsModels();

    if (loadErrors.isEmpty()) {
        _clearLastError();
    } else {
        _setLastError(loadErrors.join(QStringLiteral("\n")));
    }
}

void FieldJobCatalogManager::beginCreateFieldDraft()
{
    _activeFieldDraft = {};
    _activeFieldDraft.id = newUuidString();
    _activeFieldDraft.createdAtUtc = _utcNowString();
    _activeFieldDraft.updatedAtUtc = _activeFieldDraft.createdAtUtc;
    _activeFieldDraft.thumbnailFilePath = _fieldThumbnailFilePath(_activeFieldDraft.id);
    _activeFieldDraft.schemaVersion = kCatalogVersion;
    _activeFieldDraft.revision = 0;
    _activeFieldDraft.jobCount = 0;
    _activeFieldDraft.syncPending = false;

    _activeFieldCreateMode = true;
    _activeFieldDraftValid = true;
    _setActiveFieldId(_activeFieldDraft.id);
    _setFieldContextActive(true);
}

void FieldJobCatalogManager::beginEditField(const QString& fieldId)
{
    const int fieldIndex = _findFieldRecordIndex(fieldId);
    if (fieldIndex < 0) {
        _setLastError(tr("Unable to edit field. Unknown field id: %1").arg(fieldId));
        return;
    }

    _activeFieldDraft = _fieldRecords[fieldIndex];
    _activeFieldCreateMode = false;
    _activeFieldDraftValid = true;
    _setActiveFieldId(fieldId);
    _setFieldContextActive(true);
}

void FieldJobCatalogManager::commitActiveField(const QString& name, const QVariantList& polygonPath, const QVariantList& exclusionPolygonPaths)
{
    const QString trimmedName = name.trimmed();
    qCWarning(FieldJobCatalogManagerLog) << "commitActiveField begin name:" << trimmedName
                                         << "mainPolygonRawCount:" << polygonPath.size()
                                         << "exclusionRawTopLevelCount:" << exclusionPolygonPaths.size()
                                         << "activeFieldId:" << _activeFieldId
                                         << "createMode:" << _activeFieldCreateMode
                                         << "draftValid:" << _activeFieldDraftValid;
    for (int i = 0; i < exclusionPolygonPaths.size(); ++i) {
        qCWarning(FieldJobCatalogManagerLog) << "commitActiveField exclusion raw index" << i
                                             << "summary:" << variantSummary(exclusionPolygonPaths[i]);
    }

    if (trimmedName.isEmpty()) {
        _setLastError(tr("Field name is required."));
        return;
    }
    if (!isValidPolygonPath(polygonPath)) {
        _setLastError(tr("Field polygon must contain at least 3 valid points."));
        return;
    }

    const QVariantList normalizedExclusionPolygonPaths = normalizedPolygonPaths(exclusionPolygonPaths);
    qCWarning(FieldJobCatalogManagerLog) << "commitActiveField normalizedExclusionCount:" << normalizedExclusionPolygonPaths.size();
    for (int i = 0; i < normalizedExclusionPolygonPaths.size(); ++i) {
        const QVariantList normalizedPath = normalizedExclusionPolygonPaths[i].toList();
        qCWarning(FieldJobCatalogManagerLog) << "commitActiveField normalized exclusion index" << i
                                             << "vertexCount:" << normalizedPath.size()
                                             << "firstVertexSummary:" << (normalizedPath.isEmpty() ? QStringLiteral("<none>") : variantSummary(normalizedPath.first()));
    }
    if (!exclusionPolygonPaths.isEmpty() && normalizedExclusionPolygonPaths.isEmpty()) {
        qCWarning(FieldJobCatalogManagerLog) << "No-go polygons were provided but none were valid after normalization for field"
                                             << trimmedName
                                             << "rawCount:" << exclusionPolygonPaths.size()
                                             << "firstEntry:" << variantSummary(exclusionPolygonPaths.first());
    }

    if (!_activeFieldDraftValid) {
        beginCreateFieldDraft();
        if (!_activeFieldDraftValid) {
            _setLastError(tr("Unable to initialize field draft."));
            return;
        }
    }

    FieldRecord updatedField = _activeFieldDraft;
    updatedField.name = trimmedName;
    updatedField.polygonPath = polygonPath;
    updatedField.exclusionPolygonPaths = normalizedExclusionPolygonPaths;
    qCWarning(FieldJobCatalogManagerLog) << "commitActiveField prepared updatedField id:" << updatedField.id
                                         << "name:" << updatedField.name
                                         << "exclusionStoredCount:" << updatedField.exclusionPolygonPaths.size();
    updatedField.updatedAtUtc = _utcNowString();
    if (_activeFieldCreateMode && updatedField.createdAtUtc.isEmpty()) {
        updatedField.createdAtUtc = updatedField.updatedAtUtc;
    }
    updatedField.revision += 1;
    updatedField.schemaVersion = kCatalogVersion;
    updatedField.thumbnailFilePath = _fieldThumbnailFilePath(updatedField.id);

    int fieldIndex = _findFieldRecordIndex(updatedField.id);
    if (fieldIndex < 0) {
        _fieldRecords.append(updatedField);
        qCWarning(FieldJobCatalogManagerLog) << "commitActiveField appended field id:" << updatedField.id;
    } else {
        _fieldRecords[fieldIndex] = updatedField;
        qCWarning(FieldJobCatalogManagerLog) << "commitActiveField replaced field index:" << fieldIndex
                                             << "id:" << updatedField.id;
    }

    // Field commit invalidates all linked jobs. Regeneration is explicit and handled by job save.
    const QString nowUtc = _utcNowString();
    for (JobRecord& job : _jobRecords) {
        if (job.fieldId == updatedField.id) {
            job.fieldPolygonPath = updatedField.polygonPath;
            job.fieldExclusionPolygonPaths = updatedField.exclusionPolygonPaths;
            job.needsRegeneration = true;
            job.fieldRevisionRequired = updatedField.revision;
            job.updatedAtUtc = nowUtc;
            job.revision += 1;
        }
    }

    QString errorString;
    if (!_ensureCatalogDirectories(errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!QGCFileHelper::ensureDirectoryExists(_fieldDirectoryPath(updatedField.id))) {
        _setLastError(tr("Failed to create field directory: %1").arg(_fieldDirectoryPath(updatedField.id)));
        return;
    }
    if (!_saveFieldsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }
    qCWarning(FieldJobCatalogManagerLog) << "commitActiveField saved fields catalog successfully for field id:" << updatedField.id;
    if (!_saveJobsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }

    _activeFieldDraft = updatedField;
    _activeFieldCreateMode = false;
    _activeFieldDraftValid = true;
    _setActiveFieldId(updatedField.id);
    _setFieldContextActive(true);

    _recomputeFieldJobCounts();
    _sortRecords();
    _refreshFieldsModel();
    _refreshJobsModels();
    _clearLastError();
}

QString FieldJobCatalogManager::activeFieldThumbnailFilePath()
{
    if (_activeFieldDraftValid && !_activeFieldDraft.thumbnailFilePath.isEmpty()) {
        return _activeFieldDraft.thumbnailFilePath;
    }
    if (!_activeFieldId.isEmpty()) {
        return _fieldThumbnailFilePath(_activeFieldId);
    }

    return QString();
}

void FieldJobCatalogManager::deleteField(const QString& fieldId)
{
    const int fieldIndex = _findFieldRecordIndex(fieldId);
    if (fieldIndex < 0) {
        _setLastError(tr("Unable to delete field. Unknown field id: %1").arg(fieldId));
        return;
    }

    QString errorString;

    QList<int> linkedJobIndexes;
    linkedJobIndexes.reserve(_jobRecords.size());
    for (int i = 0; i < _jobRecords.size(); ++i) {
        if (_jobRecords[i].fieldId == fieldId) {
            linkedJobIndexes.append(i);
        }
    }

    for (int i = linkedJobIndexes.size() - 1; i >= 0; --i) {
        const JobRecord& job = _jobRecords[linkedJobIndexes[i]];
        if (!_removeFileIfExists(job.planFilePath, errorString)) {
            _setLastError(errorString);
            return;
        }
        if (!_removeFileIfExists(job.thumbnailFilePath, errorString)) {
            _setLastError(errorString);
            return;
        }
        _jobRecords.removeAt(linkedJobIndexes[i]);
    }

    const FieldRecord field = _fieldRecords[fieldIndex];
    if (!_removeFileIfExists(field.thumbnailFilePath, errorString)) {
        _setLastError(errorString);
        return;
    }

    const QString fieldDirPath = _fieldDirectoryPath(field.id);
    QDir fieldDir(fieldDirPath);
    if (fieldDir.exists() && !fieldDir.removeRecursively()) {
        _setLastError(tr("Failed to remove field directory: %1").arg(fieldDirPath));
        return;
    }

    _fieldRecords.removeAt(fieldIndex);

    if (_activeFieldId == fieldId) {
        clearContexts();
    } else if (_activeJobDraftValid && _activeJobDraft.fieldId == fieldId) {
        _activeJobDraftValid = false;
        _setActiveJobId(QString());
        _setJobContextActive(false);
    }

    if (_jobsModelsByFieldId.contains(fieldId)) {
        if (_jobsModelsByFieldId[fieldId]) {
            _jobsModelsByFieldId[fieldId]->deleteLater();
        }
        _jobsModelsByFieldId.remove(fieldId);
    }

    _recomputeFieldJobCounts();
    _sortRecords();

    if (!_saveFieldsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!_saveJobsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }

    _refreshFieldsModel();
    _refreshJobsModels();
    _clearLastError();
}

QmlObjectListModel* FieldJobCatalogManager::jobsForField(const QString& fieldId)
{
    if (fieldId.isEmpty()) {
        return nullptr;
    }

    if (!_jobsModelsByFieldId.contains(fieldId) || _jobsModelsByFieldId[fieldId].isNull()) {
        _jobsModelsByFieldId[fieldId] = new QmlObjectListModel(this);
    }

    _refreshJobsModelForField(fieldId);
    return _jobsModelsByFieldId[fieldId];
}

void FieldJobCatalogManager::beginCreateJob(const QString& fieldId)
{
    const int fieldIndex = _findFieldRecordIndex(fieldId);
    if (fieldIndex < 0) {
        _setLastError(tr("Unable to create job. Unknown field id: %1").arg(fieldId));
        return;
    }

    _activeJobDraft = {};
    _activeJobDraft.id = newUuidString();
    _activeJobDraft.fieldId = fieldId;
    _activeJobDraft.planFilePath = _jobPlanFilePath(_activeJobDraft.id);
    _activeJobDraft.thumbnailFilePath = _jobThumbnailFilePath(_activeJobDraft.id);
    _activeJobDraft.createdAtUtc = _utcNowString();
    _activeJobDraft.updatedAtUtc = _activeJobDraft.createdAtUtc;
    _activeJobDraft.schemaVersion = kCatalogVersion;
    _activeJobDraft.revision = 0;
    _activeJobDraft.syncPending = false;
    _activeJobDraft.fieldPolygonPath = _fieldRecords[fieldIndex].polygonPath;
    _activeJobDraft.fieldExclusionPolygonPaths = _fieldRecords[fieldIndex].exclusionPolygonPaths;
    _activeJobDraft.fieldRevisionRequired = _fieldRecords[fieldIndex].revision;
    _activeJobDraft.needsRegeneration = false;

    _activeJobCreateMode = true;
    _activeJobDraftValid = true;
    _setActiveFieldId(fieldId);
    _setActiveJobId(_activeJobDraft.id);
    _setJobContextActive(true);
}

void FieldJobCatalogManager::beginEditJob(const QString& jobId)
{
    const int jobIndex = _findJobRecordIndex(jobId);
    if (jobIndex < 0) {
        _setLastError(tr("Unable to edit job. Unknown job id: %1").arg(jobId));
        return;
    }

    _activeJobDraft = _jobRecords[jobIndex];
    _activeJobCreateMode = false;
    _activeJobDraftValid = true;
    _setActiveFieldId(_activeJobDraft.fieldId);
    _setActiveJobId(jobId);
    _setJobContextActive(true);
}

QString FieldJobCatalogManager::activeJobPlanFilePath()
{
    return _activeJobDraftValid ? _activeJobDraft.planFilePath : QString();
}

QString FieldJobCatalogManager::activeJobThumbnailFilePath()
{
    return _activeJobDraftValid ? _activeJobDraft.thumbnailFilePath : QString();
}

QVariantList FieldJobCatalogManager::activeJobFieldPolygonPath()
{
    return _activeJobDraftValid ? _activeJobDraft.fieldPolygonPath : QVariantList();
}

QVariantList FieldJobCatalogManager::activeJobFieldExclusionPolygonPaths()
{
    return _activeJobDraftValid ? _activeJobDraft.fieldExclusionPolygonPaths : QVariantList();
}

void FieldJobCatalogManager::setActiveJobSprayMetrics(double pesticideLitersPerDekar, double dropletSizeMicron)
{
    if (!_activeJobDraftValid) {
        _setLastError(tr("No active job context for spray settings update."));
        return;
    }

    _activeJobDraft.pesticideLitersPerDekar = pesticideLitersPerDekar;
    _activeJobDraft.dropletSizeMicron = dropletSizeMicron;
    _clearLastError();
}

void FieldJobCatalogManager::commitActiveJob(const QString& name, const QString& notes)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        _setLastError(tr("Job name is required."));
        return;
    }

    if (!_activeJobDraftValid) {
        _setLastError(tr("No active job to commit."));
        return;
    }

    const int fieldIndex = _findFieldRecordIndex(_activeJobDraft.fieldId);
    if (fieldIndex < 0) {
        _setLastError(tr("Unable to commit job. Linked field no longer exists: %1").arg(_activeJobDraft.fieldId));
        return;
    }

    JobRecord updatedJob = _activeJobDraft;
    updatedJob.name = trimmedName;
    updatedJob.notes = notes.trimmed();
    updatedJob.updatedAtUtc = _utcNowString();
    if (_activeJobCreateMode && updatedJob.createdAtUtc.isEmpty()) {
        updatedJob.createdAtUtc = updatedJob.updatedAtUtc;
    }
    updatedJob.schemaVersion = kCatalogVersion;
    updatedJob.revision += 1;
    updatedJob.fieldPolygonPath = _fieldRecords[fieldIndex].polygonPath;
    updatedJob.fieldExclusionPolygonPaths = _fieldRecords[fieldIndex].exclusionPolygonPaths;
    updatedJob.fieldRevisionRequired = _fieldRecords[fieldIndex].revision;
    updatedJob.needsRegeneration = false;

    if (updatedJob.planFilePath.isEmpty()) {
        updatedJob.planFilePath = _jobPlanFilePath(updatedJob.id);
    }
    if (updatedJob.thumbnailFilePath.isEmpty()) {
        updatedJob.thumbnailFilePath = _jobThumbnailFilePath(updatedJob.id);
    }

    QString errorString;
    if (!_ensureCatalogDirectories(errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!_writeDefaultJobPlanFile(updatedJob.planFilePath, errorString)) {
        _setLastError(errorString);
        return;
    }

    int jobIndex = _findJobRecordIndex(updatedJob.id);
    if (jobIndex < 0) {
        _jobRecords.append(updatedJob);
    } else {
        _jobRecords[jobIndex] = updatedJob;
    }

    _activeJobDraft = updatedJob;
    _activeJobCreateMode = false;
    _activeJobDraftValid = true;

    _recomputeFieldJobCounts();
    _sortRecords();

    if (!_saveFieldsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!_saveJobsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }

    _refreshFieldsModel();
    _refreshJobsModels();
    _clearLastError();
}

void FieldJobCatalogManager::deleteJob(const QString& jobId)
{
    const int jobIndex = _findJobRecordIndex(jobId);
    if (jobIndex < 0) {
        _setLastError(tr("Unable to delete job. Unknown job id: %1").arg(jobId));
        return;
    }

    QString errorString;
    const JobRecord job = _jobRecords[jobIndex];

    if (!_removeFileIfExists(job.planFilePath, errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!_removeFileIfExists(job.thumbnailFilePath, errorString)) {
        _setLastError(errorString);
        return;
    }

    _jobRecords.removeAt(jobIndex);

    if (_activeJobId == jobId) {
        _activeJobDraftValid = false;
        _setActiveJobId(QString());
        _setJobContextActive(false);
    }

    _recomputeFieldJobCounts();
    _sortRecords();

    if (!_saveFieldsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }
    if (!_saveJobsCatalog(errorString)) {
        _setLastError(errorString);
        return;
    }

    _refreshFieldsModel();
    _refreshJobsModels();
    _clearLastError();
}

void FieldJobCatalogManager::startJob(const QString& jobId)
{
    JobRecord job;
    QString errorString;
    if (!_validateUploadRequest(jobId, false /* requireMissionIncomplete */, false /* missionIncomplete */, job, errorString)) {
        _setLastError(errorString);
        emit jobUploadFailed(jobId, errorString);
        return;
    }

    if (!_startJobUpload(job, errorString)) {
        _setLastError(errorString);
        emit jobUploadFailed(jobId, errorString);
        return;
    }

    _clearLastError();
}

void FieldJobCatalogManager::resumeJob(const QString& jobId, bool missionIncomplete)
{
    JobRecord job;
    QString errorString;
    if (!_validateUploadRequest(jobId, true /* requireMissionIncomplete */, missionIncomplete, job, errorString)) {
        _setLastError(errorString);
        emit jobUploadFailed(jobId, errorString);
        return;
    }

    if (!_startJobUpload(job, errorString)) {
        _setLastError(errorString);
        emit jobUploadFailed(jobId, errorString);
        return;
    }

    _clearLastError();
}

bool FieldJobCatalogManager::_validateUploadRequest(const QString& jobId, bool requireMissionIncomplete, bool missionIncomplete,
                                                    JobRecord& outJob, QString& errorString) const
{
    if (_uploadInProgress) {
        errorString = tr("Another job upload is already in progress.");
        return false;
    }

    const int jobIndex = _findJobRecordIndex(jobId);
    if (jobIndex < 0) {
        errorString = tr("Unknown job id: %1").arg(jobId);
        return false;
    }

    const JobRecord& job = _jobRecords[jobIndex];
    if (job.needsRegeneration) {
        errorString = tr("Job %1 must be regenerated before upload.").arg(job.name);
        return false;
    }

    if (requireMissionIncomplete && !missionIncomplete) {
        errorString = tr("Resume requires an incomplete mission state.");
        return false;
    }

    if (!QFileInfo::exists(job.planFilePath)) {
        errorString = tr("Job plan file is missing: %1").arg(job.planFilePath);
        return false;
    }

    Vehicle* const activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!activeVehicle) {
        errorString = tr("No active vehicle connected.");
        return false;
    }

    if (!activeVehicle->missionManager()) {
        errorString = tr("Active vehicle mission manager is unavailable.");
        return false;
    }

    outJob = job;
    return true;
}

bool FieldJobCatalogManager::_startJobUpload(const JobRecord& job, QString& errorString)
{
    Vehicle* const activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!activeVehicle || !activeVehicle->missionManager()) {
        errorString = tr("No active vehicle mission upload path is available.");
        return false;
    }

    if (!_syncJobPlanGeoFenceFromCatalog(job, errorString)) {
        return false;
    }

    _clearUploadState();

    MissionManager* const missionManager = activeVehicle->missionManager();
    _uploadVehicle = activeVehicle;
    _uploadJobId = job.id;
    _uploadErrorText.clear();

    _uploadInProgressConnection = connect(missionManager, &MissionManager::inProgressChanged, this, [this](bool inProgress) {
        if (_uploadJobId.isEmpty()) {
            return;
        }
        _setUploadInProgress(inProgress);
    });

    _uploadErrorConnection = connect(missionManager, &MissionManager::error, this, [this](int, const QString& errorMsg) {
        if (_uploadJobId.isEmpty()) {
            return;
        }
        _uploadErrorText = errorMsg;
    });

    _uploadSendCompleteConnection = connect(missionManager, &MissionManager::sendComplete, this, [this](bool error) {
        const QString completedJobId = _uploadJobId;
        const QString uploadErrorText = _uploadErrorText;
        _clearUploadState();
        if (error) {
            const QString errorText = uploadErrorText.isEmpty()
                    ? tr("Job upload failed due to a mission transfer error.")
                    : uploadErrorText;
            _setLastError(errorText);
            emit jobUploadFailed(completedJobId, errorText);
        } else {
            _clearLastError();
            emit jobUploadSucceeded(completedJobId);
        }
    });

    _setUploadInProgress(true);
    activeVehicle->sendPlan(job.planFilePath);

    QTimer::singleShot(0, this, [this, missionManager]() {
        if (_uploadJobId.isEmpty() || missionManager->inProgress()) {
            return;
        }

        QString startError = _uploadErrorText;
        if (startError.isEmpty()) {
            SharedLinkInterfacePtr sharedLink;
            if (_uploadVehicle) {
                sharedLink = _uploadVehicle->vehicleLinkManager()->primaryLink().lock();
            }
            if (!sharedLink) {
                startError = tr("Unable to start job upload. No active telemetry link is available.");
            } else if (sharedLink->linkConfiguration() && sharedLink->linkConfiguration()->isHighLatency()) {
                startError = tr("Upload not supported on high latency links.");
            } else {
                startError = tr("Unable to start job upload. Vehicle rejected mission transfer request.");
            }
        }

        const QString failedJobId = _uploadJobId;
        _clearUploadState();
        _setLastError(startError);
        emit jobUploadFailed(failedJobId, startError);
    });

    return true;
}

void FieldJobCatalogManager::_clearUploadState()
{
    if (_uploadSendCompleteConnection) {
        disconnect(_uploadSendCompleteConnection);
    }
    if (_uploadInProgressConnection) {
        disconnect(_uploadInProgressConnection);
    }
    if (_uploadErrorConnection) {
        disconnect(_uploadErrorConnection);
    }

    _uploadSendCompleteConnection = {};
    _uploadInProgressConnection = {};
    _uploadErrorConnection = {};

    _uploadVehicle = nullptr;
    _uploadJobId.clear();
    _uploadErrorText.clear();
    _setUploadInProgress(false);
}

void FieldJobCatalogManager::clearContexts()
{
    _activeFieldDraft = {};
    _activeFieldDraftValid = false;
    _activeFieldCreateMode = false;

    _activeJobDraft = {};
    _activeJobDraftValid = false;
    _activeJobCreateMode = false;

    _setActiveFieldId(QString());
    _setActiveJobId(QString());
    _setFieldContextActive(false);
    _setJobContextActive(false);
}

QString FieldJobCatalogManager::_catalogRootPath() const
{
    AppSettings* appSettings = SettingsManager::instance()->appSettings();
    if (!appSettings) {
        return QString();
    }

    const QString missionSavePath = appSettings->missionSavePath();
    if (missionSavePath.isEmpty()) {
        return QString();
    }

    return QGCFileHelper::joinPath(missionSavePath, kCatalogDirectoryName);
}

QString FieldJobCatalogManager::_fieldsCatalogFilePath() const
{
    return QGCFileHelper::joinPath(_catalogRootPath(), QString::fromUtf8(kFieldsCatalogFileName));
}

QString FieldJobCatalogManager::_jobsCatalogFilePath() const
{
    return QGCFileHelper::joinPath(_catalogRootPath(), QString::fromUtf8(kJobsCatalogFileName));
}

QString FieldJobCatalogManager::_fieldsDirectoryPath() const
{
    return QGCFileHelper::joinPath(_catalogRootPath(), kFieldsDirectoryName);
}

QString FieldJobCatalogManager::_jobsDirectoryPath() const
{
    return QGCFileHelper::joinPath(_catalogRootPath(), kJobsDirectoryName);
}

QString FieldJobCatalogManager::_fieldDirectoryPath(const QString& fieldId) const
{
    return QGCFileHelper::joinPath(_fieldsDirectoryPath(), fieldId);
}

QString FieldJobCatalogManager::_fieldThumbnailFilePath(const QString& fieldId) const
{
    return QGCFileHelper::joinPath(_fieldDirectoryPath(fieldId), QStringLiteral("field.png"));
}

QString FieldJobCatalogManager::_jobPlanFilePath(const QString& jobId) const
{
    return QGCFileHelper::joinPath(_jobsDirectoryPath(), QStringLiteral("%1.plan").arg(jobId));
}

QString FieldJobCatalogManager::_jobThumbnailFilePath(const QString& jobId) const
{
    return QGCFileHelper::joinPath(_jobsDirectoryPath(), QStringLiteral("%1.png").arg(jobId));
}

bool FieldJobCatalogManager::_ensureCatalogDirectories(QString& errorString) const
{
    const QString rootPath = _catalogRootPath();
    if (rootPath.isEmpty()) {
        errorString = tr("Mission save path is not available.");
        return false;
    }

    if (!QGCFileHelper::ensureDirectoryExists(rootPath)) {
        errorString = tr("Failed to create catalog directory: %1").arg(rootPath);
        return false;
    }
    if (!QGCFileHelper::ensureDirectoryExists(_fieldsDirectoryPath())) {
        errorString = tr("Failed to create fields directory: %1").arg(_fieldsDirectoryPath());
        return false;
    }
    if (!QGCFileHelper::ensureDirectoryExists(_jobsDirectoryPath())) {
        errorString = tr("Failed to create jobs directory: %1").arg(_jobsDirectoryPath());
        return false;
    }

    return true;
}

bool FieldJobCatalogManager::_saveFieldsCatalog(QString& errorString) const
{
    QJsonObject rootJson;
    JsonHelper::saveQGCJsonFileHeader(rootJson, QString::fromUtf8(kFieldsCatalogFileType), kCatalogVersion);
    qCWarning(FieldJobCatalogManagerLog) << "_saveFieldsCatalog begin recordCount:" << _fieldRecords.size();

    QJsonArray fieldsArray;
    for (const FieldRecord& record : _fieldRecords) {
        qCWarning(FieldJobCatalogManagerLog) << "_saveFieldsCatalog serializing field id:" << record.id
                                             << "name:" << record.name
                                             << "exclusionCount:" << record.exclusionPolygonPaths.size();
        QJsonObject fieldObject;
        if (!_fieldRecordToJson(record, fieldObject, errorString)) {
            return false;
        }
        fieldsArray.append(fieldObject);
    }
    rootJson[QString::fromUtf8(kFieldsArrayKey)] = fieldsArray;

    const QByteArray bytes = QJsonDocument(rootJson).toJson(QJsonDocument::Indented);
    const QString filePath = _fieldsCatalogFilePath();
    if (!QGCFileHelper::atomicWrite(filePath, bytes)) {
        errorString = tr("Failed to save fields catalog: %1").arg(filePath);
        return false;
    }
    qCWarning(FieldJobCatalogManagerLog) << "_saveFieldsCatalog wrote file:" << filePath
                                         << "jsonBytes:" << bytes.size();

    return true;
}

bool FieldJobCatalogManager::_saveJobsCatalog(QString& errorString) const
{
    QJsonObject rootJson;
    JsonHelper::saveQGCJsonFileHeader(rootJson, QString::fromUtf8(kJobsCatalogFileType), kCatalogVersion);

    QJsonArray jobsArray;
    for (const JobRecord& record : _jobRecords) {
        QJsonObject jobObject;
        if (!_jobRecordToJson(record, jobObject, errorString)) {
            return false;
        }
        jobsArray.append(jobObject);
    }
    rootJson[QString::fromUtf8(kJobsArrayKey)] = jobsArray;

    const QByteArray bytes = QJsonDocument(rootJson).toJson(QJsonDocument::Indented);
    const QString filePath = _jobsCatalogFilePath();
    if (!QGCFileHelper::atomicWrite(filePath, bytes)) {
        errorString = tr("Failed to save jobs catalog: %1").arg(filePath);
        return false;
    }

    return true;
}

bool FieldJobCatalogManager::_loadFieldsCatalog(QString& errorString)
{
    const QString filePath = _fieldsCatalogFilePath();
    if (!QFileInfo::exists(filePath)) {
        return true;
    }

    QJsonDocument jsonDoc;
    if (!JsonParsing::isJsonFile(filePath, jsonDoc, errorString)) {
        errorString = tr("Fields catalog is corrupt: %1 (%2)").arg(filePath, errorString);
        return false;
    }
    if (!jsonDoc.isObject()) {
        errorString = tr("Fields catalog root is not an object: %1").arg(filePath);
        return false;
    }

    QJsonObject rootJson = jsonDoc.object();
    int version = 0;
    if (!JsonHelper::validateExternalQGCJsonFile(rootJson, QString::fromUtf8(kFieldsCatalogFileType), kCatalogVersion, kCatalogVersion, version, errorString)) {
        errorString = tr("Fields catalog validation failed: %1").arg(errorString);
        return false;
    }

    const QList<JsonHelper::KeyValidateInfo> keyInfo = {
        { kFieldsArrayKey, QJsonValue::Array, false },
    };
    if (!JsonHelper::validateKeys(rootJson, keyInfo, errorString)) {
        errorString = tr("Fields catalog format error: %1").arg(errorString);
        return false;
    }

    const QJsonArray fieldsArray = rootJson.value(QString::fromUtf8(kFieldsArrayKey)).toArray();
    _fieldRecords.clear();
    _fieldRecords.reserve(fieldsArray.size());

    for (const QJsonValue& value : fieldsArray) {
        if (!value.isObject()) {
            errorString = tr("Fields catalog contains a non-object entry.");
            _fieldRecords.clear();
            return false;
        }

        FieldRecord record;
        if (!_fieldRecordFromJson(value.toObject(), record, errorString)) {
            errorString = tr("Fields catalog entry parse error: %1").arg(errorString);
            _fieldRecords.clear();
            return false;
        }
        _fieldRecords.append(record);
    }

    return true;
}

bool FieldJobCatalogManager::_loadJobsCatalog(QString& errorString)
{
    const QString filePath = _jobsCatalogFilePath();
    if (!QFileInfo::exists(filePath)) {
        return true;
    }

    QJsonDocument jsonDoc;
    if (!JsonParsing::isJsonFile(filePath, jsonDoc, errorString)) {
        errorString = tr("Jobs catalog is corrupt: %1 (%2)").arg(filePath, errorString);
        return false;
    }
    if (!jsonDoc.isObject()) {
        errorString = tr("Jobs catalog root is not an object: %1").arg(filePath);
        return false;
    }

    QJsonObject rootJson = jsonDoc.object();
    int version = 0;
    if (!JsonHelper::validateExternalQGCJsonFile(rootJson, QString::fromUtf8(kJobsCatalogFileType), kCatalogVersion, kCatalogVersion, version, errorString)) {
        errorString = tr("Jobs catalog validation failed: %1").arg(errorString);
        return false;
    }

    const QList<JsonHelper::KeyValidateInfo> keyInfo = {
        { kJobsArrayKey, QJsonValue::Array, false },
    };
    if (!JsonHelper::validateKeys(rootJson, keyInfo, errorString)) {
        errorString = tr("Jobs catalog format error: %1").arg(errorString);
        return false;
    }

    const QJsonArray jobsArray = rootJson.value(QString::fromUtf8(kJobsArrayKey)).toArray();
    _jobRecords.clear();
    _jobRecords.reserve(jobsArray.size());

    for (const QJsonValue& value : jobsArray) {
        if (!value.isObject()) {
            errorString = tr("Jobs catalog contains a non-object entry.");
            _jobRecords.clear();
            return false;
        }

        JobRecord record;
        if (!_jobRecordFromJson(value.toObject(), record, errorString)) {
            errorString = tr("Jobs catalog entry parse error: %1").arg(errorString);
            _jobRecords.clear();
            return false;
        }
        _jobRecords.append(record);
    }

    return true;
}

void FieldJobCatalogManager::_refreshFieldsModel()
{
    _fields->clearAndDeleteContents();

    for (const FieldRecord& record : _fieldRecords) {
        FieldCatalogEntry* const entry = new FieldCatalogEntry(_fields);
        entry->_id = record.id;
        entry->_name = record.name;
        entry->_thumbnailFilePath = record.thumbnailFilePath;
        entry->_polygonPath = record.polygonPath;
        entry->_exclusionPolygonPaths = record.exclusionPolygonPaths;
        entry->_jobCount = record.jobCount;
        entry->_createdAtUtc = record.createdAtUtc;
        entry->_updatedAtUtc = record.updatedAtUtc;
        entry->_schemaVersion = record.schemaVersion;
        entry->_revision = record.revision;
        entry->_syncPending = record.syncPending;
        _fields->append(entry);
    }

    emit fieldsChanged();
}

void FieldJobCatalogManager::_refreshJobsModels()
{
    const QList<QString> modelKeys = _jobsModelsByFieldId.keys();
    for (const QString& fieldId : modelKeys) {
        if (_findFieldRecordIndex(fieldId) < 0) {
            if (_jobsModelsByFieldId[fieldId]) {
                _jobsModelsByFieldId[fieldId]->deleteLater();
            }
            _jobsModelsByFieldId.remove(fieldId);
        }
    }

    for (const QString& fieldId : _jobsModelsByFieldId.keys()) {
        _refreshJobsModelForField(fieldId);
    }
}

void FieldJobCatalogManager::_refreshJobsModelForField(const QString& fieldId)
{
    if (!_jobsModelsByFieldId.contains(fieldId) || _jobsModelsByFieldId[fieldId].isNull()) {
        return;
    }

    QmlObjectListModel* const model = _jobsModelsByFieldId[fieldId];
    model->clearAndDeleteContents();

    for (const JobRecord& record : _jobRecords) {
        if (record.fieldId != fieldId) {
            continue;
        }

        JobCatalogEntry* const entry = new JobCatalogEntry(model);
        entry->_id = record.id;
        entry->_fieldId = record.fieldId;
        entry->_name = record.name;
        entry->_planFilePath = record.planFilePath;
        entry->_thumbnailFilePath = record.thumbnailFilePath;
        entry->_pesticideLitersPerDekar = record.pesticideLitersPerDekar;
        entry->_dropletSizeMicron = record.dropletSizeMicron;
        entry->_nozzleValuePct = record.nozzleValuePct;
        entry->_boomWidthM = record.boomWidthM;
        entry->_notes = record.notes;
        entry->_needsRegeneration = record.needsRegeneration;
        entry->_fieldRevisionRequired = record.fieldRevisionRequired;
        entry->_createdAtUtc = record.createdAtUtc;
        entry->_updatedAtUtc = record.updatedAtUtc;
        entry->_schemaVersion = record.schemaVersion;
        entry->_revision = record.revision;
        entry->_syncPending = record.syncPending;
        model->append(entry);
    }
}

void FieldJobCatalogManager::_sortRecords()
{
    std::sort(_fieldRecords.begin(), _fieldRecords.end(), [](const FieldRecord& lhs, const FieldRecord& rhs) {
        return parseUtcTime(lhs.updatedAtUtc) > parseUtcTime(rhs.updatedAtUtc);
    });

    std::sort(_jobRecords.begin(), _jobRecords.end(), [](const JobRecord& lhs, const JobRecord& rhs) {
        return parseUtcTime(lhs.updatedAtUtc) > parseUtcTime(rhs.updatedAtUtc);
    });
}

void FieldJobCatalogManager::_recomputeFieldJobCounts()
{
    QHash<QString, int> countByFieldId;
    for (const JobRecord& job : _jobRecords) {
        countByFieldId[job.fieldId] = countByFieldId.value(job.fieldId, 0) + 1;
    }

    for (FieldRecord& field : _fieldRecords) {
        field.jobCount = countByFieldId.value(field.id, 0);
    }
}

int FieldJobCatalogManager::_findFieldRecordIndex(const QString& fieldId) const
{
    for (int i = 0; i < _fieldRecords.size(); ++i) {
        if (_fieldRecords[i].id == fieldId) {
            return i;
        }
    }
    return -1;
}

int FieldJobCatalogManager::_findJobRecordIndex(const QString& jobId) const
{
    for (int i = 0; i < _jobRecords.size(); ++i) {
        if (_jobRecords[i].id == jobId) {
            return i;
        }
    }
    return -1;
}

int FieldJobCatalogManager::_findFieldRevision(const QString& fieldId) const
{
    const int fieldIndex = _findFieldRecordIndex(fieldId);
    return fieldIndex >= 0 ? _fieldRecords[fieldIndex].revision : 0;
}

bool FieldJobCatalogManager::_removeFileIfExists(const QString& filePath, QString& errorString) const
{
    if (filePath.isEmpty()) {
        return true;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return true;
    }

    if (!QFile::remove(filePath)) {
        errorString = tr("Failed to remove file: %1").arg(filePath);
        return false;
    }

    return true;
}

bool FieldJobCatalogManager::_writeDefaultJobPlanFile(const QString& filePath, QString& errorString) const
{
    if (filePath.isEmpty()) {
        errorString = tr("Cannot create job plan file: path is empty.");
        return false;
    }

    if (QFileInfo::exists(filePath)) {
        return true;
    }

    QJsonObject planJson;
    JsonHelper::saveQGCJsonFileHeader(planJson, QStringLiteral("Plan"), 1);
    planJson[QStringLiteral("mission")] = QJsonObject();
    planJson[QStringLiteral("geoFence")] = QJsonObject();
    planJson[QStringLiteral("rallyPoints")] = QJsonObject();

    const QByteArray bytes = QJsonDocument(planJson).toJson(QJsonDocument::Indented);
    if (!QGCFileHelper::atomicWrite(filePath, bytes)) {
        errorString = tr("Failed to create job plan file: %1").arg(filePath);
        return false;
    }

    return true;
}

bool FieldJobCatalogManager::_syncJobPlanGeoFenceFromCatalog(const JobRecord& job, QString& errorString) const
{
    if (job.planFilePath.isEmpty()) {
        errorString = tr("Job plan file path is empty.");
        return false;
    }

    QJsonDocument planJsonDoc;
    if (!JsonParsing::isJsonFile(job.planFilePath, planJsonDoc, errorString)) {
        errorString = tr("Job plan file is invalid JSON: %1 (%2)").arg(job.planFilePath, errorString);
        return false;
    }
    if (!planJsonDoc.isObject()) {
        errorString = tr("Job plan file root is not an object: %1").arg(job.planFilePath);
        return false;
    }

    QJsonObject planJson = planJsonDoc.object();
    QJsonObject geoFenceJson = planJson.value(QStringLiteral("geoFence")).toObject();
    geoFenceJson[JsonHelper::jsonVersionKey] = 2;
    if (!geoFenceJson.value(QStringLiteral("circles")).isArray()) {
        geoFenceJson[QStringLiteral("circles")] = QJsonArray();
    }

    QJsonArray polygonsJson;
    const QVariantList normalizedExclusionPolygonPaths = normalizedPolygonPaths(job.fieldExclusionPolygonPaths);
    for (const QVariant& polygonPathVar : normalizedExclusionPolygonPaths) {
        const QVariantList polygonPath = polygonPathVar.toList();
        if (!isValidPolygonPath(polygonPath)) {
            continue;
        }

        QJsonObject polygonJson;
        polygonJson[JsonHelper::jsonVersionKey] = 1;
        polygonJson[QStringLiteral("inclusion")] = false;

        QJsonValue polygonPathJson;
        JsonHelper::saveGeoCoordinateArray(polygonPath, false, polygonPathJson);
        polygonJson[QString::fromUtf8(QGCMapPolygon::jsonPolygonKey)] = polygonPathJson;

        polygonsJson.append(polygonJson);
    }
    geoFenceJson[QStringLiteral("polygons")] = polygonsJson;
    planJson[QStringLiteral("geoFence")] = geoFenceJson;

    const QByteArray planBytes = QJsonDocument(planJson).toJson(QJsonDocument::Indented);
    if (!QGCFileHelper::atomicWrite(job.planFilePath, planBytes)) {
        errorString = tr("Failed to update job plan geofence: %1").arg(job.planFilePath);
        return false;
    }

    return true;
}

void FieldJobCatalogManager::_setActiveFieldId(const QString& fieldId)
{
    if (_activeFieldId != fieldId) {
        _activeFieldId = fieldId;
        emit activeFieldIdChanged();
    }
}

void FieldJobCatalogManager::_setActiveJobId(const QString& jobId)
{
    if (_activeJobId != jobId) {
        _activeJobId = jobId;
        emit activeJobIdChanged();
    }
}

void FieldJobCatalogManager::_setFieldContextActive(bool active)
{
    if (_fieldContextActive != active) {
        _fieldContextActive = active;
        emit fieldContextActiveChanged();
    }
}

void FieldJobCatalogManager::_setJobContextActive(bool active)
{
    if (_jobContextActive != active) {
        _jobContextActive = active;
        emit jobContextActiveChanged();
    }
}

void FieldJobCatalogManager::_setUploadInProgress(bool inProgress)
{
    if (_uploadInProgress != inProgress) {
        _uploadInProgress = inProgress;
        emit uploadInProgressChanged();
    }
}

void FieldJobCatalogManager::_setLastError(const QString& error)
{
    if (_lastError != error) {
        _lastError = error;
        emit lastErrorChanged();
    }
}

void FieldJobCatalogManager::_clearLastError()
{
    _setLastError(QString());
}

QString FieldJobCatalogManager::_utcNowString()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool FieldJobCatalogManager::_fieldRecordToJson(const FieldRecord& record, QJsonObject& json, QString& errorString)
{
    if (record.id.isEmpty()) {
        errorString = QObject::tr("Field record has empty id.");
        return false;
    }
    if (record.name.isEmpty()) {
        errorString = QObject::tr("Field record has empty name.");
        return false;
    }

    json[QString::fromUtf8(kIdKey)] = record.id;
    json[QString::fromUtf8(kNameKey)] = record.name;
    json[QString::fromUtf8(kThumbnailFilePathKey)] = record.thumbnailFilePath;
    qCWarning(FieldJobCatalogManagerLog) << "_fieldRecordToJson field id:" << record.id
                                         << "name:" << record.name
                                         << "mainVertexCount:" << record.polygonPath.size()
                                         << "exclusionCount:" << record.exclusionPolygonPaths.size();

    QJsonValue polygonValue;
    JsonHelper::saveGeoCoordinateArray(record.polygonPath, false, polygonValue);
    json[QString::fromUtf8(kPolygonPathKey)] = polygonValue;

    QJsonArray exclusionPolygonsArray;
    for (int polygonIndex = 0; polygonIndex < record.exclusionPolygonPaths.size(); ++polygonIndex) {
        const QVariant& polygonPathVar = record.exclusionPolygonPaths[polygonIndex];
        const QVariantList polygonPath = polygonPathVar.toList();
        if (polygonPath.isEmpty()) {
            qCWarning(FieldJobCatalogManagerLog) << "_fieldRecordToJson skipping empty exclusion polygon index:" << polygonIndex;
            continue;
        }
        qCWarning(FieldJobCatalogManagerLog) << "_fieldRecordToJson exclusion polygon index:" << polygonIndex
                                             << "vertexCount:" << polygonPath.size()
                                             << "firstVertexSummary:" << variantSummary(polygonPath.first());

        QJsonValue exclusionPolygonValue;
        JsonHelper::saveGeoCoordinateArray(polygonPath, false, exclusionPolygonValue);
        exclusionPolygonsArray.append(exclusionPolygonValue);
    }
    json[QString::fromUtf8(kExclusionPolygonPathsKey)] = exclusionPolygonsArray;

    json[QString::fromUtf8(kJobCountKey)] = record.jobCount;
    json[QString::fromUtf8(kCreatedAtUtcKey)] = record.createdAtUtc;
    json[QString::fromUtf8(kUpdatedAtUtcKey)] = record.updatedAtUtc;
    json[QString::fromUtf8(kSchemaVersionKey)] = record.schemaVersion;
    json[QString::fromUtf8(kRevisionKey)] = record.revision;
    json[QString::fromUtf8(kSyncPendingKey)] = record.syncPending;

    return true;
}

bool FieldJobCatalogManager::_jobRecordToJson(const JobRecord& record, QJsonObject& json, QString& errorString)
{
    if (record.id.isEmpty()) {
        errorString = QObject::tr("Job record has empty id.");
        return false;
    }
    if (record.fieldId.isEmpty()) {
        errorString = QObject::tr("Job record has empty fieldId.");
        return false;
    }
    if (record.name.isEmpty()) {
        errorString = QObject::tr("Job record has empty name.");
        return false;
    }

    json[QString::fromUtf8(kIdKey)] = record.id;
    json[QString::fromUtf8(kFieldIdKey)] = record.fieldId;
    json[QString::fromUtf8(kNameKey)] = record.name;
    json[QString::fromUtf8(kPlanFilePathKey)] = record.planFilePath;
    json[QString::fromUtf8(kThumbnailFilePathKey)] = record.thumbnailFilePath;
    json[QString::fromUtf8(kPesticideLitersPerDekarKey)] = record.pesticideLitersPerDekar;
    json[QString::fromUtf8(kDropletSizeMicronKey)] = record.dropletSizeMicron;
    json[QString::fromUtf8(kNozzleValuePctKey)] = record.nozzleValuePct;
    json[QString::fromUtf8(kBoomWidthMKey)] = record.boomWidthM;
    json[QString::fromUtf8(kNotesKey)] = record.notes;
    json[QString::fromUtf8(kNeedsRegenerationKey)] = record.needsRegeneration;
    json[QString::fromUtf8(kFieldRevisionRequiredKey)] = record.fieldRevisionRequired;

    QJsonValue fieldPolygonValue;
    JsonHelper::saveGeoCoordinateArray(record.fieldPolygonPath, false, fieldPolygonValue);
    json[QString::fromUtf8(kFieldPolygonPathKey)] = fieldPolygonValue;

    QJsonArray fieldExclusionPolygonsArray;
    for (const QVariant& polygonPathVar : record.fieldExclusionPolygonPaths) {
        const QVariantList polygonPath = polygonPathVar.toList();
        if (polygonPath.isEmpty()) {
            continue;
        }

        QJsonValue fieldExclusionPolygonValue;
        JsonHelper::saveGeoCoordinateArray(polygonPath, false, fieldExclusionPolygonValue);
        fieldExclusionPolygonsArray.append(fieldExclusionPolygonValue);
    }
    json[QString::fromUtf8(kFieldExclusionPolygonPathsKey)] = fieldExclusionPolygonsArray;

    json[QString::fromUtf8(kCreatedAtUtcKey)] = record.createdAtUtc;
    json[QString::fromUtf8(kUpdatedAtUtcKey)] = record.updatedAtUtc;
    json[QString::fromUtf8(kSchemaVersionKey)] = record.schemaVersion;
    json[QString::fromUtf8(kRevisionKey)] = record.revision;
    json[QString::fromUtf8(kSyncPendingKey)] = record.syncPending;

    return true;
}

bool FieldJobCatalogManager::_fieldRecordFromJson(const QJsonObject& json, FieldRecord& record, QString& errorString) const
{
    if (!json.value(QString::fromUtf8(kIdKey)).isString()) {
        errorString = tr("Field id is missing or invalid.");
        return false;
    }
    if (!json.value(QString::fromUtf8(kNameKey)).isString()) {
        errorString = tr("Field name is missing or invalid.");
        return false;
    }

    record.id = json.value(QString::fromUtf8(kIdKey)).toString();
    record.name = json.value(QString::fromUtf8(kNameKey)).toString();
    record.thumbnailFilePath = json.value(QString::fromUtf8(kThumbnailFilePathKey)).toString(_fieldThumbnailFilePath(record.id));

    if (json.contains(QString::fromUtf8(kPolygonPathKey))) {
        if (!JsonHelper::loadGeoCoordinateArray(json.value(QString::fromUtf8(kPolygonPathKey)), false, record.polygonPath, errorString)) {
            return false;
        }
    } else {
        record.polygonPath.clear();
    }

    record.exclusionPolygonPaths.clear();
    if (json.contains(QString::fromUtf8(kExclusionPolygonPathsKey))) {
        const QJsonArray exclusionPolygonsArray = json.value(QString::fromUtf8(kExclusionPolygonPathsKey)).toArray();
        qCWarning(FieldJobCatalogManagerLog) << "_fieldRecordFromJson field id:" << record.id
                                             << "rawExclusionArrayCount:" << exclusionPolygonsArray.size();
        for (const QJsonValue& exclusionPolygonValue : exclusionPolygonsArray) {
            QVariantList exclusionPolygonPath;
            if (!JsonHelper::loadGeoCoordinateArray(exclusionPolygonValue, false, exclusionPolygonPath, errorString)) {
                return false;
            }
            record.exclusionPolygonPaths.append(exclusionPolygonPath);
        }
    }
    qCWarning(FieldJobCatalogManagerLog) << "_fieldRecordFromJson loaded field id:" << record.id
                                         << "name:" << record.name
                                         << "mainVertexCount:" << record.polygonPath.size()
                                         << "loadedExclusionCount:" << record.exclusionPolygonPaths.size();

    record.jobCount = json.value(QString::fromUtf8(kJobCountKey)).toInt();
    record.createdAtUtc = json.value(QString::fromUtf8(kCreatedAtUtcKey)).toString(_utcNowString());
    record.updatedAtUtc = json.value(QString::fromUtf8(kUpdatedAtUtcKey)).toString(record.createdAtUtc);
    record.schemaVersion = json.value(QString::fromUtf8(kSchemaVersionKey)).toInt(kCatalogVersion);
    record.revision = json.value(QString::fromUtf8(kRevisionKey)).toInt(0);
    record.syncPending = json.value(QString::fromUtf8(kSyncPendingKey)).toBool(false);

    return true;
}

bool FieldJobCatalogManager::_jobRecordFromJson(const QJsonObject& json, JobRecord& record, QString& errorString) const
{
    if (!json.value(QString::fromUtf8(kIdKey)).isString()) {
        errorString = tr("Job id is missing or invalid.");
        return false;
    }
    if (!json.value(QString::fromUtf8(kFieldIdKey)).isString()) {
        errorString = tr("Job fieldId is missing or invalid.");
        return false;
    }
    if (!json.value(QString::fromUtf8(kNameKey)).isString()) {
        errorString = tr("Job name is missing or invalid.");
        return false;
    }

    record.id = json.value(QString::fromUtf8(kIdKey)).toString();
    record.fieldId = json.value(QString::fromUtf8(kFieldIdKey)).toString();
    record.name = json.value(QString::fromUtf8(kNameKey)).toString();
    record.planFilePath = json.value(QString::fromUtf8(kPlanFilePathKey)).toString(_jobPlanFilePath(record.id));
    record.thumbnailFilePath = json.value(QString::fromUtf8(kThumbnailFilePathKey)).toString(_jobThumbnailFilePath(record.id));
    record.pesticideLitersPerDekar = json.value(QString::fromUtf8(kPesticideLitersPerDekarKey)).toDouble(0.0);
    record.dropletSizeMicron = json.value(QString::fromUtf8(kDropletSizeMicronKey)).toDouble(0.0);
    record.nozzleValuePct = json.value(QString::fromUtf8(kNozzleValuePctKey)).toDouble(0.0);
    record.boomWidthM = json.value(QString::fromUtf8(kBoomWidthMKey)).toDouble(0.0);
    record.notes = json.value(QString::fromUtf8(kNotesKey)).toString();
    record.needsRegeneration = json.value(QString::fromUtf8(kNeedsRegenerationKey)).toBool(false);
    record.fieldRevisionRequired = json.value(QString::fromUtf8(kFieldRevisionRequiredKey)).toInt(_findFieldRevision(record.fieldId));

    if (json.contains(QString::fromUtf8(kFieldPolygonPathKey))) {
        if (!JsonHelper::loadGeoCoordinateArray(json.value(QString::fromUtf8(kFieldPolygonPathKey)), false, record.fieldPolygonPath, errorString)) {
            return false;
        }
    } else {
        record.fieldPolygonPath.clear();
    }

    record.fieldExclusionPolygonPaths.clear();
    if (json.contains(QString::fromUtf8(kFieldExclusionPolygonPathsKey))) {
        const QJsonArray fieldExclusionPolygonsArray = json.value(QString::fromUtf8(kFieldExclusionPolygonPathsKey)).toArray();
        for (const QJsonValue& fieldExclusionPolygonValue : fieldExclusionPolygonsArray) {
            QVariantList fieldExclusionPolygonPath;
            if (!JsonHelper::loadGeoCoordinateArray(fieldExclusionPolygonValue, false, fieldExclusionPolygonPath, errorString)) {
                return false;
            }
            record.fieldExclusionPolygonPaths.append(fieldExclusionPolygonPath);
        }
    } else {
        // Backward compatibility for older catalogs where no-go polygons were not stored separately.
        const int fieldIndex = _findFieldRecordIndex(record.fieldId);
        if (fieldIndex >= 0) {
            record.fieldExclusionPolygonPaths = _fieldRecords[fieldIndex].exclusionPolygonPaths;
        } else {
            record.fieldExclusionPolygonPaths.clear();
        }
    }

    record.createdAtUtc = json.value(QString::fromUtf8(kCreatedAtUtcKey)).toString(_utcNowString());
    record.updatedAtUtc = json.value(QString::fromUtf8(kUpdatedAtUtcKey)).toString(record.createdAtUtc);
    record.schemaVersion = json.value(QString::fromUtf8(kSchemaVersionKey)).toInt(kCatalogVersion);
    record.revision = json.value(QString::fromUtf8(kRevisionKey)).toInt(0);
    record.syncPending = json.value(QString::fromUtf8(kSyncPendingKey)).toBool(false);

    return true;
}
