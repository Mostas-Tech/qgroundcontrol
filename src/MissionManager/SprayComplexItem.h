#pragma once

#include "AgriculturalStyleComplexItem.h"
#include <QtCore/QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(SprayComplexItemLog)

class SprayComplexItem : public AgriculturalStyleComplexItem
{
    Q_OBJECT

   public:
    /// Display name used by UI and PlanCreator
    static const QString name;                           // defined in .cc
    /// V2 JSON complex item type tag
    static constexpr const char* jsonComplexItemTypeValue = "spray";

            /// @param flyView true: Fly View, false: Plan View
    SprayComplexItem(PlanMasterController* masterController, bool flyView);

            // ---------- VisualMissionItem / ComplexMissionItem identity ----------
    QString patternName        (void) const final { return name; }
    QString commandDescription (void) const final { return tr("Spray Pattern"); }
    QString commandName        (void) const final { return tr("Spray"); }
    QString abbreviation       (void) const final { return tr("SP"); }
    QString mapVisualQML       (void) const final { return QStringLiteral("SprayMapVisual.qml"); }
    // (No presets; base editor QML is loaded by your editor loader via missionItem.editorQml property, if used)

            // ---------- Mission build ----------
    void appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent) final;

    // ---------- Save/Load ----------
    void save(QJsonArray& planItems) final;
    bool load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString) final;

protected:
    MissionItem* _createScriptTimeItem(int sequenceNumber, int action, MAV_FRAME frame,
                                       QObject* missionItemParent) const override;
        int _scriptTimeItemCountForMission() const override { return 1; }
        MAV_CMD _exitWaypointCommand() const override;

private:
        double _scriptTimeInfoMode = 0.0;
        static constexpr const char* _jsonScriptTimeInfoModeKey = "scriptTimeInfoMode";
};
