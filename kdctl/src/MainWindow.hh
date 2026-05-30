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
class QTextEdit;

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
  };

  explicit MainWindow(Session session, QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void loggedOut();

 private slots:
  void onConversationSelected(QListWidgetItem* item);
  void onNewConversation();
  void onSend();
  void onLogout();
  void pollMessages();

 private:
  void loadConversations();
  void loadMessages(uint64_t conversationId);
  void loadUserCache();
  void appendMessageToView(const std::string& sender, const std::string& text, uint64_t timestamp);
  std::string decryptOrPlaceholder(const kd::Message& msg, uint64_t recipientId);
  QString formatTimestamp(uint64_t milliseconds);
  std::string usernameFor(uint64_t userId) const;

  // Session state
  Session session_;
  std::unique_ptr<kd::Client> client_;
  kd::MessageStore messageStore_;
  std::vector<kd::Conversation> conversations_;
  std::optional<uint64_t> activeConversationId_;
  std::optional<uint64_t> activeRecipientId_;
  std::map<uint64_t, std::string> userCache_;

  // Left panel
  QListWidget* conversationList_;
  QPushButton* newConvButton_;
  QLabel* usernameLabel_;
  QPushButton* logoutButton_;

  // Right panel
  QTextEdit* messageView_;
  QLineEdit* messageInput_;
  QPushButton* sendButton_;
  QLabel* conversationLabel_;

  // Polling
  QTimer* pollTimer_;
};
