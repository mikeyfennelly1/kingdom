#pragma once

#include <QMainWindow>
#include <QTimer>
#include <cstdint>
#include <kd/Client.hpp>
#include <kd/Conversation.hpp>
#include <kd/LocalKeyStore.hpp>
#include <kd/Message.hpp>
#include <kd/MessageStore.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  struct Session {
    uint64_t userId;
    std::string username;
    std::string token;
    kd::LocalIdentityKey identityKey;
    kd::MessageStore messageStore;
    std::string serverUrl;
    std::string caCertPath;
  };

  explicit MainWindow(Session session, QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void loggedOut();

 private slots:
  void onConversationSelected(QListWidgetItem* item);
  void onNewConversation();
  void onSend();
  void onForward();
  void onDelete();
  void onRevoke();
  void onMessageSelectionChanged();
  void onLogout();
  void pollMessages();

  // NOLINTNEXTLINE(readability-redundant-access-specifiers)
 private:
  struct DisplayedMessage {
    kd::Message message;
    std::string plaintext;
    bool forwardable;
  };

  struct ForwardTarget {
    uint64_t userId;
    uint64_t conversationId;
    std::string username;
    std::string publicKey;
  };

  void loadConversations();
  void loadMessages(uint64_t conversationId);
  void loadUserCache();
  void appendMessageToView(const kd::Message& message, const std::string& sender,
                           const std::string& text, bool forwardable);
  std::optional<std::string> decryptPlaintext(const kd::Message& msg, uint64_t recipientId);
  std::optional<ForwardTarget> chooseForwardTarget();
  [[nodiscard]] std::optional<uint64_t> findConversationWithUser(uint64_t userId) const;
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  bool confirmRecipientIdentity(uint64_t userId, const std::string& username,
                                const std::string& publicKey);
  [[nodiscard]] static QString formatTimestamp(uint64_t milliseconds);
  [[nodiscard]] static QString fingerprintForPublicKey(const std::string& publicKey);
  [[nodiscard]] std::string usernameFor(uint64_t userId) const;

  // Session state
  Session session_;
  std::unique_ptr<kd::Client> client_;
  kd::MessageStore messageStore_;
  std::vector<kd::Conversation> conversations_;
  std::optional<uint64_t> activeConversationId_;
  std::optional<uint64_t> activeRecipientId_;
  std::map<uint64_t, std::string> userCache_;
  std::vector<DisplayedMessage> visibleMessages_;

  // Left panel
  QListWidget* conversationList_;
  QPushButton* newConvButton_;
  QLabel* usernameLabel_;
  QPushButton* logoutButton_;

  // Right panel
  QListWidget* messageList_;
  QLineEdit* messageInput_;
  QPushButton* sendButton_;
  QPushButton* forwardButton_;
  QPushButton* deleteButton_;
  QPushButton* revokeButton_;
  QLabel* conversationLabel_;

  // Polling
  QTimer* pollTimer_;
};
