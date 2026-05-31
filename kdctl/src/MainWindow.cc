#include "MainWindow.hh"

#include <spdlog/spdlog.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QCryptographicHash>
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
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
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

static const char* kDangerButtonStyle =
    "QPushButton {"
    "  background-color: #dc2626;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 20px;"
    "  padding: 8px 22px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background-color: #b91c1c; }"
    "QPushButton:disabled { background-color: #94a3b8; }";

static const char* kWarnButtonStyle =
    "QPushButton {"
    "  background-color: #d97706;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 20px;"
    "  padding: 8px 22px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background-color: #b45309; }"
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
// Layout / timing constants
// ---------------------------------------------------------------------------

static constexpr int kWindowMinWidth = 860;
static constexpr int kWindowMinHeight = 600;
static constexpr int kSidebarWidth = 240;
static constexpr int kPollIntervalMs = 5000;
static constexpr int kNewConvDialogMinWidth = 340;
static constexpr int kForwardDialogMinWidth = 360;
static constexpr std::size_t kTimestampBufLen = 32;

static const char* kMessageListStyle =
    "QListWidget {"
    "  border: none;"
    "  padding: 8px 12px;"
    "  background: #f8fafc;"
    "  font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif;"
    "  font-size: 13px;"
    "  outline: none;"
    "}"
    "QListWidget::item {"
    "  background: #f1f5f9;"
    "  color: #1e293b;"
    "  border-radius: 6px;"
    "  margin: 4px 0;"
    "  padding: 8px 10px;"
    "}"
    "QListWidget::item:selected {"
    "  background: #dbeafe;"
    "  color: #1d4ed8;"
    "}";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow(Session session, QWidget* parent)
    : QMainWindow(parent)
    , session_(std::move(session))
    , client_(std::make_unique<kd::Client>(session_.serverUrl, session_.caCertPath))
    , messageStore_(std::move(session_.messageStore)) {
  client_->setAuthToken(session_.token);
  loadUserCache();

  setWindowTitle("Kingdom");
  setMinimumSize(kWindowMinWidth, kWindowMinHeight);

  // ---- Left sidebar ----
  auto* appTitle = new QLabel("Kingdom", this);
  appTitle->setStyleSheet(
      "color: white; font-size: 18px; font-weight: bold; padding: 16px 16px 4px 16px;");

  usernameLabel_ =
      new QLabel(QString("@%1").arg(QString::fromUtf8(session_.username.c_str())), this);
  usernameLabel_->setStyleSheet("color: #94a3b8; font-size: 12px; padding: 0 16px 12px 16px;");

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
  leftWidget->setFixedWidth(kSidebarWidth);

  // ---- Right panel ----
  conversationLabel_ = new QLabel("Select a conversation", this);
  conversationLabel_->setStyleSheet(
      "font-size: 15px; font-weight: bold; color: #1e293b; "
      "padding: 14px 18px; border-bottom: 1px solid #f1f5f9;");

  messageList_ = new QListWidget(this);
  messageList_->setSelectionMode(QAbstractItemView::SingleSelection);
  messageList_->setStyleSheet(kMessageListStyle);

  messageInput_ = new QLineEdit(this);
  messageInput_->setPlaceholderText("Type a message...");
  messageInput_->setEnabled(false);
  messageInput_->setStyleSheet(kInputStyle);

  sendButton_ = new QPushButton("Send", this);
  sendButton_->setEnabled(false);
  sendButton_->setStyleSheet(kSendButtonStyle);
  sendButton_->setCursor(Qt::PointingHandCursor);

  forwardButton_ = new QPushButton("Forward", this);
  forwardButton_->setEnabled(false);
  forwardButton_->setStyleSheet(kSendButtonStyle);
  forwardButton_->setCursor(Qt::PointingHandCursor);

  deleteButton_ = new QPushButton("Delete", this);
  deleteButton_->setEnabled(false);
  deleteButton_->setStyleSheet(kDangerButtonStyle);
  deleteButton_->setCursor(Qt::PointingHandCursor);

  revokeButton_ = new QPushButton("Revoke", this);
  revokeButton_->setEnabled(false);
  revokeButton_->setStyleSheet(kWarnButtonStyle);
  revokeButton_->setCursor(Qt::PointingHandCursor);

  auto* inputRow = new QHBoxLayout();
  inputRow->setContentsMargins(12, 8, 12, 12);  // NOLINT(readability-magic-numbers)
  inputRow->setSpacing(8);                      // NOLINT(readability-magic-numbers)
  inputRow->addWidget(messageInput_);
  inputRow->addWidget(deleteButton_);
  inputRow->addWidget(revokeButton_);
  inputRow->addWidget(forwardButton_);
  inputRow->addWidget(sendButton_);

  auto* rightLayout = new QVBoxLayout();
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(0);
  rightLayout->addWidget(conversationLabel_);
  rightLayout->addWidget(messageList_);
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
  connect(conversationList_, &QListWidget::itemClicked, this, &MainWindow::onConversationSelected);
  connect(newConvButton_, &QPushButton::clicked, this, &MainWindow::onNewConversation);
  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSend);
  connect(forwardButton_, &QPushButton::clicked, this, &MainWindow::onForward);
  connect(deleteButton_, &QPushButton::clicked, this, &MainWindow::onDelete);
  connect(revokeButton_, &QPushButton::clicked, this, &MainWindow::onRevoke);
  connect(logoutButton_, &QPushButton::clicked, this, &MainWindow::onLogout);
  connect(messageInput_, &QLineEdit::returnPressed, this, &MainWindow::onSend);
  connect(messageList_, &QListWidget::currentItemChanged, this,
          &MainWindow::onMessageSelectionChanged);

  // ---- Polling ----
  pollTimer_ = new QTimer(this);
  pollTimer_->setInterval(kPollIntervalMs);
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
    for (const auto& user : usersJson) {
      uint64_t uid = user["id"].get<uint64_t>();
      std::string uname = user["username"].get<std::string>();
      userCache_[uid] = uname;
    }
  } catch (const std::exception& e) {
    spdlog::warn("Failed to load user cache: {}", e.what());
  }
}

std::string MainWindow::usernameFor(uint64_t userId) const {
  auto iter = userCache_.find(userId);
  if (iter != userCache_.end()) {
    return iter->second;
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

      auto* listItem = new QListWidgetItem(QString::fromUtf8(conv.name.c_str()), conversationList_);
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
        messageList_->clear();
        messageInput_->setEnabled(false);
        sendButton_->setEnabled(false);
        forwardButton_->setEnabled(false);
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
  char buf[kTimestampBufLen];  // NOLINT(modernize-avoid-c-arrays)
  std::strftime(buf, sizeof(buf), "%H:%M", tmInfo);
  return {buf};
}

void MainWindow::appendMessageToView(const kd::Message& message, const std::string& sender,
                                     const std::string& text, bool forwardable) {
  const QString timeStr = formatTimestamp(message.timestamp);
  const bool isMe = (sender == session_.username || sender == std::to_string(session_.userId));
  const QString label = isMe ? QString("You") : QString::fromUtf8(sender.c_str());
  auto* item = new QListWidgetItem(
      QString("%1  %2\n%3").arg(label, timeStr, QString::fromUtf8(text.c_str())), messageList_);
  item->setData(Qt::UserRole, static_cast<qulonglong>(message.id));
  if (forwardable) {
    item->setToolTip("Select this message to forward it");
  } else {
    item->setToolTip("This message cannot be forwarded because it could not be decrypted");
  }
  visibleMessages_.push_back({message, text, forwardable});
}

std::optional<std::string> MainWindow::decryptPlaintext(const kd::Message& msg,
                                                        uint64_t recipientId) {
  if (auto cached = messageStore_.getPlaintext(msg.id); cached.has_value()) {
    return cached;
  }

  try {
    auto cachedPk = messageStore_.getCachedPublicKey(msg.senderId);
    std::string senderPk = cachedPk.has_value() ? *cachedPk : client_->getPublicKey(msg.senderId);
    messageStore_.cachePublicKey(msg.senderId, senderPk);

    std::string plaintext =
        kd::LocalKeyStore::decryptMessage(msg.payload, session_.identityKey, senderPk,
                                          msg.conversationId, msg.senderId, recipientId);
    messageStore_.savePlaintext(msg.id, msg.conversationId, msg.senderId, msg.timestamp, plaintext);
    return plaintext;
  } catch (const std::exception& e) {
    spdlog::warn("Decryption failed for message {}: {}", msg.id, e.what());
    return std::nullopt;
  }
}

void MainWindow::loadMessages(uint64_t conversationId) {
  visibleMessages_.clear();
  messageList_->clear();
  forwardButton_->setEnabled(false);

  try {
    auto msgs = client_->getMessages(conversationId);

    // NOLINTNEXTLINE(modernize-use-ranges) - nlohmann::json iterator is not random_access_range
    std::sort(msgs.begin(), msgs.end(), [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
      return lhs["timestamp"].get<uint64_t>() < rhs["timestamp"].get<uint64_t>();
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
      bool forwardable = true;
      if (msg.senderId == session_.userId) {
        // Outgoing: I encrypted this with recipientId = the other person.
        if (auto cached = messageStore_.getPlaintext(msg.id); cached.has_value()) {
          text = *cached;
        } else if (auto plaintext = decryptPlaintext(msg, outgoingRecipient);
                   plaintext.has_value()) {
          text = *plaintext;
        } else {
          text = "[decryption failed]";
          forwardable = false;
        }
      } else {
        // Incoming: sender encrypted with recipientId = me (session_.userId).
        if (auto plaintext = decryptPlaintext(msg, session_.userId); plaintext.has_value()) {
          text = *plaintext;
        } else {
          text = "[decryption failed]";
          forwardable = false;
        }
      }

      appendMessageToView(msg, senderLabel, text, forwardable);
    }

    auto* bar = messageList_->verticalScrollBar();
    bar->setValue(bar->maximum());

  } catch (const std::exception& e) {
    spdlog::warn("Failed to load messages for conversation {}: {}", conversationId, e.what());
    auto* item =
        new QListWidgetItem(QString("Failed to load messages: %1").arg(QString::fromUtf8(e.what())),
                            messageList_);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
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
  for (const auto& user : usersJson) {
    uint64_t uid = user["id"].get<uint64_t>();
    if (uid == session_.userId) {
      continue;
    }
    others.push_back({uid, user["username"].get<std::string>()});
  }

  if (others.empty()) {
    QMessageBox::information(this, "No Users", "No other users are registered yet.");
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("New Conversation");
  dialog.setMinimumWidth(kNewConvDialogMinWidth);

  auto* userCombo = new QComboBox(&dialog);
  for (const auto& user : others) {
    userCombo->addItem(QString::fromUtf8(user.username.c_str()), static_cast<qulonglong>(user.id));
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
  const uint64_t recipientId = userCombo->itemData(userCombo->currentIndex()).toULongLong();

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

std::optional<MainWindow::ForwardTarget> MainWindow::chooseForwardTarget() {
  nlohmann::json usersJson;
  try {
    usersJson = client_->getUsers();
  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Forward Failed",
                          QString("Could not load users: %1").arg(QString::fromUtf8(e.what())));
    return std::nullopt;
  }

  struct UserEntry {
    uint64_t id;
    std::string username;
  };
  std::vector<UserEntry> others;
  for (const auto& user : usersJson) {
    const uint64_t uid = user["id"].get<uint64_t>();
    if (uid != session_.userId) {
      others.push_back({uid, user["username"].get<std::string>()});
    }
  }

  if (others.empty()) {
    QMessageBox::information(this, "Forward Message", "No other users are registered yet.");
    return std::nullopt;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Forward Message");
  dialog.setMinimumWidth(kForwardDialogMinWidth);

  auto* userCombo = new QComboBox(&dialog);
  for (const auto& user : others) {
    userCombo->addItem(QString::fromUtf8(user.username.c_str()), static_cast<qulonglong>(user.id));
  }

  auto* okButton = new QPushButton("Forward", &dialog);
  okButton->setDefault(true);
  auto* cancelButton = new QPushButton("Cancel", &dialog);

  auto* form = new QFormLayout();
  form->addRow("Recipient:", userCombo);

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
    return std::nullopt;
  }

  const uint64_t userId = userCombo->itemData(userCombo->currentIndex()).toULongLong();
  const std::string username{userCombo->currentText().toUtf8().constData()};

  std::string publicKey;
  try {
    publicKey = client_->getPublicKey(userId);
  } catch (const std::exception& e) {
    QMessageBox::critical(
        this, "Forward Failed",
        QString("Could not load recipient identity: %1").arg(QString::fromUtf8(e.what())));
    return std::nullopt;
  }

  if (!confirmRecipientIdentity(userId, username, publicKey)) {
    return std::nullopt;
  }
  messageStore_.cachePublicKey(userId, publicKey);

  if (auto existingConversationId = findConversationWithUser(userId); existingConversationId) {
    return ForwardTarget{.userId = userId,
                         .conversationId = *existingConversationId,
                         .username = username,
                         .publicKey = publicKey};
  }

  try {
    const std::string conversationName = "Chat with " + username;
    auto created = client_->createConversation(conversationName,
                                               std::vector<uint64_t>{session_.userId, userId});
    const uint64_t conversationId = created["id"].get<uint64_t>();
    loadConversations();
    return ForwardTarget{.userId = userId,
                         .conversationId = conversationId,
                         .username = username,
                         .publicKey = publicKey};
  } catch (const std::exception& e) {
    QMessageBox::critical(
        this, "Forward Failed",
        QString("Failed to create destination conversation: %1").arg(QString::fromUtf8(e.what())));
    return std::nullopt;
  }
}

std::optional<uint64_t> MainWindow::findConversationWithUser(uint64_t userId) const {
  for (const auto& conv : conversations_) {
    if (conv.hasParticipant(session_.userId) && conv.hasParticipant(userId)) {
      return conv.id;
    }
  }
  return std::nullopt;
}

QString MainWindow::fingerprintForPublicKey(const std::string& publicKey) {
  std::string fingerprintInput = publicKey;
  auto bundle = nlohmann::json::parse(publicKey, nullptr, false);
  if (bundle.is_object() && bundle.contains("identityKey") && bundle.contains("signingKey")) {
    fingerprintInput =
        nlohmann::json{{"identityKey", bundle["identityKey"]}, {"signingKey", bundle["signingKey"]}}
            .dump();
  }

  const QByteArray digest = QCryptographicHash::hash(QByteArray::fromStdString(fingerprintInput),
                                                     QCryptographicHash::Sha256);
  const QString hex = QString::fromLatin1(digest.toHex());

  QStringList groups;
  for (int i = 0; i < hex.size(); i += 4) {
    groups << hex.mid(i, 4);
  }
  return groups.join(' ');
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool MainWindow::confirmRecipientIdentity(uint64_t userId, const std::string& username,
                                          const std::string& publicKey) {
  const QString fingerprint = fingerprintForPublicKey(publicKey);
  const QString message =
      QString("Verify %1's identity before forwarding.\n\nUser ID: %2\nFingerprint:\n%3")
          .arg(QString::fromUtf8(username.c_str()))
          .arg(static_cast<qulonglong>(userId))
          .arg(fingerprint);

  const auto answer =
      QMessageBox::question(this, "Verify Recipient", message,
                            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
  return answer == QMessageBox::Ok;
}

void MainWindow::onMessageSelectionChanged() {
  auto* item = messageList_->currentItem();
  const bool selected = (item != nullptr) && activeConversationId_.has_value();
  forwardButton_->setEnabled(selected);

  bool ownMessage = false;
  if (selected) {
    const uint64_t messageId = item->data(Qt::UserRole).toULongLong();
    auto iter = std::ranges::find_if(visibleMessages_, [messageId](const DisplayedMessage& d) {
      return d.message.id == messageId;
    });
    ownMessage = (iter != visibleMessages_.end() && iter->message.senderId == session_.userId);
  }
  deleteButton_->setEnabled(ownMessage);
  revokeButton_->setEnabled(ownMessage && activeRecipientId_.has_value());
}

void MainWindow::onForward() {
  auto* selectedItem = messageList_->currentItem();
  if (selectedItem == nullptr) {
    return;
  }

  const uint64_t messageId = selectedItem->data(Qt::UserRole).toULongLong();
  auto iter =
      std::ranges::find_if(visibleMessages_, [messageId](const DisplayedMessage& displayed) {
        return displayed.message.id == messageId;
      });
  if (iter == visibleMessages_.end() || !iter->forwardable) {
    QMessageBox::warning(this, "Cannot Forward", "This message is not available in plaintext.");
    return;
  }

  auto target = chooseForwardTarget();
  if (!target.has_value()) {
    return;
  }

  try {
    const std::string payload =
        kd::LocalKeyStore::encryptMessage(iter->plaintext, session_.identityKey, target->publicKey,
                                          target->conversationId, session_.userId, target->userId);
    auto sentJson =
        client_->sendMessage(target->conversationId, session_.userId, target->userId, payload);
    auto sentMsg = sentJson.get<kd::Message>();
    messageStore_.savePlaintext(sentMsg.id, sentMsg.conversationId, sentMsg.senderId,
                                sentMsg.timestamp, iter->plaintext);

    QMessageBox::information(
        this, "Message Forwarded",
        QString("Forwarded to %1.").arg(QString::fromUtf8(target->username.c_str())));
  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Forward Failed", QString::fromUtf8(e.what()));
  }
}

void MainWindow::onDelete() {
  auto* selectedItem = messageList_->currentItem();
  if (selectedItem == nullptr) {
    return;
  }

  const uint64_t messageId = selectedItem->data(Qt::UserRole).toULongLong();
  auto iter = std::ranges::find_if(visibleMessages_, [messageId](const DisplayedMessage& d) {
    return d.message.id == messageId;
  });
  if (iter == visibleMessages_.end()) {
    return;
  }

  const auto answer =
      QMessageBox::question(this, "Delete Message", "Delete this message? This cannot be undone.",
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  try {
    client_->deleteMessage(iter->message.conversationId, messageId);
    loadMessages(*activeConversationId_);
  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Delete Failed", QString::fromUtf8(e.what()));
  }
}

void MainWindow::onRevoke() {
  auto* selectedItem = messageList_->currentItem();
  if (selectedItem == nullptr) {
    return;
  }

  const uint64_t messageId = selectedItem->data(Qt::UserRole).toULongLong();
  auto iter = std::ranges::find_if(visibleMessages_, [messageId](const DisplayedMessage& d) {
    return d.message.id == messageId;
  });
  if (iter == visibleMessages_.end() || !activeRecipientId_.has_value()) {
    return;
  }

  const std::string recipientName = usernameFor(*activeRecipientId_);
  const auto answer = QMessageBox::question(
      this, "Revoke Access",
      QString("Revoke %1's access to this message?").arg(QString::fromUtf8(recipientName.c_str())),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  try {
    client_->revokeMessageAccess(iter->message.conversationId, messageId, *activeRecipientId_);
    QMessageBox::information(
        this, "Access Revoked",
        QString("%1 can no longer access this message.").arg(QString::fromUtf8(recipientName.c_str())));
  } catch (const std::exception& e) {
    QMessageBox::critical(this, "Revoke Failed", QString::fromUtf8(e.what()));
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
    const std::string payload =
        kd::LocalKeyStore::encryptMessage(text, session_.identityKey, recipientPk, convId,
                                          session_.userId, recipientId);

    auto sentJson = client_->sendMessage(convId, session_.userId, recipientId, payload);
    auto sentMsg = sentJson.get<kd::Message>();

    messageStore_.savePlaintext(sentMsg.id, sentMsg.conversationId, sentMsg.senderId,
                                sentMsg.timestamp, text);

    appendMessageToView(sentMsg, session_.username, text, true);
    auto* bar = messageList_->verticalScrollBar();
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
