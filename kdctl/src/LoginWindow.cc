#include "LoginWindow.hh"

#include <sodium.h>

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <kd/Client.hpp>
#include <kd/LocalKeyStore.hpp>
#include <nlohmann/json.hpp>
#include <string>

static const char* kFieldStyle =
    "QLineEdit {"
    "  border: 1px solid #cbd5e1;"
    "  border-radius: 8px;"
    "  padding: 10px 14px;"
    "  font-size: 14px;"
    "  color: #1e293b;"
    "  background: white;"
    "}"
    "QLineEdit:focus {"
    "  border-color: #2563eb;"
    "  outline: none;"
    "}";

static const char* kPrimaryButtonStyle =
    "QPushButton {"
    "  background-color: #2563eb;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 8px;"
    "  padding: 11px 0;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background-color: #1d4ed8; }"
    "QPushButton:pressed { background-color: #1e40af; }"
    "QPushButton:disabled { background-color: #93c5fd; }";

static const char* kSecondaryButtonStyle =
    "QPushButton {"
    "  background: transparent;"
    "  color: #2563eb;"
    "  border: 1px solid #2563eb;"
    "  border-radius: 8px;"
    "  padding: 11px 0;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background-color: #eff6ff; }"
    "QPushButton:pressed { background-color: #dbeafe; }"
    "QPushButton:disabled { color: #93c5fd; border-color: #93c5fd; }";

static bool isValidSignupPassword(const std::string& password) {
  if (password.size() < 12 || password.size() > 72) {
    return false;
  }

  bool hasUppercase = false;
  bool hasNumber = false;
  for (unsigned char ch : password) {
    hasUppercase = hasUppercase || std::isupper(ch) != 0;
    hasNumber = hasNumber || std::isdigit(ch) != 0;
  }

  return hasUppercase && hasNumber;
}

LoginWindow::LoginWindow(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Kingdom");
  setFixedWidth(420);
  setStyleSheet("background-color: #f8fafc;");

  // ---- Header ----
  auto* logo = new QLabel("K", this);
  logo->setAlignment(Qt::AlignCenter);
  logo->setFixedSize(56, 56);
  logo->setStyleSheet(
      "background-color: #2563eb;"
      "color: white;"
      "font-size: 26px;"
      "font-weight: bold;"
      "border-radius: 16px;");

  auto* title = new QLabel("Kingdom", this);
  title->setAlignment(Qt::AlignCenter);
  title->setStyleSheet("color: #1e293b; font-size: 22px; font-weight: bold;");

  auto* subtitle = new QLabel("Secure end-to-end encrypted messaging", this);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setStyleSheet("color: #64748b; font-size: 13px;");

  // ---- Form card ----
  auto* card = new QWidget(this);
  card->setStyleSheet(
      "background: white;"
      "border-radius: 12px;"
      "border: 1px solid #e2e8f0;");

  serverUrlEdit_ = new QLineEdit("https://localhost:8080", card);
  serverUrlEdit_->setStyleSheet(kFieldStyle);
  serverUrlEdit_->setPlaceholderText("Server URL");

  usernameEdit_ = new QLineEdit(card);
  usernameEdit_->setStyleSheet(kFieldStyle);
  usernameEdit_->setPlaceholderText("Username");

  passwordEdit_ = new QLineEdit(card);
  passwordEdit_->setStyleSheet(kFieldStyle);
  passwordEdit_->setPlaceholderText("Password");
  passwordEdit_->setEchoMode(QLineEdit::Password);

  loginButton_ = new QPushButton("Log In", card);
  loginButton_->setStyleSheet(kPrimaryButtonStyle);
  loginButton_->setCursor(Qt::PointingHandCursor);
  loginButton_->setDefault(true);

  signupButton_ = new QPushButton("Sign Up", card);
  signupButton_->setStyleSheet(kSecondaryButtonStyle);
  signupButton_->setCursor(Qt::PointingHandCursor);

  errorLabel_ = new QLabel(card);
  errorLabel_->setStyleSheet(
      "color: #dc2626; font-size: 13px; background: #fef2f2;"
      "border: 1px solid #fecaca; border-radius: 6px; padding: 8px 12px;");
  errorLabel_->setWordWrap(true);
  errorLabel_->hide();

  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(28, 28, 28, 28);
  cardLayout->setSpacing(12);
  cardLayout->addWidget(serverUrlEdit_);
  cardLayout->addWidget(usernameEdit_);
  cardLayout->addWidget(passwordEdit_);
  cardLayout->addSpacing(4);
  cardLayout->addWidget(loginButton_);
  cardLayout->addWidget(signupButton_);
  cardLayout->addWidget(errorLabel_);

  // ---- Root layout ----
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(32, 32, 32, 32);
  root->setSpacing(16);
  root->addWidget(logo, 0, Qt::AlignHCenter);
  root->addWidget(title);
  root->addWidget(subtitle);
  root->addSpacing(8);
  root->addWidget(card);

  connect(loginButton_, &QPushButton::clicked, this, &LoginWindow::onLogin);
  connect(signupButton_, &QPushButton::clicked, this, &LoginWindow::onSignup);
  connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginWindow::onLogin);
}

void LoginWindow::showError(const QString& msg) {
  errorLabel_->setText(msg);
  errorLabel_->show();
}

void LoginWindow::onLogin() {
  performAuth(false);
}

void LoginWindow::onSignup() {
  performAuth(true);
}

void LoginWindow::performAuth(bool isSignup) {
  errorLabel_->hide();

  const std::string serverUrl{serverUrlEdit_->text().trimmed().toUtf8().constData()};
  const std::string username{usernameEdit_->text().trimmed().toUtf8().constData()};
  std::string password{passwordEdit_->text().toUtf8().constData()};

  if (serverUrl.empty()) {
    showError("Server URL is required.");
    return;
  }
  if (username.empty()) {
    showError("Username is required.");
    return;
  }
  if (password.empty()) {
    showError("Password is required.");
    return;
  }
  if (isSignup && !isValidSignupPassword(password)) {
    showError(
        "Password must be 12-72 characters and include at least one uppercase letter and one "
        "number.");
    return;
  }

  loginButton_->setEnabled(false);
  signupButton_->setEnabled(false);
  loginButton_->setText(isSignup ? "Signing up..." : "Logging in...");

  try {
    const char* caCertEnv = std::getenv("KD_CA_CERT");
    std::string caCertPath = (caCertEnv != nullptr) ? caCertEnv : "";
    kd::Client client(serverUrl, caCertPath);

    nlohmann::json userJson;
    if (isSignup) {
      userJson = client.signup(username, password);
    } else {
      userJson = client.login(username, password);
    }

    if (!userJson.contains("token")) {
      showError("Server response missing token.");
      loginButton_->setEnabled(true);
      signupButton_->setEnabled(true);
      loginButton_->setText("Log In");
      return;
    }

    const std::string token = userJson["token"].get<std::string>();
    client.setAuthToken(token);

    kd::LocalIdentityKey identityKey = kd::LocalKeyStore::loadForLogin(username, password);
    kd::MessageStore messageStore = kd::MessageStore::encryptedForUser(username, password);
    sodium_memzero(password.data(), password.size());

    LoginResult res;
    res.userId = userJson["id"].get<uint64_t>();
    res.username = userJson["username"].get<std::string>();
    res.token = token;
    res.identityKey = std::move(identityKey);
    res.messageStore = std::move(messageStore);
    res.serverUrl = serverUrl;

    result_ = std::move(res);
    accept();

  } catch (const std::exception& e) {
    if (!password.empty()) {
      sodium_memzero(password.data(), password.size());
    }
    showError(QString::fromUtf8(e.what()));
    loginButton_->setEnabled(true);
    signupButton_->setEnabled(true);
    loginButton_->setText("Log In");
  }
}
