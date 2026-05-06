#include "chat_module_plugin.h"
#include <QDebug>
#include <QCoreApplication>
#include <QVariantList>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QTimer>
#include <QRandomGenerator>
#include "logos_api.h"
#include "logos_api_client.h"
#include "token_manager.h"

ChatModulePlugin::ChatModulePlugin() : chatCtx(nullptr)
{
    qDebug() << "ChatModulePlugin: Initializing...";
    qDebug() << "ChatModulePlugin: Initialized successfully";
}

ChatModulePlugin::~ChatModulePlugin() 
{
    // Clean up Chat context if it exists
    if (chatCtx) {
        chat_destroy(chatCtx, destroy_callback, this);
        chatCtx = nullptr;
    }
    
    // Clean up resources
    if (logosAPI) {
        delete logosAPI;
        logosAPI = nullptr;
    }
}

void ChatModulePlugin::initLogos(LogosAPI* logosAPIInstance) {
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

void ChatModulePlugin::emitEvent(const QString& eventName, const QVariantList& data) {
    if (!logosAPI) {
        qWarning() << "ChatModulePlugin: LogosAPI not available, cannot emit" << eventName;
        return;
    }

    LogosAPIClient* client = logosAPI->getClient("chat_module");
    if (!client) {
        qWarning() << "ChatModulePlugin: Failed to get chat_module client for event" << eventName;
        return;
    }

    // Emit via Qt signal for QtRO forwarding (works on macOS).
    client->onEventResponse(this, eventName, data);

    // Optional stderr fallback for test/simulation environments. On Linux,
    // Qt's QueuedConnection events posted from the Nim FFI thread are never
    // processed by logos_host's main thread, so QtRO signal forwarding fails.
    // When LOGOS_EVENT_STDERR=1, event data is written to stderr so that sim
    // scripts can observe events regardless of Qt signal delivery.
    // Not needed in production — inter-instance communication uses Waku, not Qt signals.
    static const bool stderrEvents = qEnvironmentVariableIsSet("LOGOS_EVENT_STDERR");
    if (stderrEvents) {
        for (const QVariant& v : data) {
            QString s = v.toString();
            if (!s.isEmpty()) {
                QByteArray name = eventName.toUtf8();
                QByteArray val = s.toUtf8();
                fprintf(stderr, "EVENT:%s:%s\n", name.constData(), val.constData());
            }
        }
        fflush(stderr);
    }
}

// ============================================================================
// Static Callback Functions
// ============================================================================

void ChatModulePlugin::init_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::init_callback called with ret:" << callerRet;
    
    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::init_callback: Invalid userData";
        return;
    }

    QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";

    QVariantList eventData;
    eventData << (callerRet == RET_OK);  // success boolean
    eventData << callerRet;               // return code
    eventData << message;                 // message (may be empty)
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatInitResult", eventData);
}

void ChatModulePlugin::start_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::start_callback called with ret:" << callerRet;
    
    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::start_callback: Invalid userData";
        return;
    }

    QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";
    
    QVariantList eventData;
    eventData << (callerRet == RET_OK);  // success boolean
    eventData << callerRet;               // return code
    eventData << message;
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatStartResult", eventData);
}

void ChatModulePlugin::stop_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::stop_callback called with ret:" << callerRet;
    
    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::stop_callback: Invalid userData";
        return;
    }

    QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";

    QVariantList eventData;
    eventData << (callerRet == RET_OK);  // success boolean
    eventData << callerRet;               // return code
    eventData << message;
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatStopResult", eventData);
}

void ChatModulePlugin::destroy_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::destroy_callback called with ret:" << callerRet;
    
    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::destroy_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        qDebug() << "ChatModulePlugin::destroy_callback message:" << message;

        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent("chatDestroyResult", eventData);
    }
}

void ChatModulePlugin::event_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::event_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::event_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        
        // Parse the JSON to determine the event type
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        QString eventName = "chatEvent"; // Default event name
        
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString eventType = obj["eventType"].toString();
            
            // Map event types to Qt event names
            if (eventType == "new_message") {
                eventName = "chatNewMessage";
            } else if (eventType == "new_conversation") {
                eventName = "chatNewConversation";
            } else if (eventType == "delivery_ack") {
                eventName = "chatDeliveryAck";
            }
        }

        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent(eventName, eventData);
    }
}

void ChatModulePlugin::get_id_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::get_id_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::get_id_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        
        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent("chatGetIdResult", eventData);
    }
}

void ChatModulePlugin::list_conversations_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::list_conversations_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::list_conversations_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        
        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent("chatListConversationsResult", eventData);
    }
}

void ChatModulePlugin::get_conversation_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::get_conversation_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::get_conversation_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        
        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent("chatGetConversationResult", eventData);
    }
}

void ChatModulePlugin::new_private_conversation_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::new_private_conversation_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::new_private_conversation_callback: Invalid userData";
        return;
    }

    QString conversationJson = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";
    
    QVariantList eventData;
    eventData << (callerRet == RET_OK && !conversationJson.isEmpty());  // success
    eventData << callerRet;                                               // return code
    eventData << conversationJson;                                        // conversation JSON
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatNewPrivateConversationResult", eventData);
}

void ChatModulePlugin::send_message_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::send_message_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::send_message_callback: Invalid userData";
        return;
    }

    QString resultJson = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";
    qDebug() << "ChatModulePlugin::send_message_callback result:" << resultJson;
    
    QVariantList eventData;
    eventData << (callerRet == RET_OK);  // success
    eventData << callerRet;               // return code
    eventData << resultJson;              // result JSON (may contain message ID)
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatSendMessageResult", eventData);
}

void ChatModulePlugin::get_identity_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::get_identity_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::get_identity_callback: Invalid userData";
        return;
    }

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        
        QVariantList eventData;
        eventData << message;
        eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

        plugin->emitEvent("chatGetIdentityResult", eventData);
    }
}

void ChatModulePlugin::create_intro_bundle_callback(int callerRet, const char* msg, size_t len, void* userData)
{
    qDebug() << "ChatModulePlugin::create_intro_bundle_callback called with ret:" << callerRet;

    ChatModulePlugin* plugin = static_cast<ChatModulePlugin*>(userData);
    if (!plugin) {
        qWarning() << "ChatModulePlugin::create_intro_bundle_callback: Invalid userData";
        return;
    }

    QString bundleStr = (msg && len > 0) ? QString::fromUtf8(msg, len) : "";
    qDebug() << "ChatModulePlugin: IntroBundle:" << bundleStr;

    QVariantList eventData;
    eventData << (callerRet == RET_OK && !bundleStr.isEmpty());  // success
    eventData << callerRet;                                        // return code
    eventData << bundleStr;                                        // intro bundle string
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate);

    plugin->emitEvent("chatCreateIntroBundleResult", eventData);
}

// ============================================================================
// Client Lifecycle Methods
// ============================================================================

bool ChatModulePlugin::initChat(const QString &configJson)
{
    qDebug() << "ChatModulePlugin::initChat called with config:" << configJson;
    
    // Convert QString to UTF-8 byte array
    QByteArray cfgUtf8 = configJson.toUtf8();
    
    // Call chat_new with the configuration
    chatCtx = chat_new(cfgUtf8.constData(), init_callback, this);
    
    if (chatCtx) {
        qDebug() << "ChatModulePlugin: Chat context created successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to create Chat context";
        return false;
    }
}

bool ChatModulePlugin::startChat()
{
    qDebug() << "ChatModulePlugin::startChat called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot start Chat - context not initialized. Call initChat first.";
        return false;
    }
    
    int result = chat_start(chatCtx, start_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Chat start initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to start Chat, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::stopChat()
{
    qDebug() << "ChatModulePlugin::stopChat called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot stop Chat - context not initialized.";
        return false;
    }
    
    int result = chat_stop(chatCtx, stop_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Chat stop initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to stop Chat, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::destroyChat()
{
    qDebug() << "ChatModulePlugin::destroyChat called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot destroy Chat - context not initialized.";
        return false;
    }
    
    int result = chat_destroy(chatCtx, destroy_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Chat destroy initiated successfully";
        chatCtx = nullptr;
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to destroy Chat, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::setEventCallback()
{
    qDebug() << "ChatModulePlugin::setEventCallback called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot set event callback - context not initialized. Call initChat first.";
        return false;
    }
    
    set_event_callback(chatCtx, event_callback, this);
    
    qDebug() << "ChatModulePlugin: Event callback set successfully";
    return true;
}

// ============================================================================
// Client Info Methods
// ============================================================================

bool ChatModulePlugin::getId()
{
    qDebug() << "ChatModulePlugin::getId called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot get ID - context not initialized";
        return false;
    }
    
    int result = chat_get_id(chatCtx, get_id_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Get ID initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to get ID, error code:" << result;
        return false;
    }
}

// ============================================================================
// Conversation Operations
// ============================================================================

bool ChatModulePlugin::listConversations()
{
    qDebug() << "ChatModulePlugin::listConversations called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot list conversations - context not initialized";
        return false;
    }
    
    int result = chat_list_conversations(chatCtx, list_conversations_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: List conversations initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to list conversations, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::getConversation(const QString &convoId)
{
    qDebug() << "ChatModulePlugin::getConversation called with convoId:" << convoId;
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot get conversation - context not initialized";
        return false;
    }
    
    QByteArray convoIdUtf8 = convoId.toUtf8();
    
    int result = chat_get_conversation(chatCtx, get_conversation_callback, this, convoIdUtf8.constData());
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Get conversation initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to get conversation, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::newPrivateConversation(const QString &introBundleStr, const QString &contentHex)
{
    qDebug() << "ChatModulePlugin::newPrivateConversation called";

    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot create new private conversation - context not initialized";
        return false;
    }

    QByteArray introBundleUtf8 = introBundleStr.toUtf8();
    QByteArray contentUtf8 = contentHex.toUtf8();
    
    int result = chat_new_private_conversation(chatCtx, new_private_conversation_callback, this, introBundleUtf8.constData(), contentUtf8.constData());
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: New private conversation initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to create new private conversation, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::sendMessage(const QString &convoId, const QString &contentHex)
{
    qDebug() << "ChatModulePlugin::sendMessage called with convoId:" << convoId;
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot send message - context not initialized";
        return false;
    }
    
    QByteArray convoIdUtf8 = convoId.toUtf8();
    QByteArray contentUtf8 = contentHex.toUtf8();
    
    int result = chat_send_message(chatCtx, send_message_callback, this, convoIdUtf8.constData(), contentUtf8.constData());
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Send message initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to send message, error code:" << result;
        return false;
    }
}

// ============================================================================
// Identity Operations
// ============================================================================

bool ChatModulePlugin::getIdentity()
{
    qDebug() << "ChatModulePlugin::getIdentity called";
    
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot get identity - context not initialized";
        return false;
    }
    
    int result = chat_get_identity(chatCtx, get_identity_callback, this);
    
    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Get identity initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to get identity, error code:" << result;
        return false;
    }
}

bool ChatModulePlugin::createIntroBundle()
{
    qDebug() << "ChatModulePlugin::createIntroBundle called";

    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot create intro bundle - context not initialized";
        return false;
    }

    int result = chat_create_intro_bundle(chatCtx, create_intro_bundle_callback, this);

    if (result == RET_OK) {
        qDebug() << "ChatModulePlugin: Create intro bundle initiated successfully";
        return true;
    } else {
        qWarning() << "ChatModulePlugin: Failed to create intro bundle, error code:" << result;
        return false;
    }
}

// ============================================================================
// RLN Operations
// ============================================================================

int ChatModulePlugin::rln_fetcher(const char* method, const char* params,
    void (*callback)(int, const char*, size_t, void*), void* callbackData, void* fetcherData)
{
    auto* plugin = static_cast<ChatModulePlugin*>(fetcherData);
    if (!plugin || !plugin->logosAPI) {
        if (callback) callback(1, "LogosAPI not available", 21, callbackData);
        return 1;
    }

    if (QThread::currentThread() != plugin->thread()) {
        qDebug() << "rln_fetcher: marshalling to Qt thread for method" << method;
        int result = 1;
        QMetaObject::invokeMethod(plugin, [&]() {
            result = rln_fetcher(method, params, callback, callbackData, fetcherData);
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    auto* rlnClient = plugin->logosAPI->getClient("liblogos_rln_module");
    if (!rlnClient) {
        if (callback) callback(1, "RLN module not available", 24, callbackData);
        return 1;
    }

    QString methodStr(method);
    QString paramsStr(params);

    if (methodStr == "get_valid_roots") {
        QVariant result = rlnClient->invokeRemoteMethod(
            "liblogos_rln_module", "get_valid_roots", QVariant(paramsStr));
        QByteArray utf8 = result.toString().toUtf8();
        if (callback) callback(0, utf8.constData(), utf8.size(), callbackData);
        return 0;
    }

    if (methodStr == "get_merkle_proofs") {
        QStringList parts = paramsStr.split(",");
        if (parts.size() < 2) {
            if (callback) callback(1, "Expected configAccountId,leafIndex", 35, callbackData);
            return 1;
        }
        QString configAccount = parts[0];
        QString leafIndicesJson = "[" + parts[1] + "]";
        QVariant result = rlnClient->invokeRemoteMethod(
            "liblogos_rln_module", "get_merkle_proofs",
            QVariant(configAccount), QVariant(leafIndicesJson));
        QString proofsJson = result.toString();
        QJsonArray arr = QJsonDocument::fromJson(proofsJson.toUtf8()).array();
        if (arr.isEmpty()) {
            if (callback) callback(1, "Empty proof array", 17, callbackData);
            return 1;
        }
        QByteArray singleProof = QJsonDocument(arr[0].toObject()).toJson(QJsonDocument::Compact);
        if (callback) callback(0, singleProof.constData(), singleProof.size(), callbackData);
        return 0;
    }

    if (methodStr == "generate_identity") {
        QVariant result = rlnClient->invokeRemoteMethod(
            "liblogos_rln_module", "generate_identity", QVariant(paramsStr));
        QString resultJson = result.toString();
        if (resultJson.isEmpty()) {
            if (callback) callback(1, "generate_identity failed", 24, callbackData);
            return 1;
        }
        QByteArray utf8 = resultJson.toUtf8();
        if (callback) callback(0, utf8.constData(), utf8.size(), callbackData);
        return 0;
    }

    if (methodStr == "register_member") {
        QJsonDocument paramsDoc = QJsonDocument::fromJson(paramsStr.toUtf8());
        if (!paramsDoc.isObject()) {
            if (callback) callback(1, "Expected JSON object params", 27, callbackData);
            return 1;
        }
        QJsonObject paramsObj = paramsDoc.object();
        QString configAccountId = paramsObj["configAccountId"].toString();
        QString userHoldingAccountId = paramsObj["userHoldingAccountId"].toString();
        QString idCommitment = paramsObj["idCommitment"].toString();
        int rateLimit = paramsObj["rateLimit"].toInt(100);

        if (configAccountId.isEmpty() || userHoldingAccountId.isEmpty() || idCommitment.isEmpty()) {
            if (callback) callback(1, "Missing required params", 23, callbackData);
            return 1;
        }

        QVariant result = rlnClient->invokeRemoteMethod(
            "liblogos_rln_module", "register_member",
            QVariant(configAccountId),
            QVariant(userHoldingAccountId),
            QVariant(idCommitment),
            QVariant(rateLimit));
        QString resultJson = result.toString();
        if (resultJson.isEmpty()) {
            if (callback) callback(1, "register_member failed", 22, callbackData);
            return 1;
        }
        QByteArray utf8 = resultJson.toUtf8();
        if (callback) callback(0, utf8.constData(), utf8.size(), callbackData);
        return 0;
    }

    if (callback) callback(1, "Unknown method", 14, callbackData);
    return 1;
}

bool ChatModulePlugin::setRlnConfig(const QString& configAccountId, int leafIndex)
{
    if (!chatCtx) {
        qWarning() << "ChatModulePlugin: Cannot set RLN config - context not initialized";
        return false;
    }

    chat_set_rln_fetcher(chatCtx, rln_fetcher, this);
    chat_set_rln_config(chatCtx, configAccountId.toUtf8().constData(), leafIndex);

    if (logosAPI) {
        void* ctx = chatCtx;
        auto* api = logosAPI;
        QTimer::singleShot(15000, [ctx, api]() {
            auto* rlnConsumer = new LogosAPIConsumer("liblogos_rln_module", "chat_module",
                                                      api->getTokenManager());
            QObject* rlnReplica = rlnConsumer->requestObject("liblogos_rln_module");
            if (rlnReplica) {
                rlnConsumer->onEvent(rlnReplica, rlnConsumer, "valid_roots",
                    [ctx](const QString& eventName, const QVariantList& data) {
                        if (data.isEmpty()) return;
                        QByteArray utf8 = data[0].toString().toUtf8();
                        if (!utf8.isEmpty())
                            chat_push_roots(ctx, utf8.constData());
                    });
                rlnConsumer->onEvent(rlnReplica, rlnConsumer, "merkle_proof",
                    [ctx](const QString& eventName, const QVariantList& data) {
                        if (data.isEmpty()) return;
                        QByteArray utf8 = data[0].toString().toUtf8();
                        if (!utf8.isEmpty())
                            chat_push_proof(ctx, utf8.constData());
                    });
                qDebug() << "ChatModulePlugin: Subscribed to RLN module events";
            } else {
                qWarning() << "ChatModulePlugin: Could not get RLN module replica, retrying in 10s...";
                QTimer::singleShot(10000, [ctx, api]() {
                    auto* rlnConsumer2 = new LogosAPIConsumer("liblogos_rln_module", "chat_module",
                                                               api->getTokenManager());
                    QObject* rlnReplica2 = rlnConsumer2->requestObject("liblogos_rln_module");
                    if (rlnReplica2) {
                        rlnConsumer2->onEvent(rlnReplica2, rlnConsumer2, "valid_roots",
                            [ctx](const QString&, const QVariantList& data) {
                                if (data.isEmpty()) return;
                                QByteArray utf8 = data[0].toString().toUtf8();
                                if (!utf8.isEmpty()) chat_push_roots(ctx, utf8.constData());
                            });
                        rlnConsumer2->onEvent(rlnReplica2, rlnConsumer2, "merkle_proof",
                            [ctx](const QString&, const QVariantList& data) {
                                if (data.isEmpty()) return;
                                QByteArray utf8 = data[0].toString().toUtf8();
                                if (!utf8.isEmpty()) chat_push_proof(ctx, utf8.constData());
                            });
                        qDebug() << "ChatModulePlugin: Subscribed to RLN module events (retry)";
                    } else {
                        qWarning() << "ChatModulePlugin: Could not get RLN module replica after retry";
                    }
                });
            }
        });
    }

    qDebug() << "ChatModulePlugin: RLN config set, account:" << configAccountId << "leaf:" << leafIndex;
    return true;
}

QString ChatModulePlugin::selfRegisterRln(const QString& configAccountId,
                                           const QString& walletAccountId,
                                           int rateLimit)
{
    if (!logosAPI) {
        qWarning() << "selfRegisterRln: logosAPI not initialized";
        return {};
    }

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
        QVariant(idCommitment), QVariant(rateLimit));
    QString regJson = regResult.toString();
    if (regJson.isEmpty()) {
        qWarning() << "selfRegisterRln: register_member failed";
        return {};
    }

    QJsonDocument regDoc = QJsonDocument::fromJson(regJson.toUtf8());
    int leafIndex = static_cast<int>(regDoc.object()["leaf_index"].toDouble());
    qDebug() << "selfRegisterRln: registered at leaf" << leafIndex;

    if (!setRlnConfig(configAccountId, leafIndex)) {
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
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
