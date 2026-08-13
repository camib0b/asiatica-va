#pragma once

#include <QObject>
#include <QString>

#include <functional>

class YouTubeAuthManager final : public QObject {
    Q_OBJECT

public:
    explicit YouTubeAuthManager(QObject* parent = nullptr);

    bool isAuthenticated() const;
    QString channelTitle() const;
    QString channelId() const;

    /// Returns a valid access token, refreshing when needed. Empty on failure.
    void requestAccessToken(const std::function<void(const QString& token, const QString& error)>& callback);

    void startSignIn();
    void signOut();

signals:
    void authStateChanged();
    void authError(const QString& message);

private:
    void exchangeAuthorizationCode(const QString& authorizationCode,
                                   const QString& codeVerifier,
                                   const QString& redirectUri);
    void refreshAccessToken(const std::function<void(const QString& token, const QString& error)>& callback);
    void fetchChannelInfo();
    void persistTokens();
    void loadTokens();
    void clearStoredTokens();

    QString accessToken_;
    QString refreshToken_;
    qint64 accessTokenExpiryEpochSec_ = 0;
    QString channelTitle_;
    QString channelId_;
};
