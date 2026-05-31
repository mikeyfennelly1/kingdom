#include "MainWindow.hh"

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Stylesheet constants
// ---------------------------------------------------------------------------

static const char* kSidebarStyle =
    "background-color: #1e293b;"
    "border-right: 1px solid #334155;";

static const char* kConvListStyle =
    "QListWidget {"
    "  background: transparent;"
    "  border: none;"
    "  color: #cbd5e1;"
    "  font-size: 14px;"
    "  outline: none;"
    "}"
    "QListWidget::item {"
    "  padding: 10px 14px;"
    "  border-radius: 6px;"
    "  margin: 1px 6px;"
    "}"
    "QListWidget::item:selected {"
    "  background-color: #334155;"
    "  color: white;"
    "}"
    "QListWidget::item:hover:!selected {"
    "  background-color: #273449;"
    "}";

static const char* kNewConvButtonStyle =
    "QPushButton {"
    "  background-color: #2563eb;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 6px;"
    "  padding: 8px 14px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  margin: 4px 8px;"
    "}"
    "QPushButton:hover { background-color: #1d4ed8; }"
    "QPushButton:pressed { background-color: #1e40af; }";

static const char* kLogoutButtonStyle =
    "QPushButton {"
    "  background: transparent;"
    "  color: #94a3b8;"
    "  border: 1px solid #334155;"
    "  border-radius: 6px;"
    "  padding: 6px 14px;"
    "  font-size: 12px;"
    "  margin: 4px 8px 8px 8px;"
    "}"
    "QPushButton:hover { color: #e2e8f0; border-color: #475569; }";

static const char* kSendButtonStyle =
    "QPushButton {"
    "  background-color: #2563eb;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 20px;"
    "  padding: 8px 22px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background-color: #1d4ed8; }"
    "QPushButton:disabled { background-color: #94a3b8; }";

static const char* kInputStyle =
    "QLineEdit {"
    "  border: 1px solid #e2e8f0;"
    "  border-radius: 20px;"
    "  padding: 8px 16px;"
    "  font-size: 13px;"
    "  background: white;"
    "  color: #1e293b;"
    "}"
    "QLineEdit:focus { border-color: #2563eb; }";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow(Session session, QWidget* parent)
    : QMainWindow(parent),
      session_(std::move(session)),
      client_(std::make_unique<kd::Client>(
          session_.serverUrl,
          !session_.caCertPath.empty()
              ? session_.caCertPath
              : [] {
                  const char* e = std::getenv("KD_CA_CERT");
                  return e != nullptr ? std::string(e) : std::string{};
                }())),
      messageStore_(std::move(session_.messageStore)) {
  client_->setAuthToken(session_.token);
  loadUserCache();

  setWindowTitle("Kingdom");
  setMinimumSize(860, 600);

  // ---- Left sidebar ----
  auto* appTitle = new QLabel("Kingdom", this);
  appTitle->setStyleSheet(
      "color: white; font-size: 18px; font-weight: bold; padding: 16px 16px 4px 16px;");

  usernameLabel_ = new QLabel(
      QString("@%1").arg(QString::fromUtf8(session_.username.c_str())), this);
  usernameLabel_->setStyleSheet(
      "color: #94a3b8; font-size: 12px; padding: 0 16px 12px 16px;");

  auto* convHeader = new QLabel("Conversations", this);
  convHeader->setStyleSheet(
      "color: #64748b; font-size: 11px; font-weight: bold; "
      "padding: 8px 16px 4px 16px; letter-spacing: 1px;");

  conversationList_ = new QListWidget(this);
  conversationList_->setStyleSheet(kConvListStyle);

  newConvButton_ = new QPushButton("+ New Conversation", this);
  newConvButton_->setStyleSheet(kNewConvButtonStyle);
  newConvButton_->setCursor(Qt::PointingHandCursor);

  logoutButton_ = new QPushButton("Log Out", this);
  logoutButton_->setStyleSheet(kLogoutButtonStyle);
  logoutButton_->setCursor(Qt::PointingHandCursor);

  auto* leftLayout = new QVBoxLayout();
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);
  leftLayout->addWidget(appTitle);
  leftLayout->addWidget(usernameLabel_);
  leftLayout->addWidget(convHeader);
  leftLayout->addWidget(conversationList_);
  leftLayout->addWidget(newConvButton_);
  leftLayout->addWidget(logoutButton_);

  auto* leftWidget = new QWidget(this);
  leftWidget->setLayout(leftLayout);
  leftWidget->setStyleSheet(kSidebarStyle);
  leftWidget->setFixedWidth(240);

  // ---- Right panel ----
  conversationLabel_ = new QLabel("Select a conversation", this);
  conversationLabel_->setStyleSheet(
      "font-size: 15px; font-weight: bold; color: #1e293b; "
      "padding: 14px 18px; border-bottom: 1px solid #f1f5f9;");

  messageView_ = new QTextEdit(this);
  messageView_->setReadOnly(true);
  messageView_->setStyleSheet(
      "QTextEdit {"
      "  border: none;"
      "  padding: 8px 12px;"
      "  background: #f8fafc;"
      "  font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif;"
      "  font-size: 13px;"
      "}");

  messageInput_ = new QLineEdit(this);
  messageInput_->setPlaceholderText("Type a message...");
  messageInput_->setEnabled(false);
  messageInput_->setStyleSheet(kInputStyle);

  sendButton_ = new QPushButton("Send", this);
  sendButton_->setEnabled(false);
  sendButton_->setStyleSheet(kSendButtonStyle);
  sendButton_->setCursor(Qt::PointingHandCursor);

  auto* inputRow = new QHBoxLayout();
  inputRow->setContentsMargins(12, 8, 12, 12);
  inputRow->setSpacing(8);
  inputRow->addWidget(messageInput_);
  inputRow->addWidget(sendButton_);

  auto* rightLayout = new QVBoxLayout();
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);
  rightLayout->addWidget(conversationLabel_);
  rightLayout->addWidget(messageView_);
  rightLayout->addLayout(inputRow);

  auto* rightWidget = new QWidget(this);
  rightWidget->setLayout(rightLayout);
  rightWidget->setStyleSheet("background: white; color: #1e293b;");

  // ---- Splitter ----
  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->addWidget(leftWidget);
  splitter->addWidget(rightWidget);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setHandleWidth(0);
  setCentralWidget(splitter);

  // ---- Connections ----
  connect(conversationList_, &QListWidget::itemClicked, this,
          &MainWindow::onConversationSelected);
  connect(newConvButton_, &QPushButton::clicked, this, &MainWindow::onNewConversation);
  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSend);
  connect(logoutButton_, &QPushButton::clicked, this, &MainWindow::onLogout);
  connect(messageInput_, &QLineEdit::returnPressed, this, &MainWindow::onSend);

  // ---- Polling ----
  pollTimer_ = new QTimer(this);
  pollTimer_->setInterval(5000);
  connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollMessages);
  pollTimer_->start();

  loadConversations();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// User cache
// ---------------------------------------------------------------------------

void MainWindow::loadUserCache() {
  try {
    auto usersJson = client_->getUsers();
    for (const auto& u : usersJson) {
      uint64_t uid = u["id"].get<uint64_t>();
      std::string uname = u["username"].get<std::string>();
      userCache_[uid] = uname;
    }
  } catch (const std::exception& e) {
    spdlog::warn("Failed to load user cache: {}", e.what());
  }
}

std::string MainWindow::usernameFor(uint64_t userId) const {
  auto it = userCache_.find(userId);
  if (it != userCache_.end()) {
    return it->second;
  }
  return "User " + std::to_string(userId);
}

// ---------------------------------------------------------------------------
// Conversations
// ---------------------------------------------------------------------------

void MainWindow::loadConversations() {
  try {
    const auto selectedConversationId = activeConversationId_;
    auto json = client_->getConversations(session_.userId);
    conversations_.clear();
    conversationList_->clear();
    QListWidgetItem* selectedItem = nullptr;

    for (const auto& item : json) {
      auto conv = item.get<kd::Conversation>();
      conversations_.push_back(conv);

      auto* listItem =
          new QListWidgetItem(QString::fromUtf8(conv.name.c_str()), conversationList_);
      listItem->setData(Qt::UserRole, static_cast<qulonglong>(conv.id));
      if (selectedConversationId.has_value() && conv.id == *selectedConversationId) {
        selectedItem = listItem;
      }
    }

    if (selectedConversationId.has_value()) {
      if (selectedItem != nullptr) {
        conversationList_->setCurrentItem(selectedItem);
      } else {
        activeConversationId_ = std::nullopt;
        activeRecipientId_ = std::nullopt;
        conversationLabel_->setText("Select a conversation");
        messageView_->clear();
        messageInput_->setEnabled(false);
        sendButton_->setEnabled(false);
      }
    }
  } catch (const std::exception& e) {
    spdlog::warn("Failed to load conversations: {}", e.what());
  }
}

void MainWindow::onConversationSelected(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }

  const uint64_t convId = item->data(Qt::UserRole).toULongLong();
  activeConversationId_ = convId;
  activeRecipientId_ = std::nullopt;

  for (const auto& conv : conversations_) {
    if (conv.id == convId) {
      for (uint64_t pid : conv.participantIds) {
        if (pid != session_.userId) {
          activeRecipientId_ = pid;
          break;
        }
      }
      conversationLabel_->setText(QString::fromUtf8(conv.name.c_str()));
      break;
    }
  }

  messageInput_->setEnabled(true);
  sendButton_->setEnabled(true);
  loadMessages(convId);
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

QString MainWindow::formatTimestamp(uint64_t milliseconds) {
  const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  std::tm* tmInfo = std::localtime(&seconds);
  if (tmInfo == nullptr) {
    return QString::number(static_cast<qulonglong>(milliseconds));
  }
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M", tmInfo);
  return QString(buf);
}

void MainWindow::appendMessageToView(const std::string& sender, const std::string& text,
                                     uint64_t timestamp) {
  const QString ts = formatTimestamp(timestamp);
  const bool isMe =
      (sender == session_.username || sender == std::to_string(session_.userId));
  const QString escaped = QString::fromUtf8(text.c_str()).toHtmlEscaped();

  if (isMe) {
    messageView_->append(
        QString("<p align='right' style='margin:6px 0; color:#1e293b;'>"
                "<small style='color:#94a3b8;'>%1</small><br>"
                "<span style='background-color:#dbeafe; color:#1d4ed8; "
                "padding:6px 10px;'>%2</span>"
                "</p>")
            .arg(ts, escaped));
  } else {
    messageView_->append(
        QString("<p align='left' style='margin:6px 0;'>"
                "<small style='color:#64748b;'><b>%1</b> &nbsp;%2</small><br>"
                "<span style='background-color:#f1f5f9; color:#1e293b; "
                "padding:6px 10px;'>%3</span>"
                "</p>")
            .arg(QString::fromUtf8(sender.c_str()), ts, escaped));
  }
}

std::string MainWindow::decryptOrPlaceholder(const kd::Message& msg, uint64_t recipientId) {
  if (auto cached = messageStore_.getPlaintext(msg.id); cached.has_value()) {
    return *cached;
  }

  try {
    auto cachedPk = messageStore_.getCachedPublicKey(msg.senderId);
    std::string senderPk =
        cachedPk.has_value() ? *cachedPk : client_->getPublicKey(msg.senderId);
    messageStore_.cachePublicKey(msg.senderId, senderPk);

    std::string plaintext =
        kd::LocalKeyStore::decryptMessage(msg.payload, session_.identityKey, senderPk,
                                          msg.conversationId, msg.senderId, recipientId);
    messageStore_.savePlaintext(msg.id, msg.conversationId, msg.senderId, msg.timestamp, plaintext);
    return plaintext;
  } catch (const std::exception& e) {
    spdlog::warn("Decryption failed for message {}: {}", msg.id, e.what());
    return "[decryption failed]";
  }
}

void MainWindow::loadMessages(uint64_t conversationId) {
  messageView_->clear();

  try {
    auto msgs = client_->getMessages(conversationId);

    std::sort(msgs.begin(), msgs.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
      return a["timestamp"].get<uint64_t>() < b["timestamp"].get<uint64_t>();
    });

    // outgoingRecipient: used as AAD recipientId when I am the sender.
    const uint64_t outgoingRecipient =
        activeRecipientId_.has_value() ? *activeRecipientId_ : session_.userId;

    for (const auto& msgJson : msgs) {
      auto msg = msgJson.get<kd::Message>();

      std::string senderLabel;
      if (msg.senderId == session_.userId) {
        senderLabel = session_.username;
      } else {
        senderLabel = usernameFor(msg.senderId);
      }

      std::string text;
      if (msg.senderId == session_.userId) {
        // Outgoing: I encrypted this with recipientId = the other person.
        if (auto cached = messageStore_.getPlaintext(msg.id); cached.has_value()) {
          text = *cached;
        } else {
          text = decryptOrPlaceholder(msg, outgoingRecipient);
        }
      } else {
        // Incoming: sender encrypted with recipientId = me (session_.userId).
        text = decryptOrPlaceholder(msg, session_.userId);
      }

      appendMessageToView(senderLabel, text, msg.timestamp);
    }

    auto* bar = messageView_->verticalScrollBar();
    bar->setValue(bar->maximum());

  } catch (const std::exception& e) {
    spdlog::warn("Failed to load messages for conversation {}: {}", conversationId, e.what());
    messageView_->setPlainText(
        QString("Failed to load messages: %1").arg(QString::fromUtf8(e.what())));
  }
}

// ---------------------------------------------------------------------------
// New conversation dialog
// ---------------------------------------------------------------------------

void MainWindow::onNewConversation() {
  // Fetch current user list from server.
  nlohmann::json usersJson;
  try {
    usersJson = client_->getUsers();
  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Error",
                          QString("Could not load users: %1").arg(QString::fromUtf8(e.what())));
    return;
  }

  // Build list excluding self.
  struct UserEntry {
    uint64_t id;
    std::string username;
  };
  std::vector<UserEntry> others;
  for (const auto& u : usersJson) {
    uint64_t uid = u["id"].get<uint64_t>();
    if (uid == session_.userId) {
      continue;
    }
    others.push_back({uid, u["username"].get<std::string>()});
  }

  if (others.empty()) {
    QMessageBox::information(this, "No Users", "No other users are registered yet.");
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("New Conversation");
  dialog.setMinimumWidth(340);

  auto* userCombo = new QComboBox(&dialog);
  for (const auto& u : others) {
    userCombo->addItem(QString::fromUtf8(u.username.c_str()),
                       static_cast<qulonglong>(u.id));
  }

  auto* nameEdit = new QLineEdit(&dialog);
  // Pre-fill name based on selected user.
  auto updateName = [&]() {
    const std::string uname{userCombo->currentText().toUtf8().constData()};
    nameEdit->setText(QString("Chat with %1").arg(QString::fromUtf8(uname.c_str())));
  };
  updateName();
  connect(userCombo, &QComboBox::currentTextChanged, [&](const QString&) { updateName(); });

  auto* okButton = new QPushButton("Create", &dialog);
  okButton->setDefault(true);
  auto* cancelButton = new QPushButton("Cancel", &dialog);

  auto* form = new QFormLayout();
  form->addRow("User:", userCombo);
  form->addRow("Conversation name:", nameEdit);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch();
  buttonRow->addWidget(cancelButton);
  buttonRow->addWidget(okButton);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addLayout(buttonRow);

  connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
  connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const std::string convName{nameEdit->text().trimmed().toUtf8().constData()};
  const uint64_t recipientId =
      userCombo->itemData(userCombo->currentIndex()).toULongLong();

  if (convName.empty()) {
    QMessageBox::warning(this, "Invalid Input", "Conversation name cannot be empty.");
    return;
  }

  try {
    std::vector<uint64_t> participants = {session_.userId, recipientId};
    client_->createConversation(convName, participants);
    loadConversations();
  } catch (const std::exception& e) {
    QMessageBox::critical(
        this, "Error",
        QString("Failed to create conversation: %1").arg(QString::fromUtf8(e.what())));
  }
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------

void MainWindow::onSend() {
  if (!activeConversationId_.has_value()) {
    return;
  }

  const std::string text{messageInput_->text().trimmed().toUtf8().constData()};
  if (text.empty()) {
    return;
  }

  if (!activeRecipientId_.has_value()) {
    QMessageBox::warning(this, "Cannot Send",
                         "Could not determine recipient. Select a conversation.");
    return;
  }

  const uint64_t convId = *activeConversationId_;
  const uint64_t recipientId = *activeRecipientId_;

  try {
    const std::string recipientPk = client_->getPublicKey(recipientId);
    const std::string payload = kd::LocalKeyStore::encryptMessage(
        text, session_.identityKey, recipientPk, convId, session_.userId, recipientId);

    auto sentJson = client_->sendMessage(convId, session_.userId, recipientId, payload);
    auto sentMsg = sentJson.get<kd::Message>();

    messageStore_.savePlaintext(sentMsg.id, sentMsg.conversationId, sentMsg.senderId,
                                sentMsg.timestamp, text);

    appendMessageToView(session_.username, text, sentMsg.timestamp);
    auto* bar = messageView_->verticalScrollBar();
    bar->setValue(bar->maximum());
    messageInput_->clear();

  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Send Failed", QString::fromUtf8(e.what()));
  }
}

// ---------------------------------------------------------------------------
// Logout / Poll
// ---------------------------------------------------------------------------

void MainWindow::onLogout() {
  pollTimer_->stop();
  try {
    client_->logout();
  } catch (const std::exception& e) {
    spdlog::warn("Logout request failed: {}", e.what());
  }
  client_->clearAuthToken();
  emit loggedOut();
  close();
}

void MainWindow::pollMessages() {
  loadConversations();
  if (!activeConversationId_.has_value()) {
    return;
  }
  loadMessages(*activeConversationId_);
}
