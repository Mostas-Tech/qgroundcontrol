#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QReadWriteLock>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

// gRPC
#include <grpcpp/grpcpp.h>

// Generated from your .proto files (adjust include paths to your build)
// Use MavSDK proto-generated headers
#include "mavsdk/core/core.grpc.pb.h"
#include "mavsdk/telemetry/telemetry.grpc.pb.h"
#include "mavsdk/action/action.grpc.pb.h"
#include "mavsdk/arm_authorizer_server/arm_authorizer_server.grpc.pb.h"
#include "mavsdk/info/info.grpc.pb.h"

// QGC forward decls (avoid heavy includes in header)
class Vehicle;
class MultiVehicleManager;
class QGCApplication;

/// Thread-safe snapshot of telemetry pulled from QGC Vehicle on the Qt thread,
/// read by gRPC worker threads without touching QObjects directly.
struct TelemetrySnapshot {
    double lat_deg = std::numeric_limits<double>::quiet_NaN();
    double lon_deg = std::numeric_limits<double>::quiet_NaN();
    double alt_amsl_m = std::numeric_limits<double>::quiet_NaN();
    double rel_alt_m  = std::numeric_limits<double>::quiet_NaN();

    float roll_deg  = std::numeric_limits<float>::quiet_NaN();
    float pitch_deg = std::numeric_limits<float>::quiet_NaN();
    float yaw_deg   = std::numeric_limits<float>::quiet_NaN();

    bool in_air = false;

    int gps_sat_count = 0;
    // Derived 0..5 per your levels
    int gps_signal_level = 0;

    // Monotonic timestamp (us) when the attitude was last sampled
    int64_t attitude_timestamp_us = 0;

    int32_t vehicle_id = -1;
};

/// QObject living on Qt main thread that listens to Vehicle signals / Facts,
/// updates an internal snapshot behind a read-write lock.
class TelemetryCache : public QObject {
    Q_OBJECT
public:
    explicit TelemetryCache(QObject* parent = nullptr);
    void attachActiveVehicle(Vehicle* v);  // <— new
    void attachVehicle(Vehicle* v);
    void detachVehicle();

    TelemetrySnapshot snapshot() const;

private slots:
    void _pollFromVehicle(); // periodic polling when signals aren’t convenient

private:
    QPointer<Vehicle> _vehicle;
    mutable QReadWriteLock _lock;
    TelemetrySnapshot _snap;
    QTimer _pollTimer; // 20 Hz polling; we downsample per stream in service threads

    void _recomputeGpsSignalLevel(); // derive 0..5 level from sat count (simple heuristic)
};

/// Arming authorization state machine (thread-safe), backing ArmAuthorizerServerService.
/// External arming logic in QGC can ask isArmAuthorizedNow(systemId).
class ArmAuthState : public QObject {
    Q_OBJECT
public:
    explicit ArmAuthState(QObject* parent = nullptr) : QObject(parent) {}

    struct Decision {
        enum Type { Unknown, Accepted, Rejected } type{Unknown};
        // When Accepted, valid until this wall-clock timepoint; when Rejected and temporary,
        // valid until this timepoint, else until overridden.
        std::chrono::steady_clock::time_point valid_until{};
        // Optional context
        bool temporary_reject{false};
        std::string reason;
        std::string extra_info;
        int system_id{0};
    };

    void acceptForSeconds(int systemId, int valid_time_s);
    void reject(bool temporary, const std::string& reason, const std::string& extra);
    Decision current() const;

    // Convenience helper for QML/C++ arming logic
    Q_INVOKABLE bool isArmAuthorizedNow() const;

    // Allow wiring the telemetry cache (active vehicle id) so arm decisions
    // can be compared against the active vehicle.
    void setTelemetryCache(const TelemetryCache* cache) { _cache = cache; }

private:
    mutable std::mutex _mx;
    Decision _decision;
    // Optional pointer to telemetry cache (not owned)
    const TelemetryCache* _cache{nullptr};
};

/// The main service wrapper which owns the gRPC Server and service implementations.
class IhattysServerService : public QObject {
    Q_OBJECT
public:
    explicit IhattysServerService(QObject* parent = nullptr);
    ~IhattysServerService() override;

    // Start/Stop server. Hard-coded address per your request: 0.0.0.0:50051
    bool start();
    void stop();

    // Bind to app lifecycle
    static void bindToQGCAppLifecycle(QGCApplication* app, MultiVehicleManager* mvm);
    void attachActiveVehicle(Vehicle* v);

    // Expose arm auth state checker to the rest of QGC
    Q_INVOKABLE bool isArmAuthorizedNow() const { return _armAuth.isArmAuthorizedNow(); }

    // For unit tests
    std::string listenAddress() const { return _listenAddress; }

signals:
    void serverStarted();
    void serverStopped();

private:
    // Dependencies (main thread)
    TelemetryCache _telemetryCache;
    ArmAuthState   _armAuth;

    // gRPC server state
    std::unique_ptr<grpc::Server> _server;
    std::string _listenAddress = "0.0.0.0:50051";
    std::thread _serverThread;

    // ==== Service implementations (synchronous for clarity) ====
    class CoreServiceImpl final : public mavsdk::rpc::core::CoreService::Service {
    public:
        explicit CoreServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status SubscribeConnectionState(
            grpc::ServerContext* ctx,
            const mavsdk::rpc::core::SubscribeConnectionStateRequest*,
            grpc::ServerWriter<mavsdk::rpc::core::ConnectionStateResponse>* writer) override;

    private:
        const TelemetryCache* _cache{};
    };

    class TelemetryServiceImpl final : public mavsdk::rpc::telemetry::TelemetryService::Service {
    public:
        explicit TelemetryServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status SubscribePosition(
            grpc::ServerContext*,
            const mavsdk::rpc::telemetry::SubscribePositionRequest*,
            grpc::ServerWriter<mavsdk::rpc::telemetry::PositionResponse>* writer) override;

        grpc::Status SubscribeAltitude(
            grpc::ServerContext*,
            const mavsdk::rpc::telemetry::SubscribeAltitudeRequest*,
            grpc::ServerWriter<mavsdk::rpc::telemetry::AltitudeResponse>* writer) override;

        grpc::Status SubscribeInAir(
            grpc::ServerContext*,
            const mavsdk::rpc::telemetry::SubscribeInAirRequest*,
            grpc::ServerWriter<mavsdk::rpc::telemetry::InAirResponse>* writer) override;

        grpc::Status SubscribeAttitudeEuler(
            grpc::ServerContext*,
            const mavsdk::rpc::telemetry::SubscribeAttitudeEulerRequest*,
            grpc::ServerWriter<mavsdk::rpc::telemetry::AttitudeEulerResponse>* writer) override;

        grpc::Status SubscribeGpsInfo(
            grpc::ServerContext*,
            const mavsdk::rpc::telemetry::SubscribeGpsInfoRequest*,
            grpc::ServerWriter<mavsdk::rpc::telemetry::GpsInfoResponse>* writer) override;

    private:
        const TelemetryCache* _cache{};
    };

    

    class ActionServiceImpl final : public mavsdk::rpc::action::ActionService::Service {
    public:
        grpc::Status Hold(grpc::ServerContext*,
                          const mavsdk::rpc::action::HoldRequest*,
                          mavsdk::rpc::action::HoldResponse* response) override;
    };

    class ArmAuthorizerServerServiceImpl final : public mavsdk::rpc::arm_authorizer_server::ArmAuthorizerServerService::Service {
    public:
        explicit ArmAuthorizerServerServiceImpl(ArmAuthState* state, const TelemetryCache* cache)
            : _state(state), _cache(cache) {}

        grpc::Status SubscribeArmAuthorization(
            grpc::ServerContext*,
            const mavsdk::rpc::arm_authorizer_server::SubscribeArmAuthorizationRequest*,
            grpc::ServerWriter<mavsdk::rpc::arm_authorizer_server::ArmAuthorizationResponse>* writer) override;

        grpc::Status AcceptArmAuthorization(
            grpc::ServerContext*,
            const mavsdk::rpc::arm_authorizer_server::AcceptArmAuthorizationRequest*,
            mavsdk::rpc::arm_authorizer_server::AcceptArmAuthorizationResponse* writer) override;

        grpc::Status RejectArmAuthorization(
            grpc::ServerContext*,
            const mavsdk::rpc::arm_authorizer_server::RejectArmAuthorizationRequest*,
            mavsdk::rpc::arm_authorizer_server::RejectArmAuthorizationResponse* writer) override;

    private:
        ArmAuthState* _state{};
        const TelemetryCache* _cache{};
    };

    class InfoServiceImpl final : public mavsdk::rpc::info::InfoService::Service {
    public:
        explicit InfoServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status GetProduct(grpc::ServerContext*,
                                const mavsdk::rpc::info::GetProductRequest*,
                                mavsdk::rpc::info::GetProductResponse* reply) override;

        grpc::Status GetIdentification(grpc::ServerContext*,
                                       const mavsdk::rpc::info::GetIdentificationRequest*,
                                       mavsdk::rpc::info::GetIdentificationResponse* reply) override;
    private:
        const TelemetryCache* _cache{};
    };

    // Instances
    std::unique_ptr<CoreServiceImpl>             _coreSvc;
    std::unique_ptr<TelemetryServiceImpl>        _telemetrySvc;
    std::unique_ptr<ActionServiceImpl>           _actionSvc;
    std::unique_ptr<ArmAuthorizerServerServiceImpl> _armSvc;
    std::unique_ptr<InfoServiceImpl>             _infoSvc;

    // Helpers
    static void _sleepMillis(int ms);
};

