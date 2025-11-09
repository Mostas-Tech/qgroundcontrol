#pragma once
#include <QObject>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <QtCore/QLoggingCategory>

#include <grpcpp/grpcpp.h>
#include "ihattys_api.grpc.pb.h"   // must contain: CoreService, TelemetryService, FlightControllerService,
// ActionService, ArmAuthorizerServerService, InfoService

Q_DECLARE_LOGGING_CATEGORY(IhattysServerLog);

class IhattysServerService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString listenAddress READ listenAddress WRITE setListenAddress NOTIFY listenAddressChanged)

   public:
    explicit IhattysServerService(QObject* parent = nullptr);
    ~IhattysServerService() override;

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    QString listenAddress() const { return _listenAddress; }
    void setListenAddress(const QString& a) {
        if (_listenAddress == a) return;
        _listenAddress = a;
        emit listenAddressChanged();
    }

            // ↓↓↓ Make the type public so IhattysServer.cc can name it ↓↓↓
    struct VehicleState {
        std::atomic<bool> connected{false};   // RC/GCS <-> Vehicle link
        std::atomic<double> lat{0}, lon{0}, alt_amsl{0}, alt_rel{0};
        std::atomic<float>  roll{0}, pitch{0}, yaw{0};
        std::atomic<int32_t> numSat{0};
        std::atomic<int>    gpsLevel{0};
        std::atomic<bool>   inAir{false};
        std::string vendorName{"Unknown"};
        std::string productName{"Unknown"};
        std::string hardwareUid{""};
        std::atomic<int64_t> armValidUntilUs{0};
    };

   signals:
    void started();
    void stopped();
    void listenAddressChanged();

   private:
    // gRPC runtime members...
    std::unique_ptr<grpc::Server> _server;
    std::unique_ptr<grpc::ServerCompletionQueue> _cq;

    std::unique_ptr<ihattys::InfoService::AsyncService>                _svcInfo;
    std::unique_ptr<ihattys::CoreService::AsyncService>                _svcCore;
    std::unique_ptr<ihattys::TelemetryService::AsyncService>           _svcTelemetry;
    std::unique_ptr<ihattys::FlightControllerService::AsyncService>    _svcFlightCtl;
    std::unique_ptr<ihattys::ActionService::AsyncService>              _svcAction;
    std::unique_ptr<ihattys::ArmAuthorizerServerService::AsyncService> _svcArmAuth;

    std::thread _cqThread;
    std::atomic<bool> _running{false};
    QString _listenAddress{QStringLiteral("127.0.0.1:50051")};

    VehicleState _state;  // keeping the INSTANCE private is fine
    void _cqThreadMain();
};
