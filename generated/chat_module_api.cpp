#include "chat_module_api.h"

#include <QDebug>

ChatModule::ChatModule(LogosAPI* api) : m_api(api), m_client(api->getClient("chat_module")), m_moduleName(QStringLiteral("chat_module")) {}

QObject* ChatModule::ensureReplica() {
    if (!m_eventReplica) {
        QObject* replica = m_client->requestObject(m_moduleName);
        if (!replica) {
            qWarning() << "ChatModule: failed to acquire remote object for events on" << m_moduleName;
            return nullptr;
        }
        m_eventReplica = replica;
    }
    return m_eventReplica.data();
}

bool ChatModule::on(const QString& eventName, RawEventCallback callback) {
    if (!callback) {
        qWarning() << "ChatModule: ignoring empty event callback for" << eventName;
        return false;
    }
    QObject* origin = ensureReplica();
    if (!origin) {
        return false;
    }
    m_client->onEvent(origin, nullptr, eventName, callback);
    return true;
}

bool ChatModule::on(const QString& eventName, EventCallback callback) {
    if (!callback) {
        qWarning() << "ChatModule: ignoring empty event callback for" << eventName;
        return false;
    }
    return on(eventName, [callback](const QString&, const QVariantList& data) {
        callback(data);
    });
}

void ChatModule::setEventSource(QObject* source) {
    m_eventSource = source;
}

QObject* ChatModule::eventSource() const {
    return m_eventSource.data();
}

void ChatModule::trigger(const QString& eventName) {
    trigger(eventName, QVariantList{});
}

void ChatModule::trigger(const QString& eventName, const QVariantList& data) {
    if (!m_eventSource) {
        qWarning() << "ChatModule: no event source set for trigger" << eventName;
        return;
    }
    m_client->onEventResponse(m_eventSource.data(), eventName, data);
}

void ChatModule::trigger(const QString& eventName, QObject* source, const QVariantList& data) {
    if (!source) {
        qWarning() << "ChatModule: cannot trigger" << eventName << "with null source";
        return;
    }
    m_client->onEventResponse(source, eventName, data);
}

bool ChatModule::initChat(const QString& configJson) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "initChat", configJson);
    return _result.toBool();
}

bool ChatModule::startChat() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "startChat");
    return _result.toBool();
}

bool ChatModule::stopChat() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "stopChat");
    return _result.toBool();
}

bool ChatModule::destroyChat() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "destroyChat");
    return _result.toBool();
}

bool ChatModule::setEventCallback() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "setEventCallback");
    return _result.toBool();
}

bool ChatModule::getId() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "getId");
    return _result.toBool();
}

bool ChatModule::listConversations() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "listConversations");
    return _result.toBool();
}

bool ChatModule::getConversation(const QString& convoId) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "getConversation", convoId);
    return _result.toBool();
}

bool ChatModule::newPrivateConversation(const QString& introBundleStr, const QString& contentHex) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "newPrivateConversation", introBundleStr, contentHex);
    return _result.toBool();
}

bool ChatModule::sendMessage(const QString& convoId, const QString& contentHex) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "sendMessage", convoId, contentHex);
    return _result.toBool();
}

bool ChatModule::getIdentity() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "getIdentity");
    return _result.toBool();
}

bool ChatModule::createIntroBundle() {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "createIntroBundle");
    return _result.toBool();
}

bool ChatModule::setRlnConfig(const QString& configAccountId, int leafIndex) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "setRlnConfig", configAccountId, leafIndex);
    return _result.toBool();
}

QString ChatModule::selfRegisterRln(const QString& configAccountId, const QString& walletAccountId, int rateLimit) {
    QVariant _result = m_client->invokeRemoteMethod("chat_module", "selfRegisterRln", configAccountId, walletAccountId, rateLimit);
    return _result.toString();
}

void ChatModule::initLogos(QVariant logosAPIInstance) {
    m_client->invokeRemoteMethod("chat_module", "initLogos", logosAPIInstance);
}

