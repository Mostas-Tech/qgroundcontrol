#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(FieldJobCatalogManagerLog)

class QmlObjectListModel;
class Vehicle;

class FieldJobCatalogManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("QmlObjectListModel.h")

    Q_PROPERTY(QmlObjectListModel* fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(QString activeFieldId READ activeFieldId NOTIFY activeFieldIdChanged)
    Q_PROPERTY(QString activeJobId READ activeJobId NOTIFY activeJobIdChanged)
    Q_PROPERTY(bool fieldContextActive READ fieldContextActive NOTIFY fieldContextActiveChanged)
    Q_PROPERTY(bool jobContextActive READ jobContextActive NOTIFY jobContextActiveChanged)
    Q_PROPERTY(bool uploadInProgress READ uploadInProgress NOTIFY uploadInProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit FieldJobCatalogManager(QObject *parent = nullptr);
    ~FieldJobCatalogManager() override;

    QmlObjectListModel* fields() const { return _fields; }
    const QString& activeFieldId() const { return _activeFieldId; }
    const QString& activeJobId() const { return _activeJobId; }
    bool fieldContextActive() const { return _fieldContextActive; }
    bool jobContextActive() const { return _jobContextActive; }
    bool uploadInProgress() const { return _uploadInProgress; }
    const QString& lastError() const { return _lastError; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void beginCreateFieldDraft();
    Q_INVOKABLE void beginEditField(const QString& fieldId);
    Q_INVOKABLE void commitActiveField(const QString& name, const QVariantList& polygonPath, const QVariantList& exclusionPolygonPaths = QVariantList());
    Q_INVOKABLE QString activeFieldThumbnailFilePath();
    Q_INVOKABLE void deleteField(const QString& fieldId);
    Q_INVOKABLE QmlObjectListModel* jobsForField(const QString& fieldId);
    Q_INVOKABLE void beginCreateJob(const QString& fieldId);
    Q_INVOKABLE void beginEditJob(const QString& jobId);
    Q_INVOKABLE QString activeJobPlanFilePath();
    Q_INVOKABLE QString activeJobThumbnailFilePath();
    Q_INVOKABLE QVariantList activeJobFieldPolygonPath();
    Q_INVOKABLE QVariantList activeJobFieldExclusionPolygonPaths();
    Q_INVOKABLE void setActiveJobSprayMetrics(double pesticideLitersPerDekar, double dropletSizeMicron);
    Q_INVOKABLE void commitActiveJob(const QString& name, const QString& notes);
    Q_INVOKABLE void deleteJob(const QString& jobId);
    Q_INVOKABLE void startJob(const QString& jobId);
    Q_INVOKABLE void resumeJob(const QString& jobId, bool missionIncomplete);
    Q_INVOKABLE void clearContexts();

signals:
    void jobUploadSucceeded(const QString& jobId);
    void jobUploadFailed(const QString& jobId, const QString& errorText);
    void fieldsChanged();
    void lastErrorChanged();
    void uploadInProgressChanged();

    void activeFieldIdChanged();
    void activeJobIdChanged();
    void fieldContextActiveChanged();
    void jobContextActiveChanged();

private:
    struct FieldRecord {
        QString id;
        QString name;
        QString thumbnailFilePath;
        QVariantList polygonPath;
        QVariantList exclusionPolygonPaths;
        int jobCount = 0;
        QString createdAtUtc;
        QString updatedAtUtc;
        int schemaVersion = 1;
        int revision = 0;
        bool syncPending = false;
    };

    struct JobRecord {
        QString id;
        QString fieldId;
        QString name;
        QString planFilePath;
        QString thumbnailFilePath;
        double pesticideLitersPerDekar = 0.0;
        double dropletSizeMicron = 0.0;
        double nozzleValuePct = 0.0;
        double boomWidthM = 0.0;
        QString notes;
        bool needsRegeneration = false;
        int fieldRevisionRequired = 0;
        QVariantList fieldPolygonPath;
        QVariantList fieldExclusionPolygonPaths;
        QString createdAtUtc;
        QString updatedAtUtc;
        int schemaVersion = 1;
        int revision = 0;
        bool syncPending = false;
    };

    QString _catalogRootPath() const;
    QString _fieldsCatalogFilePath() const;
    QString _jobsCatalogFilePath() const;
    QString _fieldsDirectoryPath() const;
    QString _jobsDirectoryPath() const;
    QString _fieldDirectoryPath(const QString& fieldId) const;
    QString _fieldThumbnailFilePath(const QString& fieldId) const;
    QString _jobPlanFilePath(const QString& jobId) const;
    QString _jobThumbnailFilePath(const QString& jobId) const;

    bool _ensureCatalogDirectories(QString& errorString) const;
    bool _saveFieldsCatalog(QString& errorString) const;
    bool _saveJobsCatalog(QString& errorString) const;
    bool _loadFieldsCatalog(QString& errorString);
    bool _loadJobsCatalog(QString& errorString);

    void _refreshFieldsModel();
    void _refreshJobsModels();
    void _refreshJobsModelForField(const QString& fieldId);
    void _sortRecords();
    void _recomputeFieldJobCounts();

    int _findFieldRecordIndex(const QString& fieldId) const;
    int _findJobRecordIndex(const QString& jobId) const;
    int _findFieldRevision(const QString& fieldId) const;
    bool _removeFileIfExists(const QString& filePath, QString& errorString) const;
    bool _writeDefaultJobPlanFile(const QString& filePath, QString& errorString) const;
    bool _syncJobPlanGeoFenceFromCatalog(const JobRecord& job, QString& errorString) const;
    bool _validateUploadRequest(const QString& jobId, bool requireMissionIncomplete, bool missionIncomplete,
                                JobRecord& outJob, QString& errorString) const;
    bool _startJobUpload(const JobRecord& job, QString& errorString);
    void _clearUploadState();

    void _setActiveFieldId(const QString& fieldId);
    void _setActiveJobId(const QString& jobId);
    void _setFieldContextActive(bool active);
    void _setJobContextActive(bool active);
    void _setUploadInProgress(bool inProgress);
    void _setLastError(const QString& error);
    void _clearLastError();

    static QString _utcNowString();
    static bool _fieldRecordToJson(const FieldRecord& record, QJsonObject& json, QString& errorString);
    static bool _jobRecordToJson(const JobRecord& record, QJsonObject& json, QString& errorString);
    bool _fieldRecordFromJson(const QJsonObject& json, FieldRecord& record, QString& errorString) const;
    bool _jobRecordFromJson(const QJsonObject& json, JobRecord& record, QString& errorString) const;

    QmlObjectListModel* _fields = nullptr;
    QHash<QString, QPointer<QmlObjectListModel>> _jobsModelsByFieldId;
    QList<FieldRecord> _fieldRecords;
    QList<JobRecord> _jobRecords;

    QString _activeFieldId;
    QString _activeJobId;
    bool _fieldContextActive = false;
    bool _jobContextActive = false;
    bool _uploadInProgress = false;
    QString _lastError;

    FieldRecord _activeFieldDraft;
    bool _activeFieldDraftValid = false;
    bool _activeFieldCreateMode = false;

    JobRecord _activeJobDraft;
    bool _activeJobDraftValid = false;
    bool _activeJobCreateMode = false;

    QString _uploadJobId;
    QPointer<Vehicle> _uploadVehicle;
    QMetaObject::Connection _uploadSendCompleteConnection;
    QMetaObject::Connection _uploadInProgressConnection;
    QMetaObject::Connection _uploadErrorConnection;
    QString _uploadErrorText;
};
