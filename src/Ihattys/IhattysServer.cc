#include "IhattysServer.h"

// QGC includes (we include in .cc to avoid header bloat)
#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

// Qt
#include <QCoreApplication>
#include <QThread>
#include <QString>
#include <QtGlobal>
#include <QtMath>

// gRPC
#include <grpcpp/server_builder.h>

using namespace std::chrono_literals;

namespace {
constexpr float kArmAuthAcceptSubId = 20.0f;
constexpr float kArmAuthRejectSubId = 21.0f;
constexpr float kArmAuthAllow = 1.0f;
constexpr float kArmAuthDeny = 0.0f;
constexpr float kArmAuthSoftAllowSeconds = 10.0f;
} // namespace

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

void TelemetryCache::updateRcReceiverStatus(bool is_available)
{
    QWriteLocker l(&_lock);
    _snap.rc_receiver_status = is_available;
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

    VehicleGPSFactGroup* gps = nullptr;

    // GPS (prefer FactGroup values driven directly from MAVLink GPS_RAW_INT).
    // Vehicle::latitude/longitude are also available but are not always updated
    // the same way across firmware/message sets.
    if (auto fg = v->gpsFactGroup()) {
        gps = qobject_cast<VehicleGPSFactGroup*>(fg);
        if (gps) {
            if (gps->lat())   _snap.lat_deg = gps->lat()->rawValue().toDouble();
            if (gps->lon())   _snap.lon_deg = gps->lon()->rawValue().toDouble();
            if (gps->count()) _snap.gps_sat_count = gps->count()->rawValue().toInt();
            if (gps->lock())  _snap.gps_fix_type = gps->lock()->rawValue().toInt(); // MAVLink GPS_FIX_TYPE
        }
    }

    // Fallback for coordinate if GPS FactGroup hasn't received data yet.
    if (!qIsFinite(_snap.lat_deg)) _snap.lat_deg = v->latitude();
    if (!qIsFinite(_snap.lon_deg)) _snap.lon_deg = v->longitude();

    // Home position (MAVLink HOME_POSITION via Vehicle).
    const QGeoCoordinate home = v->homePosition();
    if (home.isValid()) {
        _snap.home_lat_deg = home.latitude();
        _snap.home_lon_deg = home.longitude();
        _snap.home_alt_amsl_m = home.altitude();
    } else {
        _snap.home_lat_deg = std::numeric_limits<double>::quiet_NaN();
        _snap.home_lon_deg = std::numeric_limits<double>::quiet_NaN();
        _snap.home_alt_amsl_m = std::numeric_limits<double>::quiet_NaN();
    }

    // Altitudes (meters)
    if (v->altitudeAMSL())     _snap.alt_amsl_m = v->altitudeAMSL()->rawValue().toDouble();
    if (v->altitudeRelative()) _snap.rel_alt_m  = v->altitudeRelative()->rawValue().toDouble();

    // Arming state
    _snap.armed = v->armed();

    // In-Air
    _snap.in_air = v->flying();

    // Attitude (degrees) and timestamp (us)
    // Prefer Facts if available; else derive from vehicle helpers.
    if (v->roll())  _snap.roll_deg  = v->roll()->rawValue().toFloat();
    if (v->pitch()) _snap.pitch_deg = v->pitch()->rawValue().toFloat();
    if (v->heading()) _snap.yaw_deg = v->heading()->rawValue().toFloat();
    _snap.attitude_timestamp_us = QDateTime::currentMSecsSinceEpoch() * 1000LL;

    // Heading (degrees), prefer vehicle heading, fallback to GPS course-over-ground.
    double heading_deg = std::numeric_limits<double>::quiet_NaN();
    if (v->heading()) {
        heading_deg = v->heading()->rawValue().toDouble();
    }
    if (!qIsFinite(heading_deg) && gps && gps->courseOverGround()) {
        heading_deg = gps->courseOverGround()->rawValue().toDouble();
    }
    _snap.heading_deg = heading_deg;

    // Velocity (NED): use ground speed with GPS course-over-ground for horizontal components.
    double ground_speed_m_s = std::numeric_limits<double>::quiet_NaN();
    if (v->groundSpeed()) {
        ground_speed_m_s = v->groundSpeed()->rawValue().toDouble();
    }
    double cog_deg = std::numeric_limits<double>::quiet_NaN();
    if (gps && gps->courseOverGround()) {
        cog_deg = gps->courseOverGround()->rawValue().toDouble();
    }
    if (qIsFinite(ground_speed_m_s) && qIsFinite(cog_deg)) {
        const double cog_rad = qDegreesToRadians(cog_deg);
        _snap.vel_north_m_s = static_cast<float>(ground_speed_m_s * qCos(cog_rad));
        _snap.vel_east_m_s = static_cast<float>(ground_speed_m_s * qSin(cog_rad));
    } else {
        _snap.vel_north_m_s = std::numeric_limits<float>::quiet_NaN();
        _snap.vel_east_m_s = std::numeric_limits<float>::quiet_NaN();
    }
    double climb_rate_m_s = std::numeric_limits<double>::quiet_NaN();
    if (v->climbRate()) {
        climb_rate_m_s = v->climbRate()->rawValue().toDouble();
    }
    if (qIsFinite(climb_rate_m_s)) {
        _snap.vel_down_m_s = static_cast<float>(-climb_rate_m_s);
    } else {
        _snap.vel_down_m_s = std::numeric_limits<float>::quiet_NaN();
    }

    // Derived signal level from satellite count.
    _recomputeGpsSignalLevel();
    _snap.vehicle_id = v->id(); //TODO: Add a Lue Script to vehicle set product vehicle company informations than add a param load in here
 
}

// =================== HoldWatchdog ===================

HoldWatchdog::HoldWatchdog(QObject* parent)
    : QObject(parent)
{
    _timer.setInterval(200);
    _timer.setTimerType(Qt::PreciseTimer);
    connect(&_timer, &QTimer::timeout, this, &HoldWatchdog::_checkTimeout);
    _timer.start();
}

void HoldWatchdog::recordHold()
{
    _lastHoldMs.store(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()), std::memory_order_relaxed);
}

void HoldWatchdog::_checkTimeout()
{
    const int64_t last = _lastHoldMs.load(std::memory_order_relaxed);
    if (last <= 0) {
        return;
    }

    const int64_t now = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
    if ((now - last) < _timeoutMs) {
        return;
    }

    int64_t expected = last;
    if (!_lastHoldMs.compare_exchange_strong(expected, 0, std::memory_order_relaxed)) {
        return;
    }

    auto* mvm = MultiVehicleManager::instance();
    if (!mvm) {
        return;
    }
    auto* vehicle = mvm->activeVehicle();
    if (!vehicle) {
        return;
    }
    const QString mode = vehicle->takeControlFlightMode();
    if (!mode.isEmpty()) {
        vehicle->setFlightMode(mode);
    }
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

grpc::Status IhattysServerService::CoreServiceImpl::SubscribeConnectionState(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::core::SubscribeConnectionStateRequest*,
    grpc::ServerWriter<mavsdk::rpc::core::ConnectionStateResponse>* writer)
{
    // 1 Hz as a “status” stream is fine here
    const int period_ms = 1000;
    mavsdk::rpc::core::ConnectionStateResponse resp;

    // Connection = client <-> server. Emit 'connected' immediately for this subscriber.
    resp.mutable_connection_state()->set_is_connected(true);
    if (!writer->Write(resp)) return grpc::Status::OK;

    // Keep streaming as long as client is connected.
    while (!_ctxCancelled(ctx)) {
        // keep sending "connected" (or change to heartbeat payload if desired)
        resp.mutable_connection_state()->set_is_connected(true);
        if (!writer->Write(resp)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }

    // When we get here the client unsubscribed / connection closed.
    return grpc::Status::OK;
}

// -------- TelemetryService --------

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribePosition(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribePositionRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::PositionResponse>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    mavsdk::rpc::telemetry::PositionResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        auto pos = msg.mutable_position();
        pos->set_latitude_deg(s.lat_deg);
        pos->set_longitude_deg(s.lon_deg);
        pos->set_absolute_altitude_m(static_cast<float>(s.alt_amsl_m));  // AMSL (per spec)
        pos->set_relative_altitude_m(static_cast<float>(s.rel_alt_m)); // Relative to home
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeHome(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeHomeRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::HomeResponse>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    mavsdk::rpc::telemetry::HomeResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        auto home = msg.mutable_home();
        home->set_latitude_deg(s.home_lat_deg);
        home->set_longitude_deg(s.home_lon_deg);
        home->set_absolute_altitude_m(static_cast<float>(s.home_alt_amsl_m));
        if (qIsFinite(s.home_lat_deg) && qIsFinite(s.home_lon_deg)) {
            home->set_relative_altitude_m(0.0f);
        } else {
            home->set_relative_altitude_m(std::numeric_limits<float>::quiet_NaN());
        }
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeAltitude(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeAltitudeRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::AltitudeResponse>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    mavsdk::rpc::telemetry::AltitudeResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.mutable_altitude()->set_altitude_amsl_m(static_cast<float>(s.alt_amsl_m));
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeInAir(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeInAirRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::InAirResponse>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    mavsdk::rpc::telemetry::InAirResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_is_in_air(s.in_air);
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeArmed(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeArmedRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::ArmedResponse>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    mavsdk::rpc::telemetry::ArmedResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.set_is_armed(s.armed);
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeAttitudeEuler(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeAttitudeEulerRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::AttitudeEulerResponse>* writer)
{
    // 50 Hz
    const int period_ms = 20;
    mavsdk::rpc::telemetry::AttitudeEulerResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        auto e = msg.mutable_attitude_euler();
        e->set_roll_deg(s.roll_deg);
        e->set_pitch_deg(s.pitch_deg);
        e->set_yaw_deg(s.yaw_deg);
        e->set_timestamp_us(s.attitude_timestamp_us);
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeVelocityNed(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeVelocityNedRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::VelocityNedResponse>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    mavsdk::rpc::telemetry::VelocityNedResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        auto vel = msg.mutable_velocity_ned();
        vel->set_north_m_s(s.vel_north_m_s);
        vel->set_east_m_s(s.vel_east_m_s);
        vel->set_down_m_s(s.vel_down_m_s);
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeHeading(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeHeadingRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::HeadingResponse>* writer)
{
    // 10 Hz
    const int period_ms = 100;
    mavsdk::rpc::telemetry::HeadingResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        auto heading = msg.mutable_heading_deg();
        heading->set_heading_deg(s.heading_deg);
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

// -------- FlightControllerService --------

grpc::Status IhattysServerService::TelemetryServiceImpl::SubscribeGpsInfo(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::telemetry::SubscribeGpsInfoRequest*,
    grpc::ServerWriter<mavsdk::rpc::telemetry::GpsInfoResponse>* writer)
{
    // 1 Hz
    const int period_ms = 1000;
    mavsdk::rpc::telemetry::GpsInfoResponse msg;
    grpc::WriteOptions writeOpts;
    writeOpts.set_write_through();

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        msg.mutable_gps_info()->set_num_satellites(s.gps_sat_count);
        {
            using mavsdk::rpc::telemetry::FixType;
            FixType fix = FixType::FIX_TYPE_NO_GPS;
            switch (s.gps_fix_type) {
            case 1: fix = FixType::FIX_TYPE_NO_FIX; break;
            case 2: fix = FixType::FIX_TYPE_FIX_2D; break;
            case 3: fix = FixType::FIX_TYPE_FIX_3D; break;
            case 4: fix = FixType::FIX_TYPE_FIX_DGPS; break;
            case 5: fix = FixType::FIX_TYPE_RTK_FLOAT; break;
            case 6: fix = FixType::FIX_TYPE_RTK_FIXED; break;
            default:
                // Fallback heuristic if fix type isn't available.
                if (s.gps_sat_count >= 4) {
                    fix = FixType::FIX_TYPE_FIX_3D;
                } else if (s.gps_sat_count == 3) {
                    fix = FixType::FIX_TYPE_FIX_2D;
                } else if (s.gps_sat_count > 0) {
                    fix = FixType::FIX_TYPE_NO_FIX;
                }
                break;
            }
            msg.mutable_gps_info()->set_fix_type(fix);
        }
        // mavsdk GpsInfo does not have a 'level' enum equivalent; omit for now.
        if (!writer->Write(msg, writeOpts)) {
            break;
        }
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

// -------- TelemetryServerService --------

grpc::Status IhattysServerService::TelemetryServerServiceImpl::PublishSysStatus(
    grpc::ServerContext*,
    const mavsdk::rpc::telemetry_server::PublishSysStatusRequest* request,
    mavsdk::rpc::telemetry_server::PublishSysStatusResponse* response)
{
    if (_cache) {
        // Only cache RC receiver status for now; other fields are currently ignored.
        _cache->updateRcReceiverStatus(request->rc_receiver_status());
        response->mutable_telemetry_server_result()->set_result(
            mavsdk::rpc::telemetry_server::TelemetryServerResult::RESULT_SUCCESS);
    } else {
        response->mutable_telemetry_server_result()->set_result(
            mavsdk::rpc::telemetry_server::TelemetryServerResult::RESULT_NO_SYSTEM);
    }
    return grpc::Status::OK;
}

// -------- ActionService --------

grpc::Status IhattysServerService::ActionServiceImpl::Hold(
    grpc::ServerContext*,
    const mavsdk::rpc::action::HoldRequest*,
    mavsdk::rpc::action::HoldResponse* response)
{

    // Request RTL for hold semantics.
    if (auto mvm = MultiVehicleManager::instance()) {
        if (auto vehicle = mvm->activeVehicle()) {
            // Delegate to Vehicle API which will call the firmware plugin to
            // perform the proper RTL command for the platform.
            vehicle->guidedModeRTL(false);
            if (_holdWatchdog) {
                _holdWatchdog->recordHold();
            }
            response->mutable_action_result()->set_result(
                mavsdk::rpc::action::ActionResult::RESULT_SUCCESS);
            return grpc::Status::OK;
        }
    }

    response->mutable_action_result()->set_result(
        mavsdk::rpc::action::ActionResult::RESULT_FAILED);
    return grpc::Status::OK;
}

// -------- ArmAuthorizerServerService --------

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::SubscribeArmAuthorization(
    grpc::ServerContext* ctx,
    const mavsdk::rpc::arm_authorizer_server::SubscribeArmAuthorizationRequest*,
    grpc::ServerWriter<mavsdk::rpc::arm_authorizer_server::ArmAuthorizationResponse>* writer)
{
    // 1 Hz: stream the active system id (0 if unknown)
    const int period_ms = 1000;
    mavsdk::rpc::arm_authorizer_server::ArmAuthorizationResponse msg;

    while (!_ctxCancelled(ctx)) {
        auto s = _cache->snapshot();
        int systemid = s.vehicle_id >= 0 ? s.vehicle_id : 0;
        msg.set_system_id(systemid);
        if (!writer->Write(msg)) break;
        IhattysServerService::_sleepMillis(period_ms);
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::AcceptArmAuthorization(
    grpc::ServerContext*,
    const mavsdk::rpc::arm_authorizer_server::AcceptArmAuthorizationRequest* req,
    mavsdk::rpc::arm_authorizer_server::AcceptArmAuthorizationResponse* out)
{
    int valid_s = req->valid_time_s();
    auto snap = _cache->snapshot();
    int sysid = snap.vehicle_id >= 0 ? snap.vehicle_id : 0;
    _state->acceptForSeconds(sysid, valid_s);
    out->mutable_arm_authorizer_server_result()->set_result(
        mavsdk::rpc::arm_authorizer_server::ArmAuthorizerServerResult::RESULT_SUCCESS);
    // Send MAV_CMD_DO_SEND_SCRIPT_MESSAGE to the active vehicle so it knows
    // about the remote arm-accept. param1=accept sub-id, param2=1 (accept),
    // param3=valid_time_s (seconds).
    if (auto mvm = MultiVehicleManager::instance()) {
        if (auto vehicle = mvm->activeVehicle()) {
            const int compId = sysid;
            const MAV_CMD cmd = MAV_CMD_DO_SEND_SCRIPT_MESSAGE;
            vehicle->sendMavCommand(compId, cmd, /*showError=*/false,
                                    kArmAuthAcceptSubId, /*param1*/
                                    kArmAuthAllow,  /*param2*/
                                    static_cast<float>(valid_s) /*param3*/);
        }
    }
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::ArmAuthorizerServerServiceImpl::RejectArmAuthorization(
    grpc::ServerContext*,
    const mavsdk::rpc::arm_authorizer_server::RejectArmAuthorizationRequest* req,
    mavsdk::rpc::arm_authorizer_server::RejectArmAuthorizationResponse* out)
{   
    // MavSDK uses an enum for reason and an extra_info int; translate to strings
    auto snap = _cache->snapshot();
    int sysid = snap.vehicle_id >= 0 ? snap.vehicle_id : 0;
    const bool soft_allow = !req->temporarily();
    if (soft_allow) {
        _state->acceptForSeconds(sysid, static_cast<int>(kArmAuthSoftAllowSeconds));
    } else {
        _state->reject(true, std::to_string(static_cast<int>(req->reason())),
                       std::to_string(req->extra_info()));
    }
    //add logging
    qDebug() << "RejectArmAuthorization called with reason:" << req->reason() << "temporarily:" << req->temporarily() << "extra_info:" << req->extra_info();    
    out->mutable_arm_authorizer_server_result()->set_result(
        mavsdk::rpc::arm_authorizer_server::ArmAuthorizerServerResult::RESULT_FAILED);
    if (auto mvm = MultiVehicleManager::instance()) {
        if (auto vehicle = mvm->activeVehicle()) {
            int compId = sysid;
            const MAV_CMD cmd = MAV_CMD_DO_SEND_SCRIPT_MESSAGE;
            const float rc_status_param = snap.rc_receiver_status ? 1.0f : 0.0f;
            const float reject_param2 = soft_allow ? kArmAuthAllow : kArmAuthDeny;
            const float reject_param3 = soft_allow ? kArmAuthSoftAllowSeconds : rc_status_param;
            // Reject sub-id: param2=1 => soft allow for param3 seconds; param2=0 => deny (param3=rc status).
            vehicle->sendMavCommand(compId, cmd, /*showError=*/false,
                                    kArmAuthRejectSubId, /*param1*/
                                    reject_param2,  /*param2*/
                                    reject_param3 /*param3*/
                                    );
        }
    }
    return grpc::Status::OK;
}

// -------- InfoService --------

grpc::Status IhattysServerService::InfoServiceImpl::GetProduct(
    grpc::ServerContext*,
    const mavsdk::rpc::info::GetProductRequest*,
    mavsdk::rpc::info::GetProductResponse* reply)
{
    // Static for now; you can wire to Vehicle->firmwareType()/brand if needed.
    reply->mutable_product()->set_vendor_name("DJI");
    reply->mutable_product()->set_product_name("Air 3");
    return grpc::Status::OK;
}

grpc::Status IhattysServerService::InfoServiceImpl::GetIdentification(
    grpc::ServerContext*,
    const mavsdk::rpc::info::GetIdentificationRequest*,
    mavsdk::rpc::info::GetIdentificationResponse* reply)
{
    // Fill from your platform identity source if available
    reply->mutable_identification()->set_hardware_uid("testtest");
    reply->mutable_identification()->set_legacy_uid(0);
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
    _telemetryServerSvc = std::make_unique<TelemetryServerServiceImpl>(&_telemetryCache);
    _actionSvc   = std::make_unique<ActionServiceImpl>(&_holdWatchdog);
    _armSvc      = std::make_unique<ArmAuthorizerServerServiceImpl>(&_armAuth, &_telemetryCache);
    _infoSvc     = std::make_unique<InfoServiceImpl>(&_telemetryCache);

    grpc::ServerBuilder builder;
    // Synchronous streaming RPC handlers block a gRPC poller thread for the
    // lifetime of the stream. Mobile clients often open multiple telemetry
    // subscriptions concurrently, so increase the poller limit to prevent
    // starvation (which looks like "subscriptions succeed but no data").
    unsigned maxPollers = std::thread::hardware_concurrency();
    if (maxPollers == 0) maxPollers = 4;
    if (maxPollers < 4) maxPollers = 4;
    maxPollers *= 4;
    if (maxPollers > 32) maxPollers = 32;
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, 2);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 2);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, static_cast<int>(maxPollers));
    // Match the demo client's keepalive expectations on Android.
    builder.AddChannelArgument("grpc.keepalive_time_ms", 30000);
    builder.AddChannelArgument("grpc.keepalive_timeout_ms", 5000);
    builder.AddChannelArgument("grpc.keepalive_permit_without_calls", 1);
    builder.AddListeningPort(_listenAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(_coreSvc.get());
    builder.RegisterService(_telemetrySvc.get());
    builder.RegisterService(_telemetryServerSvc.get());
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
    _telemetryServerSvc.reset();
    _actionSvc.reset();
    _armSvc.reset();
    _infoSvc.reset();
}

void IhattysServerService::setListenAddress(const std::string& address)
{
    if (address.empty() || _listenAddress == address) {
        return;
    }
    _listenAddress = address;
}

void IhattysServerService::attachActiveVehicle(Vehicle* v) {
    _telemetryCache.attachVehicle(v);
}

void IhattysServerService::_sleepMillis(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
