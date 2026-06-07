#include "chat_module_plugin.h"
#include "logos_sdk.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_consumer.h"
#include "token_manager.h"
#include "liblogos_rln_module_api.h"

namespace {
// Post an emitEvent call through the plugin host's event loop so it
// fires after the current synchronous stack unwinds.
//
// Every libchat callback (init/start/stop/destroy/get_id/...) is invoked
// synchronously from inside the corresponding libchat C API call (e.g.
// chat_new, chat_start), which is itself invoked synchronously from a
// LOGOS_METHOD on ChatModuleImpl. The plugin host's Q_INVOKABLE slot
// is still on the stack at that point; the host thread cannot reach
// the next event-loop iteration (which is when QLocalSocket flushes
// the QRO reply packet) until every synchronous emit has unwound.
// On slow runners (GHA ubuntu-latest) the resulting flush starvation
// exceeds the caller's 20s waitForFinished deadline → exit 4.
//
// Deferring via QueuedConnection lets the Q_INVOKABLE slot return
// promptly; the emit runs on the next event-loop iteration after the
// reply has been flushed, restoring normal ordering.
//
// Receiver is `impl->emitRouter()` (a QObject member of `impl`). When
// the impl is destroyed the router is destroyed too, and Qt drops any
// pending queued metacall — so the captured raw `impl` pointer below
// cannot be dereferenced after free.
void deferredEmit(ChatModuleImpl* impl, const char* eventName, std::string payload)
{
    QMetaObject::invokeMethod(
        impl->emitRouter(),
        [impl, name = std::string(eventName), payload = std::move(payload)]() {
            impl->emitEvent(name, payload);
        },
        Qt::QueuedConnection
    );
}
}  // namespace

static std::string isoTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    gmtime_r(&tt, &buf);
    char out[32];
    strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &buf);
    return out;
}

ChatModuleImpl::ChatModuleImpl() : chatCtx(nullptr)
{
    fprintf(stderr, "ChatModuleImpl: Initializing...\n");
    fprintf(stderr, "ChatModuleImpl: Initialized successfully\n");
}

ChatModuleImpl::~ChatModuleImpl()
{
    if (chatCtx) {
        chat_destroy(chatCtx, destroy_callback, this);
        chatCtx = nullptr;
    }
}

// ============================================================================
// Static Callback Functions
// ============================================================================

void ChatModuleImpl::init_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::init_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::init_callback: Invalid userData\n");
        return;
    }

    std::string message = (msg && len > 0) ? std::string(msg, len) : "";

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK);
    ev["statusCode"] = callerRet;
    ev["message"] = message;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatInitResult", ev.dump());
}

void ChatModuleImpl::start_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::start_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::start_callback: Invalid userData\n");
        return;
    }

    std::string message = (msg && len > 0) ? std::string(msg, len) : "";

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK);
    ev["statusCode"] = callerRet;
    ev["message"] = message;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatStartResult", ev.dump());
}

void ChatModuleImpl::stop_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::stop_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::stop_callback: Invalid userData\n");
        return;
    }

    std::string message = (msg && len > 0) ? std::string(msg, len) : "";

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK);
    ev["statusCode"] = callerRet;
    ev["message"] = message;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatStopResult", ev.dump());
}

void ChatModuleImpl::destroy_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::destroy_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::destroy_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);
        fprintf(stderr, "ChatModuleImpl::destroy_callback message: %s\n", message.c_str());

        nlohmann::json ev;
        ev["message"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, "chatDestroyResult", ev.dump());
    }
}

void ChatModuleImpl::event_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::event_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::event_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);

        std::string eventName = "chatEvent";
        try {
            auto doc = nlohmann::json::parse(message);
            if (doc.contains("eventType") && doc["eventType"].is_string()) {
                std::string eventType = doc["eventType"].get<std::string>();
                if (eventType == "new_message")
                    eventName = "chatNewMessage";
                else if (eventType == "new_conversation")
                    eventName = "chatNewConversation";
                else if (eventType == "delivery_ack")
                    eventName = "chatDeliveryAck";
            }
        } catch (...) {
            // parse failed, keep default eventName
        }

        nlohmann::json ev;
        ev["payload"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, eventName.c_str(), ev.dump());
    }
}

void ChatModuleImpl::get_id_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::get_id_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::get_id_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);

        nlohmann::json ev;
        ev["clientId"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, "chatGetIdResult", ev.dump());
    }
}

void ChatModuleImpl::list_conversations_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::list_conversations_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::list_conversations_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);

        nlohmann::json ev;
        ev["conversations"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, "chatListConversationsResult", ev.dump());
    }
}

void ChatModuleImpl::get_conversation_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::get_conversation_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::get_conversation_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);

        nlohmann::json ev;
        ev["conversation"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, "chatGetConversationResult", ev.dump());
    }
}

void ChatModuleImpl::new_private_conversation_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::new_private_conversation_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::new_private_conversation_callback: Invalid userData\n");
        return;
    }

    std::string conversationJson = (msg && len > 0) ? std::string(msg, len) : "";

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK && !conversationJson.empty());
    ev["statusCode"] = callerRet;
    ev["conversation"] = conversationJson;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatNewPrivateConversationResult", ev.dump());
}

void ChatModuleImpl::send_message_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::send_message_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::send_message_callback: Invalid userData\n");
        return;
    }

    std::string resultJson = (msg && len > 0) ? std::string(msg, len) : "";
    fprintf(stderr, "ChatModuleImpl::send_message_callback result: %s\n", resultJson.c_str());

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK);
    ev["statusCode"] = callerRet;
    ev["result"] = resultJson;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatSendMessageResult", ev.dump());
}

void ChatModuleImpl::get_identity_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::get_identity_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::get_identity_callback: Invalid userData\n");
        return;
    }

    if (msg && len > 0) {
        std::string message(msg, len);

        nlohmann::json ev;
        ev["identity"] = message;
        ev["timestamp"] = isoTimestamp();

        deferredEmit(impl, "chatGetIdentityResult", ev.dump());
    }
}

void ChatModuleImpl::create_intro_bundle_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    fprintf(stderr, "ChatModuleImpl::create_intro_bundle_callback called with ret: %d\n", callerRet);

    auto* impl = static_cast<ChatModuleImpl*>(userData);
    if (!impl) {
        fprintf(stderr, "ChatModuleImpl::create_intro_bundle_callback: Invalid userData\n");
        return;
    }

    std::string bundleStr = (msg && len > 0) ? std::string(msg, len) : "";

    nlohmann::json ev;
    ev["success"] = (callerRet == RET_OK && !bundleStr.empty());
    ev["statusCode"] = callerRet;
    ev["introBundle"] = bundleStr;
    ev["timestamp"] = isoTimestamp();

    deferredEmit(impl, "chatCreateIntroBundleResult", ev.dump());
}

// ============================================================================
// Client Lifecycle Methods
// ============================================================================

bool ChatModuleImpl::initChat(const std::string& configJson)
{
    fprintf(stderr, "ChatModuleImpl::initChat called with config: %s\n", configJson.c_str());

    chatCtx = chat_new(configJson.c_str(), init_callback, this);

    if (chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Chat context created successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to create Chat context\n");
        return false;
    }
}

bool ChatModuleImpl::startChat()
{
    fprintf(stderr, "ChatModuleImpl::startChat called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot start Chat - context not initialized. Call initChat first.\n");
        return false;
    }

    int result = chat_start(chatCtx, start_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Chat start initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to start Chat, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::stopChat()
{
    fprintf(stderr, "ChatModuleImpl::stopChat called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot stop Chat - context not initialized.\n");
        return false;
    }

    int result = chat_stop(chatCtx, stop_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Chat stop initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to stop Chat, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::destroyChat()
{
    fprintf(stderr, "ChatModuleImpl::destroyChat called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot destroy Chat - context not initialized.\n");
        return false;
    }

    int result = chat_destroy(chatCtx, destroy_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Chat destroy initiated successfully\n");
        chatCtx = nullptr;
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to destroy Chat, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::setEventCallback()
{
    fprintf(stderr, "ChatModuleImpl::setEventCallback called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot set event callback - context not initialized. Call initChat first.\n");
        return false;
    }

    set_event_callback(chatCtx, event_callback, this);

    fprintf(stderr, "ChatModuleImpl: Event callback set successfully\n");
    return true;
}

// ============================================================================
// Client Info Methods
// ============================================================================

bool ChatModuleImpl::getId()
{
    fprintf(stderr, "ChatModuleImpl::getId called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot get ID - context not initialized\n");
        return false;
    }

    int result = chat_get_id(chatCtx, get_id_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Get ID initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to get ID, error code: %d\n", result);
        return false;
    }
}

// ============================================================================
// Conversation Operations
// ============================================================================

bool ChatModuleImpl::listConversations()
{
    fprintf(stderr, "ChatModuleImpl::listConversations called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot list conversations - context not initialized\n");
        return false;
    }

    int result = chat_list_conversations(chatCtx, list_conversations_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: List conversations initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to list conversations, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::getConversation(const std::string& convoId)
{
    fprintf(stderr, "ChatModuleImpl::getConversation called with convoId: %s\n", convoId.c_str());

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot get conversation - context not initialized\n");
        return false;
    }

    int result = chat_get_conversation(chatCtx, get_conversation_callback, this, convoId.c_str());

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Get conversation initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to get conversation, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::newPrivateConversation(const std::string& introBundleStr, const std::string& contentHex)
{
    fprintf(stderr, "ChatModuleImpl::newPrivateConversation called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot create new private conversation - context not initialized\n");
        return false;
    }

    int result = chat_new_private_conversation(chatCtx, new_private_conversation_callback, this,
                                                introBundleStr.c_str(), contentHex.c_str());

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: New private conversation initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to create new private conversation, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::sendMessage(const std::string& convoId, const std::string& contentHex)
{
    fprintf(stderr, "ChatModuleImpl::sendMessage called with convoId: %s\n", convoId.c_str());

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot send message - context not initialized\n");
        return false;
    }

    int result = chat_send_message(chatCtx, send_message_callback, this,
                                    convoId.c_str(), contentHex.c_str());

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Send message initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to send message, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::sendMessageJson(const std::string& jsonStr)
{
    try {
        auto j = nlohmann::json::parse(jsonStr);
        const std::string convoId = j.at("convoId").get<std::string>();
        const std::string contentHex = j.at("contentHex").get<std::string>();
        return sendMessage(convoId, contentHex);
    } catch (const std::exception& e) {
        fprintf(stderr, "ChatModuleImpl::sendMessageJson: parse failure: %s\n", e.what());
        return false;
    }
}

// ============================================================================
// Identity Operations
// ============================================================================

bool ChatModuleImpl::getIdentity()
{
    fprintf(stderr, "ChatModuleImpl::getIdentity called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot get identity - context not initialized\n");
        return false;
    }

    int result = chat_get_identity(chatCtx, get_identity_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Get identity initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to get identity, error code: %d\n", result);
        return false;
    }
}

bool ChatModuleImpl::createIntroBundle()
{
    fprintf(stderr, "ChatModuleImpl::createIntroBundle called\n");

    if (!chatCtx) {
        fprintf(stderr, "ChatModuleImpl: Cannot create intro bundle - context not initialized\n");
        return false;
    }

    int result = chat_create_intro_bundle(chatCtx, create_intro_bundle_callback, this);

    if (result == RET_OK) {
        fprintf(stderr, "ChatModuleImpl: Create intro bundle initiated successfully\n");
        return true;
    } else {
        fprintf(stderr, "ChatModuleImpl: Failed to create intro bundle, error code: %d\n", result);
        return false;
    }
}

// ============================================================================
// RLN Operations
// ============================================================================

bool ChatModuleImpl::initLogos(const std::string& apiHandleHex)
{
    bool ok = false;
    quintptr raw = QString::fromStdString(apiHandleHex).toULongLong(&ok, 16);
    if (!ok || raw == 0) {
        qWarning() << "ChatModuleImpl::initLogos: invalid api handle"
                   << QString::fromStdString(apiHandleHex);
        return false;
    }
    logosAPI = reinterpret_cast<LogosAPI*>(raw);
    qDebug() << "ChatModuleImpl: LogosAPI handle installed:" << logosAPI;
    return true;
}

// rln_fetcher is the FFI trampoline registered with the Nim chat library
// via chat_set_rln_fetcher. It is called from Nim chronos worker threads
// (callRlnFetcherAsync runs the fetcher off the chronos main loop).
//
// Threading contract:
// - Always dispatched onto the Qt thread via QueuedConnection (non-blocking
//   enqueue). The Qt thread is NEVER blocked waiting for the RPC to
//   liblogos_rln_module — instead, the typed client's *Async variants fire
//   their callbacks on the Qt event loop when the response arrives.
// - The calling Nim worker thread blocks on a std::promise::get_future() until
//   the async callback completes and sets the promise. This preserves the
//   existing chat_set_rln_fetcher contract (sync semantics from Nim's view)
//   without holding the Qt event loop hostage.
//
// This unblocks Qt during long-running RPCs (register_member can take 5-60s
// on testnet) and prevents cascading QtRO timeouts on unrelated host CLI
// commands.
int ChatModuleImpl::rln_fetcher(const char* method, const char* params,
    void (*callback)(int, const char*, size_t, void*), void* callbackData, void* fetcherData)
{
    auto* impl = static_cast<ChatModuleImpl*>(fetcherData);
    if (!impl || !impl->logosAPI) {
        if (callback) callback(1, "LogosAPI not available", 21, callbackData);
        return 1;
    }

    const std::string m = method ? method : "";
    const std::string p = params ? params : "";
    auto promise = std::make_shared<std::promise<QString>>();
    auto future = promise->get_future();

    QMetaObject::invokeMethod(impl->emitRouter(), [impl, m, p, promise]() {
        // Heap-allocate LiblogosRlnModule and capture by shared_ptr into each
        // setValue lambda. The typed client's *Async methods return
        // immediately; their callbacks fire later on the Qt event loop. If
        // `rln` were a stack local, it'd be destroyed when this lambda
        // returns (before the callback fires), leading to use-after-free
        // SIGSEGV in callRlnFetcherAsync's worker thread.
        auto rln = std::make_shared<LiblogosRlnModule>(impl->logosAPI);

        const QString methodStr = QString::fromStdString(m);
        const QString paramsStr = QString::fromStdString(p);

        // Capture `rln` by value (shared_ptr ref-count++) so it lives until
        // the async callback completes.
        auto setValue = [promise, rln](const QString& v) { promise->set_value(v); };

        if (methodStr == "get_valid_roots") {
            rln->get_valid_rootsAsync(paramsStr, setValue);
            return;
        }
        if (methodStr == "get_merkle_proofs") {
            const QStringList parts = paramsStr.split(",");
            if (parts.size() < 2) { setValue({}); return; }
            const QString configAccount = parts[0];
            const QString leafIndicesJson = "[" + parts[1] + "]";
            rln->get_merkle_proofsAsync(configAccount, leafIndicesJson,
                [setValue](QString proofsJson) {
                    const QJsonArray arr = QJsonDocument::fromJson(proofsJson.toUtf8()).array();
                    if (arr.isEmpty()) { setValue({}); return; }
                    setValue(QString::fromUtf8(
                        QJsonDocument(arr[0].toObject()).toJson(QJsonDocument::Compact)));
                });
            return;
        }
        if (methodStr == "generate_identity") {
            rln->generate_identityAsync(paramsStr, setValue);
            return;
        }
        if (methodStr == "register_member") {
            const QJsonDocument paramsDoc = QJsonDocument::fromJson(paramsStr.toUtf8());
            if (!paramsDoc.isObject()) { setValue({}); return; }
            const QJsonObject o = paramsDoc.object();
            const QString cfg = o["configAccountId"].toString();
            const QString holder = o["userHoldingAccountId"].toString();
            const QString idCommit = o["idCommitment"].toString();
            const int rateLimit = o["rateLimit"].toInt(100);
            if (cfg.isEmpty() || holder.isEmpty() || idCommit.isEmpty()) {
                setValue({}); return;
            }
            // register_member internally chains wallet RPCs; default 20s
            // QtRO timeout aborts when calls queue serially. Bump to 180s.
            rln->register_memberAsync(cfg, holder, idCommit, rateLimit, setValue,
                                       Timeout(180000));
            return;
        }
        setValue({});
    }, Qt::QueuedConnection);

    // Block the Nim worker thread until the Qt-thread async callback fires.
    // Qt event loop continues processing other work (subscribe, send, etc.)
    // during this wait — that's the whole point of the QueuedConnection +
    // async-typed-client + promise/future bridge.
    const QString result = future.get();
    if (result.isEmpty()) {
        if (callback) callback(1, "rln_fetcher failed", 18, callbackData);
        return 1;
    }
    const QByteArray utf8 = result.toUtf8();
    if (callback) callback(0, utf8.constData(), utf8.size(), callbackData);
    return 0;
}

bool ChatModuleImpl::setRlnConfig(const std::string& configAccountIdStd, int64_t leafIndex)
{
    if (!chatCtx) {
        qWarning() << "ChatModuleImpl: Cannot set RLN config - context not initialized";
        return false;
    }

    const QString configAccountId = QString::fromStdString(configAccountIdStd);
    chat_set_rln_fetcher(chatCtx, rln_fetcher, this);
    chat_set_rln_config(chatCtx, configAccountId.toUtf8().constData(),
                        static_cast<int>(leafIndex));

    // Lazy-fetch logosAPI via the LogosModuleContext mixin (modules().api).
    // The local `logosAPI` field is only set by the legacy initLogos(hex)
    // hook which the universal codegen-emitted provider doesn't auto-invoke
    // for chat_module. modules() is populated by the framework via
    // _logosCoreSetLogosModulesPtr_ before any method dispatch.
    if (!logosAPI && isContextReady()) {
        logosAPI = modules().api;
    }
    if (logosAPI) {
        // Subscribe to liblogos_rln_module events SYNCHRONOUSLY. The previous
        // QTimer::singleShot(15000, ...) defer never fired because setRlnConfig
        // runs on a QtRO worker thread without an active Qt event loop, so
        // queued timer events were never pumped. By the time setRlnConfig is
        // invoked (post-membership-confirmation), liblogos_rln_module has
        // been alive for minutes — the original 15s delay was solving a
        // problem that no longer exists.
        void* ctx = chatCtx;
        auto* rlnConsumer = new LogosAPIConsumer("liblogos_rln_module", "chat_module",
                                                  logosAPI->getTokenManager());
        LogosObject* rlnReplica = rlnConsumer->requestObject("liblogos_rln_module");
        if (rlnReplica) {
            rlnConsumer->onEvent(rlnReplica, "valid_roots",
                [ctx](const QString&, const QVariantList& data) {
                    if (data.isEmpty()) return;
                    QByteArray utf8 = data[0].toString().toUtf8();
                    if (!utf8.isEmpty()) chat_push_roots(ctx, utf8.constData());
                });
            rlnConsumer->onEvent(rlnReplica, "merkle_proof",
                [ctx](const QString&, const QVariantList& data) {
                    if (data.isEmpty()) return;
                    QByteArray utf8 = data[0].toString().toUtf8();
                    if (!utf8.isEmpty()) chat_push_proof(ctx, utf8.constData());
                });
            qInfo() << "ChatModuleImpl: Subscribed to RLN module events (sync)";
        } else {
            qWarning() << "ChatModuleImpl: Could not get RLN module replica during setRlnConfig";
        }
    } else {
        qWarning() << "ChatModuleImpl::setRlnConfig: logosAPI unavailable; RLN event subscription skipped";
    }

    qDebug() << "ChatModuleImpl: RLN config set, account:" << configAccountId << "leaf:" << leafIndex;
    return true;
}

std::string ChatModuleImpl::selfRegisterRln(const std::string& configAccountIdStd,
                                              const std::string& walletAccountIdStd,
                                              int64_t rateLimit)
{
    if (!logosAPI) {
        qWarning() << "selfRegisterRln: logosAPI not initialized";
        return {};
    }

    const QString configAccountId = QString::fromStdString(configAccountIdStd);
    const QString walletAccountId = QString::fromStdString(walletAccountIdStd);

    auto* rlnClient = logosAPI->getClient("liblogos_rln_module");
    if (!rlnClient) {
        qWarning() << "selfRegisterRln: RLN module not available";
        return {};
    }

    QByteArray seedBytes(32, 0);
    for (int i = 0; i < 32; ++i)
        seedBytes[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    QString seed = QString::fromLatin1(seedBytes.toHex());

    qDebug() << "selfRegisterRln: generating identity with seed" << seed.left(16) << "...";
    QVariant genResult = rlnClient->invokeRemoteMethod(
        "liblogos_rln_module", "generate_identity", QVariant(seed));
    QString genJson = genResult.toString();
    if (genJson.isEmpty()) {
        qWarning() << "selfRegisterRln: generate_identity failed";
        return {};
    }

    QJsonDocument genDoc = QJsonDocument::fromJson(genJson.toUtf8());
    QString idCommitment = genDoc.object()["id_commitment"].toString();
    QString idSecretHash = genDoc.object()["id_secret_hash"].toString();
    if (idCommitment.isEmpty() || idSecretHash.isEmpty()) {
        qWarning() << "selfRegisterRln: failed to parse identity" << genJson;
        return {};
    }
    qDebug() << "selfRegisterRln: identity generated, commitment:" << idCommitment.left(16) << "...";

    qDebug() << "selfRegisterRln: registering member...";
    QVariant regResult = rlnClient->invokeRemoteMethod(
        "liblogos_rln_module", "register_member",
        QVariant(configAccountId), QVariant(walletAccountId),
        QVariant(idCommitment), QVariant(static_cast<qlonglong>(rateLimit)));
    QString regJson = regResult.toString();
    if (regJson.isEmpty()) {
        qWarning() << "selfRegisterRln: register_member failed";
        return {};
    }

    QJsonDocument regDoc = QJsonDocument::fromJson(regJson.toUtf8());
    int leafIndex = static_cast<int>(regDoc.object()["leaf_index"].toDouble());
    qDebug() << "selfRegisterRln: registered at leaf" << leafIndex;

    if (!setRlnConfig(configAccountIdStd, leafIndex)) {
        qWarning() << "selfRegisterRln: setRlnConfig failed";
        return {};
    }

    if (chatCtx) {
        chat_set_rln_identity(chatCtx, seed.toUtf8().constData());
    }

    QJsonObject result;
    result["id_secret_hash"] = idSecretHash;
    result["id_commitment"] = idCommitment;
    result["leaf_index"] = leafIndex;
    return QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString();
}
