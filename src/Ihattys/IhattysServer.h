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
#include "ihattys_api.pb.h"
#include "ihattys_api.grpc.pb.h"

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
    Q_INVOKABLE bool isArmAuthorizedNow(int systemId) const;

private:
    mutable std::mutex _mx;
    Decision _decision;
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
    Q_INVOKABLE bool isArmAuthorizedNow(int systemId) const { return _armAuth.isArmAuthorizedNow(systemId); }

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
    class CoreServiceImpl final : public ihattys::v1::CoreService::Service {
    public:
        explicit CoreServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status subscribeConnectionState(
            grpc::ServerContext* ctx,
            const ihattys::v1::SubscribeConnectionStateRequest*,
            grpc::ServerWriter<ihattys::v1::ConnectionStateResponse>* writer) override;

    private:
        const TelemetryCache* _cache{};
    };

    class TelemetryServiceImpl final : public ihattys::v1::TelemetryService::Service {
    public:
        explicit TelemetryServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status subscribePosition(
            grpc::ServerContext*,
            const ihattys::v1::SubscribePositionRequest*,
            grpc::ServerWriter<ihattys::v1::Position>* writer) override;

        grpc::Status subscribeAltitude(
            grpc::ServerContext*,
            const ihattys::v1::SubscribeAltitudeRequest*,
            grpc::ServerWriter<ihattys::v1::Altitude>* writer) override;

        grpc::Status subscribeInAir(
            grpc::ServerContext*,
            const ihattys::v1::SubscribeInAirRequest*,
            grpc::ServerWriter<ihattys::v1::InAirResponse>* writer) override;

        grpc::Status subscribeAttitudeEuler(
            grpc::ServerContext*,
            const ihattys::v1::SubscribeAttitudeEulerRequest*,
            grpc::ServerWriter<ihattys::v1::AttitudeEulerResponse>* writer) override;

    private:
        const TelemetryCache* _cache{};
    };

    class FlightControllerServiceImpl final : public ihattys::v1::FlightControllerService::Service {
    public:
        explicit FlightControllerServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status subscribeGpsInfo(
            grpc::ServerContext*,
            const ihattys::v1::SubscribeGpsInfoRequest*,
            grpc::ServerWriter<ihattys::v1::GpsInfo>* writer) override;

    private:
        const TelemetryCache* _cache{};
    };

    class ActionServiceImpl final : public ihattys::v1::ActionService::Service {
    public:
        grpc::Status hold(grpc::ServerContext*,
                          const ihattys::v1::HoldRequest*,
                          ihattys::v1::ActionResult* response) override;
    };

    class ArmAuthorizerServerServiceImpl final : public ihattys::v1::ArmAuthorizerServerService::Service {
    public:
        explicit ArmAuthorizerServerServiceImpl(ArmAuthState* state, const TelemetryCache* cache)
            : _state(state), _cache(cache) {}

        grpc::Status subscribeArmAuthorization(
            grpc::ServerContext*,
            const ihattys::v1::SubscribeArmAuthorizationRequest*,
            grpc::ServerWriter<ihattys::v1::ArmAuthorizationResponse>* writer) override;

        grpc::Status acceptArmAuthorization(
            grpc::ServerContext*,
            const ihattys::v1::AcceptArmAuthorizationRequest*,
            ihattys::v1::ArmAuthorizerServerResult* writer) override;

        grpc::Status rejectArmAuthorization(
            grpc::ServerContext*,
            const ihattys::v1::RejectArmAuthorizationRequest*,
            ihattys::v1::ArmAuthorizerServerResult* writer) override;

    private:
        ArmAuthState* _state{};
        const TelemetryCache* _cache{};
    };

    class InfoServiceImpl final : public ihattys::v1::InfoService::Service {
    public:
        explicit InfoServiceImpl(const TelemetryCache* cache) : _cache(cache) {}
        grpc::Status GetProduct(grpc::ServerContext*,
                                const ihattys::v1::GetProductRequest*,
                                ihattys::v1::Product* reply) override;

        grpc::Status GetIdentification(grpc::ServerContext*,
                                       const ihattys::v1::GetIdentificationRequest*,
                                       ihattys::v1::Identification* reply) override;
    private:
        const TelemetryCache* _cache{};
    };

    // Instances
    std::unique_ptr<CoreServiceImpl>             _coreSvc;
    std::unique_ptr<TelemetryServiceImpl>        _telemetrySvc;
    std::unique_ptr<FlightControllerServiceImpl> _fcSvc;
    std::unique_ptr<ActionServiceImpl>           _actionSvc;
    std::unique_ptr<ArmAuthorizerServerServiceImpl> _armSvc;
    std::unique_ptr<InfoServiceImpl>             _infoSvc;

    // Helpers
    static void _sleepMillis(int ms);
};

