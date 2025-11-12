#include "IhattysServer.h"

// QGC includes (we include in .cc to avoid header bloat)
#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

// Qt
#include <QCoreApplication>
#include <QThread>

// gRPC
#include <grpcpp/server_builder.h>

using namespace std::chrono_literals;

// =================== TelemetryCache ===================

TelemetryCache::TelemetryCache(QObject* parent)
    : QObject(parent)
{
    // Poll at 20 Hz, we downsample in each streaming RPC.
    _pollTimer.setInterval(50);
    _pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&_pollTimer, &QTimer::timeout, this, &TelemetryCache::_pollFromVehicle);
    _pollTimer.start();
}



void TelemetryCache::attachVehicle(Vehicle* v)
{
    QWriteLocker l(&_lock);
    _vehicle = v;
}

void TelemetryCache::attachActiveVehicle(Vehicle* v)
{
    // For now simply delegate to attachVehicle. Kept as a separate
    // entrypoint for clarity and future platform-specific handling.
    attachVehicle(v);
}

void TelemetryCache::detachVehicle()
{
    QWriteLocker l(&_lock);
    _vehicle = nullptr;
}

TelemetrySnapshot TelemetryCache::snapshot() const
{
    QReadLocker l(&_lock);
    return _snap;
}

void TelemetryCache::_recomputeGpsSignalLevel()
{
    // Simple heuristic (you can refine with HDOP/fix type if available)
    int s = _snap.gps_sat_count;
    int level = 0;
    if      (s >= 18) level = 5;
    else if (s >= 14) level = 4;
    else if (s >= 10) level = 3;
    else if (s >= 7)  level = 2;
    else if (s >= 4)  level = 1;
    else              level = 0;
    _snap.gps_signal_level = level;
}

void TelemetryCache::_pollFromVehicle()
{
    Vehicle* v = _vehicle;
    if (!v) return;

    QWriteLocker l(&_lock);

    // Latitude / Longitude (degrees)
    _snap.lat_deg   = v->latitude();
    _snap.lon_deg   = v->longitude();

    // Altitudes (meters)
    if (v->altitudeAMSL())     _snap.alt_amsl_m = v->altitudeAMSL()->rawValue().toDouble();
    if (v->altitudeRelative()) _snap.rel_alt_m  = v->altitudeRelative()->rawValue().toDouble();

    // In-Air
    _snap.in_air = v->flying();

    // Attitude (degrees) and timestamp (us)
    // Prefer Facts if available; else derive from vehicle helpers.
    if (v->roll())  _snap.roll_deg  = v->roll()->rawValue().toFloat();
    if (v->pitch()) _snap.pitch_deg = v->pitch()->rawValue().toFloat();
    if (v->heading()) _snap.yaw_deg = v->heading()->rawValue().toFloat();
    _snap.attitude_timestamp_us = QDateTime::currentMSecsSinceEpoch() * 1000LL;

    // GPS
    // Prefer the FactGroup count fact if available; otherwise fall back to
    // vehicle helper APIs. Vehicle::gpsFactGroup() returns a FactGroup which
    // is actually a VehicleGPSFactGroup instance, so cast and use the
    // public count() accessor.
    int sats = 0;
    if (auto fg = v->gpsFactGroup()) {
        if (auto gps = static_cast<VehicleGPSFactGroup*>(fg)) {
            if (gps->count()) {
                sats = gps->count()->rawValue().toInt();
            }
        }
    }
    _snap.gps_sat_count = sats;
    _recomputeGpsSignalLevel();
    _snap.vehicle_id = v->id(); //TODO: Add a Lue Script to vehicle set product vehicle company informations than add a param load in here
}

// =================== ArmAuthState ===================

void ArmAuthState::acceptForSeconds(int systemId, int valid_time_s)
{
    std::lock_guard<std::mutex> lk(_mx);
    _decision.type = Decision::Accepted;
    _decision.system_id = systemId;
    _decision.temporary_reject = false;
    _decision.reason.clear();
    _decision.extra_info.clear();
    _decision.valid_until = std::chrono::steady_clock::now() + std::chrono::seconds(valid_time_s);
}

void ArmAuthState::reject(bool temporary, const std::string& reason, const std::string& extra)
{
    std::lock_guard<std::mutex> lk(_mx);
    _decision.type = Decision::Rejected;
    _decision.temporary_reject = temporary;
    _decision.reason = reason;
    _decision.extra_info = extra;
    _decision.valid_until = temporary ? (std::chrono::steady_clock::now() + 5s)  // small backoff, adjust as desired
                                      : std::chrono::steady_clock::time_point::max();
}

ArmAuthState::Decision ArmAuthState::current() const
{
    std::lock_guard<std::mutex> lk(_mx);
    return _decision;
}

bool ArmAuthState::isArmAuthorizedNow() const
{
    // If we have a telemetry cache wired, use its snapshot to get the active
    // vehicle id. Otherwise treat as unknown (no matching id).
    TelemetrySnapshot snap;
    if (_cache) {
        snap = _cache->snapshot();
    }

    std::lock_guard<std::mutex> lk(_mx);
    auto now = std::chrono::steady_clock::now();
    if (_decision.type == Decision::Accepted) {
        // Authorized only if the accepted system id matches the active vehicle id
        // and the acceptance hasn't expired.
        return _decision.system_id == snap.vehicle_id && now <= _decision.valid_until;
    }
    // Rejected/Unknown => not authorized
    if (_decision.type == Decision::Rejected) {
        // If temporary and expired, it's effectively unknown -> not authorized
        if (_decision.temporary_reject && now > _decision.valid_until) {
            return false;
        }
        return false;
    }
    return false; // Unknown => not authorized
}

// =================== Service impls ===================

static inline bool _ctxCancelled(grpc::ServerContext* ctx) {
    return ctx->IsCancelled();
}

// -------- CoreService --------

grpc::Status IhattysServerService::CoreServiceImpl::subscribeConnectionState(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeConnectionStateRequest*,
    grpc::ServerWriter<ihattys::v1::ConnectionStateResponse>* writer)
{
    // 1 Hz as a “status” stream is fine here
    const int period_ms = 1000;
    ihattys::v1::ConnectionStateResponse resp;

    while (!_ctxCancelled(ctx)) {
        auto snap = _cache->snapshot();
        const bool connected = std::isfinite(snap.lat_deg) && std::isfinite(snap.lon_deg); // heuristic
        resp.set_isconnected(connected);
        if (!writer->Write(resp)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

// -------- TelemetryService --------

grpc::Status IhattysServerService::TelemetryServiceImpl::subscribePosition(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribePositionRequest*,
    grpc::ServerWriter<ihattys::v1::Position>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    ihattys::v1::Position msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_lat(s.lat_deg);
        msg.set_lon(s.lon_deg);
        msg.set_alt(s.alt_amsl_m);  // AMSL (per spec)
        msg.set_relalt(s.rel_alt_m); // Relative to home
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::subscribeAltitude(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeAltitudeRequest*,
    grpc::ServerWriter<ihattys::v1::Altitude>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    ihattys::v1::Altitude msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_altitudeamslm(static_cast<float>(s.alt_amsl_m));
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::subscribeInAir(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeInAirRequest*,
    grpc::ServerWriter<ihattys::v1::InAirResponse>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    ihattys::v1::InAirResponse msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_isinair(s.in_air);
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::subscribeAttitudeEuler(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeAttitudeEulerRequest*,
    grpc::ServerWriter<ihattys::v1::AttitudeEulerResponse>* writer)
{
    // 50 Hz
    const int period_ms = 20;
    ihattys::v1::AttitudeEulerResponse msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_rolldeg(s.roll_deg);
        msg.set_pitchdeg(s.pitch_deg);
        msg.set_yawdeg(s.yaw_deg);
        msg.set_timestampus(s.attitude_timestamp_us);
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

// -------- FlightControllerService --------

grpc::Status IhattysServerService::FlightControllerServiceImpl::subscribeGpsInfo(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeGpsInfoRequest*,
    grpc::ServerWriter<ihattys::v1::GpsInfo>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    ihattys::v1::GpsInfo msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_numsatellites(s.gps_sat_count);
        msg.set_level(static_cast<ihattys::v1::GpsSignalLevel>(s.gps_signal_level));
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

// -------- ActionService --------

grpc::Status IhattysServerService::ActionServiceImpl::hold(
    grpc::ServerContext*,
    const ihattys::v1::HoldRequest*,
    ihattys::v1::ActionResult* response)
{
    // Not implemented yet per your request (#5)
    response->set_result(ihattys::v1::ActionResultCode::ACTION_FAILED);
    response->set_resultstr("Action 'hold' not implemented");
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "hold not implemented");
}

// -------- ArmAuthorizerServerService --------

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::subscribeArmAuthorization(
    grpc::ServerContext* ctx,
    const ihattys::v1::SubscribeArmAuthorizationRequest*,
    grpc::ServerWriter<ihattys::v1::ArmAuthorizationResponse>* writer)
{
    // 1 Hz: stream the active system id (0 if unknown)
    const int period_ms = 1000;
    ihattys::v1::ArmAuthorizationResponse msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        int systemid = s.vehicle_id >= 0 ? s.vehicle_id : 0;
        msg.set_systemid(systemid);
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::acceptArmAuthorization(
    grpc::ServerContext*,
    const ihattys::v1::AcceptArmAuthorizationRequest* req,
    ihattys::v1::ArmAuthorizerServerResult* out)
{
    int sysid = req->systemid();
    int valid_s = req->valid_time_s();
    qWarning() << "IhattysServerService::ArmAuthorizerServerServiceImpl::acceptArmAuthorization: sysid="
               << sysid << " valid_s=" << valid_s;
    _state->acceptForSeconds(sysid, valid_s);
    out->set_result(ihattys::v1::ArmAuthorizerServerResultCode::ARM_AUTH_SUCCESS);
     //TODO: send MAV_CMD_DO_SEND_SCRIPT_MESSAGE with param1=20 param2:1 param3:valid_s
    // Send MAV_CMD_DO_SEND_SCRIPT_MESSAGE to the active vehicle so it knows
    // about the remote arm-accept. param1=20 (custom code), param2=1 (accept),
    // param3=valid_time_s (seconds).
    if (auto mvm = MultiVehicleManager::instance()) {
        if (auto vehicle = mvm->activeVehicle()) {
            const int compId = sysid;
            const MAV_CMD cmd = MAV_CMD_DO_SEND_SCRIPT_MESSAGE;
            vehicle->sendMavCommand(compId, cmd, /*showError=*/false,
                                    20.0f, /*param1*/
                                    1.0f,  /*param2*/
                                    static_cast<float>(valid_s) /*param3*/);
        } else {
            qWarning() << "No active vehicle to send script message";
        }
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::rejectArmAuthorization(
    grpc::ServerContext*,
    const ihattys::v1::RejectArmAuthorizationRequest* req,
    ihattys::v1::ArmAuthorizerServerResult* out)
{   
    _state->reject(req->temporarily(), req->reason(), req->extrainfo());
    out->set_result(ihattys::v1::ArmAuthorizerServerResultCode::ARM_AUTH_FAILED);
    if (auto mvm = MultiVehicleManager::instance()) {
        if (auto vehicle = mvm->activeVehicle()) {
            auto snap = _cache->snapshot();
            int compId = snap.vehicle_id;
            const MAV_CMD cmd = MAV_CMD_DO_SEND_SCRIPT_MESSAGE;
            vehicle->sendMavCommand(compId, cmd, /*showError=*/false,
                                    20.0f, /*param1*/
                                    0.0f  /*param2*/
                                    );
        } else {
            qWarning() << "No active vehicle to send script message";
        }
    }
    return grpc::Status::OK;
}

// -------- InfoService --------

grpc::Status IhattysServerService::InfoServiceImpl::GetProduct(
    grpc::ServerContext*,
    const ihattys::v1::GetProductRequest*,
    ihattys::v1::Product* reply)
{
    // Static for now; you can wire to Vehicle->firmwareType()/brand if needed.
    reply->set_vendorname("Mostas / QGroundControl");
    reply->set_productname("Custom GCS");
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::InfoServiceImpl::GetIdentification(
    grpc::ServerContext*,
    const ihattys::v1::GetIdentificationRequest*,
    ihattys::v1::Identification* reply)
{
    // Fill from your platform identity source if available
    reply->set_hardware_uid("testest");
    reply->set_legacy_uid(0);
    return grpc::Status::OK;
}

// =================== IhattysServerService ===================

IhattysServerService::IhattysServerService(QObject* parent)
    : QObject(parent)
{
    // Keep TelemetryCache attached to active vehicle whenever it changes.
    // Attach to the global MultiVehicleManager instance. Many places in QGC
    // access the MVM via MultiVehicleManager::instance(), so use that here.
    auto* mvm = MultiVehicleManager::instance();
    if (mvm) {
        connect(mvm, &MultiVehicleManager::activeVehicleChanged, this, [this, mvm]() {
            _telemetryCache.attachVehicle(mvm->activeVehicle());
        });
        _telemetryCache.attachVehicle(mvm->activeVehicle());
    }    
}

IhattysServerService::~IhattysServerService()
{
    stop();
}

bool IhattysServerService::start()
{
    if (_server) return true;

    _coreSvc     = std::make_unique<CoreServiceImpl>(&_telemetryCache);
    _telemetrySvc= std::make_unique<TelemetryServiceImpl>(&_telemetryCache);
    _fcSvc       = std::make_unique<FlightControllerServiceImpl>(&_telemetryCache);
    _actionSvc   = std::make_unique<ActionServiceImpl>();
    _armSvc      = std::make_unique<ArmAuthorizerServerServiceImpl>(&_armAuth, &_telemetryCache);
    _infoSvc     = std::make_unique<InfoServiceImpl>(&_telemetryCache);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(_listenAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(_coreSvc.get());
    builder.RegisterService(_telemetrySvc.get());
    builder.RegisterService(_fcSvc.get());
    builder.RegisterService(_actionSvc.get());
    builder.RegisterService(_armSvc.get());
    builder.RegisterService(_infoSvc.get());

    _server = builder.BuildAndStart();
    if (!_server) return false;

    // Run server in a dedicated thread; synchronous services block here.
    _serverThread = std::thread([this]() {
        emit serverStarted();
        _server->Wait();
        emit serverStopped();
    });
    return true;
}

void IhattysServerService::stop()
{
    if (_server) {
        _server->Shutdown();
    }
    if (_serverThread.joinable()) {
        _serverThread.join();
    }
    _server.reset();
    _coreSvc.reset();
    _telemetrySvc.reset();
    _fcSvc.reset();
    _actionSvc.reset();
    _armSvc.reset();
    _infoSvc.reset();
}

void IhattysServerService::attachActiveVehicle(Vehicle* v) {
    _telemetryCache.attachVehicle(v);
}

void IhattysServerService::_sleepMillis(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}



