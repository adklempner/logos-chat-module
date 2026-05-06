#pragma once
#include <QString>
#include <QVariant>
#include <QStringList>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <functional>
#include <utility>
#include "logos_types.h"
#include "logos_api.h"
#include "logos_api_client.h"

class ChatModule {
public:
    explicit ChatModule(LogosAPI* api);

    using RawEventCallback = std::function<void(const QString&, const QVariantList&)>;
    using EventCallback = std::function<void(const QVariantList&)>;

    bool on(const QString& eventName, RawEventCallback callback);
    bool on(const QString& eventName, EventCallback callback);
    void setEventSource(QObject* source);
    QObject* eventSource() const;
    void trigger(const QString& eventName);
    void trigger(const QString& eventName, const QVariantList& data);
    template<typename... Args>
    void trigger(const QString& eventName, Args&&... args) {
        trigger(eventName, packVariantList(std::forward<Args>(args)...));
    }
    void trigger(const QString& eventName, QObject* source, const QVariantList& data);
    template<typename... Args>
    void trigger(const QString& eventName, QObject* source, Args&&... args) {
        trigger(eventName, source, packVariantList(std::forward<Args>(args)...));
    }

    bool initChat(const QString& configJson);
    bool startChat();
    bool stopChat();
    bool destroyChat();
    bool setEventCallback();
    bool getId();
    bool listConversations();
    bool getConversation(const QString& convoId);
    bool newPrivateConversation(const QString& introBundleStr, const QString& contentHex);
    bool sendMessage(const QString& convoId, const QString& contentHex);
    bool getIdentity();
    bool createIntroBundle();
    bool setRlnConfig(const QString& configAccountId, int leafIndex);
    QString selfRegisterRln(const QString& configAccountId, const QString& walletAccountId, int rateLimit);
    void initLogos(QVariant logosAPIInstance);

private:
    QObject* ensureReplica();
    template<typename... Args>
    static QVariantList packVariantList(Args&&... args) {
        QVariantList list;
        list.reserve(sizeof...(Args));
        using Expander = int[];
        (void)Expander{0, (list.append(QVariant::fromValue(std::forward<Args>(args))), 0)...};
        return list;
    }
    LogosAPI* m_api;
    LogosAPIClient* m_client;
    QString m_moduleName;
    QPointer<QObject> m_eventReplica;
    QPointer<QObject> m_eventSource;
};
