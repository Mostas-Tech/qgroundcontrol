#include "IhattysServer.h"
#include <QDebug>
#include "Utilities/QGCLoggingCategory.h"
#include <grpcpp/alarm.h>
#include <chrono>  // std::chrono clocks and durations

QGC_LOGGING_CATEGORY(IhattysServerLog, "IhattysServerService");

using grpc::ServerAsyncResponseWriter;
using grpc::ServerAsyncWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using std::chrono::steady_clock;
using std::chrono::microseconds;

// Frequencies -> periods (ms)
constexpr int kHz1     = 1000; // 1 Hz
constexpr int k10Hz    = 100;  // 10 Hz
constexpr int k20Hz    = 50;   // 20 Hz
constexpr int k50Hz    = 20;   // 50 Hz
constexpr int k100Hz   = 10;   // 100 Hz

namespace {

// ------------------------
// Base Call holder
// ------------------------
struct CallBase {
    virtual ~CallBase() = default;
    virtual void proceed(bool ok) = 0;
};

// Helper for periodic writes in streaming RPCs
inline int64_t nowUs() {
    return std::chrono::duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

IhattysServerService::IhattysServerService(QObject* parent)
    : QObject(parent) {
}

IhattysServerService::~IhattysServerService() {
    stop();
}

// =======================================
// INFO SERVICE (GetProduct / GetIdentification)
// =======================================

struct GetProductCall : CallBase {
    ihattys::InfoService::AsyncService* service;
    ServerCompletionQueue* cq;
    ServerContext ctx;
    ihattys::GetProductRequest req;
    ihattys::GetProductResponse resp;
    ServerAsyncResponseWriter<ihattys::GetProductResponse> responder;
    enum { CREATE, PROCESS, FINISH } state{CREATE};
    IhattysServerService::VehicleState* st;

    GetProductCall(ihattys::InfoService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), responder(&ctx), st(_st) { proceed(true); }

    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[InfoService::GetProduct] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[InfoService::GetProduct] Request registration";
            state = PROCESS;
            service->RequestGetProduct(&ctx, &req, &responder, cq, cq, this);
        } else if (state == PROCESS) {
            qCWarning(IhattysServerLog) << "[InfoService::GetProduct] PROCESS -> re-arm and respond";
            // re-arm
            new GetProductCall(service, cq, st);

                    // Fill from current state
            resp.mutable_info_result()->set_result(ihattys::InfoResult::RESULT_SUCCESS);
            resp.mutable_info_result()->set_result_str("OK");
            auto* p = resp.mutable_product();
            p->set_vendor_id(0);
            p->set_vendor_name(st->vendorName);
            p->set_product_id(0);
            p->set_product_name(st->productName);

            state = FINISH;
            qCWarning(IhattysServerLog) << "[InfoService::GetProduct] FINISH -> responder.Finish(Status::OK)";
            responder.Finish(resp, Status::OK, this);
        } else {
            qCWarning(IhattysServerLog) << "[InfoService::GetProduct] DELETE call object";
            delete this;
        }
    }
};

struct GetIdentificationCall : CallBase {
    ihattys::InfoService::AsyncService* service;
    ServerCompletionQueue* cq;
    ServerContext ctx;
    ihattys::GetIdentificationRequest req;
    ihattys::GetIdentificationResponse resp;
    ServerAsyncResponseWriter<ihattys::GetIdentificationResponse> responder;
    enum { CREATE, PROCESS, FINISH } state{CREATE};
    IhattysServerService::VehicleState* st;

    GetIdentificationCall(ihattys::InfoService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), responder(&ctx), st(_st) { proceed(true); }

    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[InfoService::GetIdentification] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[InfoService::GetIdentification] Request registration";
            state = PROCESS;
            service->RequestGetIdentification(&ctx, &req, &responder, cq, cq, this);
        } else if (state == PROCESS) {
            qCWarning(IhattysServerLog) << "[InfoService::GetIdentification] PROCESS -> re-arm and respond";
            new GetIdentificationCall(service, cq, st);

            resp.mutable_info_result()->set_result(ihattys::InfoResult::RESULT_SUCCESS);
            resp.mutable_info_result()->set_result_str("OK");
            auto* id = resp.mutable_identification();
            id->set_hardware_uid(st->hardwareUid);
            id->set_legacy_uid(0);

            state = FINISH;
            qCWarning(IhattysServerLog) << "[InfoService::GetIdentification] FINISH -> responder.Finish(Status::OK)";
            responder.Finish(resp, Status::OK, this);
        } else {
            qCWarning(IhattysServerLog) << "[InfoService::GetIdentification] DELETE call object";
            delete this;
        }
    }
};

// =======================================
// CORE SERVICE (subscribeConnectionState)
// =======================================

struct SubscribeConnectionStateCall : CallBase {
    ihattys::CoreService::AsyncService* service;
    ServerCompletionQueue* cq;
    ServerContext ctx;
    ihattys::SubscribeConnectionStateRequest req;
    ServerAsyncWriter<ihattys::ConnectionStateResponse> writer;
    enum { CREATE, WRITE, FINISH } state{CREATE};
    grpc::Alarm alarm;
    IhattysServerService::VehicleState* st;

    SubscribeConnectionStateCall(ihattys::CoreService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }

    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[CoreService::subscribeConnectionState] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[CoreService::subscribeConnectionState] Request registration";
            state = WRITE;
            service->RequestSubscribeConnectionState(&ctx, &req, &writer, cq, cq, this);
        } else if (state == WRITE) {
            qCWarning(IhattysServerLog) << "[CoreService::subscribeConnectionState] WRITE -> spawn next and push sample";
            // re-arm new call for next subscriber
            new SubscribeConnectionStateCall(service, cq, st);

                    // Periodic push loop using Alarm ticks (1 Hz is sufficient)
            ihattys::ConnectionStateResponse resp;
            resp.set_isconnected(st->connected.load());
            qCWarning(IhattysServerLog) << "[CoreService::subscribeConnectionState] writer.Write(isConnected="
                                        << resp.isconnected() << ")";
            writer.Write(resp, this);
            // schedule next tick
            alarm.Set(
                cq,
                std::chrono::system_clock::now() + std::chrono::milliseconds(kHz1),
                this);

        } else {
            qCWarning(IhattysServerLog) << "[CoreService::subscribeConnectionState] DELETE call object";
            delete this;
        }
    }
};

// =======================================
// TELEMETRY SERVICE (server-side streaming)
// =======================================

template<typename RequestT, typename ResponseT, typename BuilderFn>
struct PeriodicStreamCall : CallBase {
    using AsyncSvcT = ihattys::TelemetryService::AsyncService;

    AsyncSvcT* service;
    ServerCompletionQueue* cq;
    ServerContext ctx;
    RequestT req;
    ServerAsyncWriter<ResponseT> writer;
    enum { CREATE, WRITE, FINISH } state{CREATE};
    grpc::Alarm alarm;
    IhattysServerService::VehicleState* st;
    BuilderFn build;
    int periodMs; // 100 = 10Hz, 50 = 20Hz, 20 = 50Hz, 10 = 100Hz

    PeriodicStreamCall(AsyncSvcT* s, ServerCompletionQueue* c,
                       IhattysServerService::VehicleState* _st, BuilderFn b, int ms)
        : service(s), cq(c), writer(&ctx), st(_st), build(b), periodMs(ms) { proceed(true); }

    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[TelemetryService::PeriodicStream] proceed ok=" << ok << " state=" << state
                                    << " periodMs=" << periodMs;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[TelemetryService::PeriodicStream] CREATE (caller should have registered)";
            state = WRITE;
            // The concrete Request method is bound by the caller through build usage.
            // We'll be registered by the concrete wrapper below.
        } else if (state == WRITE) {
            ResponseT resp;
            build(*st, resp);
            qCWarning(IhattysServerLog) << "[TelemetryService::PeriodicStream] writer.Write(sample)";
            writer.Write(resp, this);
            alarm.Set(
                cq,
                std::chrono::system_clock::now() + std::chrono::milliseconds(periodMs),
                this);

        } else {
            qCWarning(IhattysServerLog) << "[TelemetryService::PeriodicStream] DELETE call object";
            delete this;
        }
    }
};

// Concrete wrappers per Telemetry RPC
struct SubscribePositionCall : CallBase {
    ihattys::TelemetryService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribePositionRequest req; ServerAsyncWriter<ihattys::PositionResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribePositionCall(ihattys::TelemetryService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribePosition] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[TelemetryService::subscribePosition] Request registration";
            service->RequestSubscribePosition(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        // Spawn next handler and stream at 10Hz
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribePosition] STREAM -> spawn next and write";
        new SubscribePositionCall(service, cq, st);
        ihattys::PositionResponse r;
        auto* p = r.mutable_position();
        p->set_latitudedeg(st->lat.load());
        p->set_longitudedeg(st->lon.load());
        p->set_absolutealtitudem(st->alt_amsl.load());
        p->set_relativealtitudem(st->alt_rel.load());
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribePosition] writer.Write(lat=" << p->latitudedeg()
                                    << ", lon=" << p->longitudedeg() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(k10Hz),
            this);
    }
};

struct SubscribeAltitudeCall : CallBase {
    ihattys::TelemetryService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribeAltitudeRequest req; ServerAsyncWriter<ihattys::AltitudeResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribeAltitudeCall(ihattys::TelemetryService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAltitude] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAltitude] Request registration";
            service->RequestSubscribeAltitude(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAltitude] STREAM -> spawn next and write";
        new SubscribeAltitudeCall(service, cq, st);
        ihattys::AltitudeResponse r;
        auto* a = r.mutable_altitude();
        a->set_altitudeamslm(st->alt_amsl.load());
        a->set_relativealtitudem(st->alt_rel.load());
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAltitude] writer.Write(amsl=" << a->altitudeamslm()
                                    << ", rel=" << a->relativealtitudem() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(k10Hz),
            this);
    }
};

struct SubscribeInAirCall : CallBase {
    ihattys::TelemetryService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribeInAirRequest req; ServerAsyncWriter<ihattys::InAirResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribeInAirCall(ihattys::TelemetryService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeInAir] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[TelemetryService::subscribeInAir] Request registration";
            service->RequestSubscribeInAir(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeInAir] STREAM -> spawn next and write";
        new SubscribeInAirCall(service, cq, st);
        ihattys::InAirResponse r;
        r.set_isinair(st->inAir.load());
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeInAir] writer.Write(isInAir=" << r.isinair() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(kHz1),
            this);
    }
};

struct SubscribeAttitudeEulerCall : CallBase {
    ihattys::TelemetryService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribeAttitudeEulerRequest req; ServerAsyncWriter<ihattys::AttitudeEulerResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribeAttitudeEulerCall(ihattys::TelemetryService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAttitudeEuler] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAttitudeEuler] Request registration";
            service->RequestSubscribeAttitudeEuler(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAttitudeEuler] STREAM -> spawn next and write";
        new SubscribeAttitudeEulerCall(service, cq, st);
        ihattys::AttitudeEulerResponse r;
        auto* e = r.mutable_eulerangle();
        e->set_rolldeg(st->roll.load());
        e->set_pitchdeg(st->pitch.load());
        e->set_yawdeg(st->yaw.load());
        e->set_timestampus(nowUs());
        qCWarning(IhattysServerLog) << "[TelemetryService::subscribeAttitudeEuler] writer.Write(roll=" << e->rolldeg()
                                    << ", pitch=" << e->pitchdeg() << ", yaw=" << e->yawdeg() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(k50Hz),
            this);
    }
};

// =======================================
// FLIGHT CONTROLLER SERVICE (subscribeGpsInfo)
// =======================================

struct SubscribeGpsInfoCall : CallBase {
    ihattys::FlightControllerService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribeGpsInfoRequest req; ServerAsyncWriter<ihattys::GpsInfoResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribeGpsInfoCall(ihattys::FlightControllerService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[FlightControllerService::subscribeGpsInfo] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[FlightControllerService::subscribeGpsInfo] Request registration";
            service->RequestSubscribeGpsInfo(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        qCWarning(IhattysServerLog) << "[FlightControllerService::subscribeGpsInfo] STREAM -> spawn next and write";
        new SubscribeGpsInfoCall(service, cq, st);
        ihattys::GpsInfoResponse r;
        auto* g = r.mutable_gpsinfo();
        g->set_numsatellites(st->numSat.load());
        g->set_level(static_cast<ihattys::GpsSignalLevel>(st->gpsLevel.load()));
        qCWarning(IhattysServerLog) << "[FlightControllerService::subscribeGpsInfo] writer.Write(numSat=" << g->numsatellites()
                                    << ", level=" << g->level() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(kHz1),
            this);
    }
};

// =======================================
// ACTION SERVICE (hold)
// =======================================

struct HoldCall : CallBase {
    ihattys::ActionService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::HoldRequest req; ihattys::HoldResponse resp; ServerAsyncResponseWriter<ihattys::HoldResponse> responder;
    enum { CREATE, PROCESS, FINISH } state{CREATE};
    HoldCall(ihattys::ActionService::AsyncService* s, ServerCompletionQueue* c)
        : service(s), cq(c), responder(&ctx) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[ActionService::hold] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[ActionService::hold] Request registration";
            state = PROCESS;
            service->RequestHold(&ctx, &req, &responder, cq, cq, this);
        } else if (state == PROCESS) {
            qCWarning(IhattysServerLog) << "[ActionService::hold] PROCESS -> re-arm and respond";
            new HoldCall(service, cq);
            // TODO: invoke your FCU "HOLD" or "Pause" here and report result
            resp.mutable_action_result()->set_result(ihattys::ActionResult::RESULT_SUCCESS);
            resp.mutable_action_result()->set_result_str("Hold executed");
            state = FINISH;
            qCWarning(IhattysServerLog) << "[ActionService::hold] FINISH -> responder.Finish(Status::OK)";
            responder.Finish(resp, Status::OK, this);
        } else {
            qCWarning(IhattysServerLog) << "[ActionService::hold] DELETE call object";
            delete this;
        }
    }
};

// =======================================
// ARM AUTHORIZER SERVER SERVICE (subscribe + accept/reject)
// =======================================

struct SubscribeArmAuthorizationCall : CallBase {
    ihattys::ArmAuthorizerServerService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::SubscribeArmAuthorizationRequest req; ServerAsyncWriter<ihattys::ArmAuthorizationResponse> writer;
    grpc::Alarm alarm; IhattysServerService::VehicleState* st; bool primed{false};
    SubscribeArmAuthorizationCall(ihattys::ArmAuthorizerServerService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), writer(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::subscribeArmAuthorization] proceed ok=" << ok
                                    << " state=" << (primed ? "STREAM" : "CREATE");
        if (!primed) {
            primed = true;
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::subscribeArmAuthorization] Request registration";
            service->RequestSubscribeArmAuthorization(&ctx, &req, &writer, cq, cq, this);
            return;
        }
        qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::subscribeArmAuthorization] STREAM -> spawn next and write";
        new SubscribeArmAuthorizationCall(service, cq, st);
        ihattys::ArmAuthorizationResponse r;
        // Populate minimum context (e.g., system id). If your proto expects fields, set them here.
        r.set_systemid(1); // TODO wire to your FCU
        qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::subscribeArmAuthorization] writer.Write(systemId=" << r.systemid() << ")";
        writer.Write(r, this);
        alarm.Set(
            cq,
            std::chrono::system_clock::now() + std::chrono::milliseconds(kHz1),
            this);
    }
};

struct RejectArmAuthorizationCall : CallBase {
    ihattys::ArmAuthorizerServerService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::RejectArmAuthorizationRequest req; ihattys::RejectArmAuthorizationResponse resp;
    ServerAsyncResponseWriter<ihattys::RejectArmAuthorizationResponse> responder;
    enum { CREATE, PROCESS, FINISH } state{CREATE};
    RejectArmAuthorizationCall(ihattys::ArmAuthorizerServerService::AsyncService* s, ServerCompletionQueue* c)
        : service(s), cq(c), responder(&ctx) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::rejectArmAuthorization] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::rejectArmAuthorization] Request registration";
            state = PROCESS;
            service->RequestRejectArmAuthorization(&ctx, &req, &responder, cq, cq, this);
        } else if (state == PROCESS) {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::rejectArmAuthorization] PROCESS -> re-arm and respond";
            new RejectArmAuthorizationCall(service, cq);
            // TODO: force arming disallowed in your FCU
            auto* r = resp.mutable_result();
            r->set_result(ihattys::ArmAuthorizerServerResult::RESULT_SUCCESS);
            r->set_result_str("Arming rejected");
            state = FINISH;
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::rejectArmAuthorization] FINISH -> responder.Finish(Status::OK)";
            responder.Finish(resp, Status::OK, this);
        } else {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::rejectArmAuthorization] DELETE call object";
            delete this;
        }
    }
};

struct AcceptArmAuthorizationCall : CallBase {
    ihattys::ArmAuthorizerServerService::AsyncService* service; ServerCompletionQueue* cq; ServerContext ctx;
    ihattys::AcceptArmAuthorizationRequest req; ihattys::AcceptArmAuthorizationResponse resp;
    ServerAsyncResponseWriter<ihattys::AcceptArmAuthorizationResponse> responder;
    IhattysServerService::VehicleState* st;
    enum { CREATE, PROCESS, FINISH } state{CREATE};
    AcceptArmAuthorizationCall(ihattys::ArmAuthorizerServerService::AsyncService* s, ServerCompletionQueue* c, IhattysServerService::VehicleState* _st)
        : service(s), cq(c), responder(&ctx), st(_st) { proceed(true); }
    void proceed(bool ok) override {
        qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::acceptArmAuthorization] proceed ok=" << ok << " state=" << state;
        if (state == CREATE) {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::acceptArmAuthorization] Request registration";
            state = PROCESS;
            service->RequestAcceptArmAuthorization(&ctx, &req, &responder, cq, cq, this);
        } else if (state == PROCESS) {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::acceptArmAuthorization] PROCESS -> re-arm and respond";
            new AcceptArmAuthorizationCall(service, cq, st);
            // Record arm validity window for your arming gate
            int32_t validSec = req.valid_time_s();
            st->armValidUntilUs.store(nowUs() + static_cast<int64_t>(validSec) * 1000000LL);
            auto* r = resp.mutable_result();
            r->set_result(ihattys::ArmAuthorizerServerResult::RESULT_SUCCESS);
            r->set_result_str("Arming accepted");
            state = FINISH;
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::acceptArmAuthorization] FINISH -> responder.Finish(Status::OK)";
            responder.Finish(resp, Status::OK, this);
        } else {
            qCWarning(IhattysServerLog) << "[ArmAuthorizerServerService::acceptArmAuthorization] DELETE call object";
            delete this;
        }
    }
};

// =======================================
// START / STOP
// =======================================

bool IhattysServerService::start() {
    if (_running.load()) {
        qCWarning(IhattysServerLog) << "IhattysServerService already running";
        return true;
    }

    try {
        ServerBuilder builder;

                // Listen on configured address (127.0.0.1:50051 by default)
        qCWarning(IhattysServerLog) << "Adding listening port:" << _listenAddress;
        builder.AddListeningPort(_listenAddress.toStdString(), grpc::InsecureServerCredentials());

                // Register async services
        _svcInfo     = std::make_unique<ihattys::InfoService::AsyncService>();
        _svcCore     = std::make_unique<ihattys::CoreService::AsyncService>();
        _svcTelemetry= std::make_unique<ihattys::TelemetryService::AsyncService>();
        _svcFlightCtl= std::make_unique<ihattys::FlightControllerService::AsyncService>();
        _svcAction   = std::make_unique<ihattys::ActionService::AsyncService>();
        _svcArmAuth  = std::make_unique<ihattys::ArmAuthorizerServerService::AsyncService>();

        qCWarning(IhattysServerLog) << "Registering services";
        builder.RegisterService(_svcInfo.get());
        builder.RegisterService(_svcCore.get());
        builder.RegisterService(_svcTelemetry.get());
        builder.RegisterService(_svcFlightCtl.get());
        builder.RegisterService(_svcAction.get());
        builder.RegisterService(_svcArmAuth.get());

        _cq = builder.AddCompletionQueue();
        qCWarning(IhattysServerLog) << "Building and starting gRPC server";
        _server = builder.BuildAndStart();
        if (!_server) {
            qCWarning(IhattysServerLog) << "Failed to start gRPC server";
            _svcInfo.reset(); _svcCore.reset(); _svcTelemetry.reset(); _svcFlightCtl.reset(); _svcAction.reset(); _svcArmAuth.reset();
            _cq.reset();
            return false;
        }

        _running.store(true);
        qCWarning(IhattysServerLog) << "Starting CQ thread";
        _cqThread = std::thread(&IhattysServerService::_cqThreadMain, this);

        qCWarning(IhattysServerLog) << "IHATTYS gRPC server started on" << _listenAddress;
        emit started();
        return true;
    } catch (const std::exception& e) {
        qCWarning(IhattysServerLog) << "Exception starting gRPC server:" << e.what();
        return false;
    }
}

void IhattysServerService::stop() {
    if (!_running.exchange(false)) {
        return;
    }
    qCWarning(IhattysServerLog) << "Stopping IHATTYS gRPC server";

    if (_server) {
        qCWarning(IhattysServerLog) << "Server.Shutdown()";
        _server->Shutdown();
    }
    if (_cq) {
        qCWarning(IhattysServerLog) << "CQ.Shutdown()";
        _cq->Shutdown();
    }
    if (_cqThread.joinable()) {
        qCWarning(IhattysServerLog) << "Joining CQ thread";
        _cqThread.join();
    }

    qCWarning(IhattysServerLog) << "Resetting services and CQ";
    _svcInfo.reset();
    _svcCore.reset();
    _svcTelemetry.reset();
    _svcFlightCtl.reset();
    _svcAction.reset();
    _svcArmAuth.reset();

    _cq.reset();
    _server.reset();

    qCWarning(IhattysServerLog) << "IHATTYS gRPC server stopped";
    emit stopped();
}

void IhattysServerService::_cqThreadMain() {
    qCWarning(IhattysServerLog) << "[CQThread] Priming RPC handlers";

            // Prime one handler per RPC
    new GetProductCall(_svcInfo.get(), _cq.get(), &_state);
    new GetIdentificationCall(_svcInfo.get(), _cq.get(), &_state);

    new SubscribeConnectionStateCall(_svcCore.get(), _cq.get(), &_state);

    new SubscribePositionCall(_svcTelemetry.get(), _cq.get(), &_state);
    new SubscribeAltitudeCall(_svcTelemetry.get(), _cq.get(), &_state);
    new SubscribeInAirCall(_svcTelemetry.get(), _cq.get(), &_state);
    new SubscribeAttitudeEulerCall(_svcTelemetry.get(), _cq.get(), &_state);

    new SubscribeGpsInfoCall(_svcFlightCtl.get(), _cq.get(), &_state);

    new HoldCall(_svcAction.get(), _cq.get());

    new SubscribeArmAuthorizationCall(_svcArmAuth.get(), _cq.get(), &_state);
    new RejectArmAuthorizationCall(_svcArmAuth.get(), _cq.get());
    new AcceptArmAuthorizationCall(_svcArmAuth.get(), _cq.get(), &_state);

    void* tag = nullptr;
    bool ok = false;

    while (_cq->Next(&tag, &ok)) {
        qCWarning(IhattysServerLog) << "[CQThread] Next(tag=" << tag << ", ok=" << ok << ")";
        if (!ok || tag == nullptr) {
            qCWarning(IhattysServerLog) << "[CQThread] Skipping dispatch for tag=" << tag << " ok=" << ok;
            continue;
        }
        qCWarning(IhattysServerLog) << "[CQThread] Dispatching tag=" << tag;
        static_cast<CallBase*>(tag)->proceed(ok);
    }

    qCWarning(IhattysServerLog) << "CQ thread exiting";
}
